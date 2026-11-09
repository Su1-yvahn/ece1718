#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>

#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include "defines.h"

#define MIN_OBJECTS 1
#define START_DELAY_NS 200000000L // initial 0.2 second sleep time (200ms)
#define DELAY_STEP_NS 25000000L   // 0.025 second step (25ms)
#define MIN_DELAY_NS 50000000L    // Fastest animation (50ms = 20FPS)
#define MAX_DELAY_NS 500000000L   // Slowest animation (500ms = 2FPS)

volatile sig_atomic_t stop = 0;

int min_x, min_y, max_x, max_y;
int object_x[NUM_XS_MAX], object_y[NUM_XS_MAX];
int delta_x[NUM_XS_MAX], delta_y[NUM_XS_MAX];
char object_color[NUM_XS_MAX];
int number_of_objects = NUM_XS;
int show_lines = TRUE;
long delay_ns = START_DELAY_NS;
unsigned long frames_rendered = 0;
char speed_message[24] = "Normal speed";

void catchSIGINT(int signum);
void get_screen_specs(void);
void clear_screen(void);
void plot_pixel(int x, int y, char color, char c);
void draw_line(int x0, int y0, int x1, int y1, char color);
void initialize_object(int i);
void initialize_objects(void);
void read_controls(int key_fd, int sw_fd);
int read_hex_device(int fd, int max_bytes, unsigned int *value);
void draw(void);

int main(void)
{
    int key_fd, sw_fd;
    struct timespec animation_time;
    int exit_status = 0;

    // capture Ctrl+C
    signal(SIGINT, catchSIGINT);

    key_fd = open("/dev/KEY", O_RDONLY); // only read
    if (key_fd == -1)
    {
        fprintf(stderr, "Cannot open /dev/KEY: %s\n", strerror(errno));
        return 1;
    }
    sw_fd = open("/dev/SW", O_RDONLY); // only read
    if (sw_fd == -1)
    {
        fprintf(stderr, "Cannot open /dev/SW: %s\n", strerror(errno));
        close(key_fd);
        return 1;
    }

    get_screen_specs();
    srand((unsigned int)time(NULL)); // true randomization
    initialize_objects();

    clear_screen();
    printf("\033[?25l\033[?7l"); // Hide cursor; disable terminal auto-wrap
    fflush(stdout);

    while (!stop)
    {
        read_controls(key_fd, sw_fd);
        if (stop)
            break;

        draw();
        fflush(stdout);

        animation_time.tv_sec = 0;
        animation_time.tv_nsec = delay_ns; // start with 0.2 sec sleep
        nanosleep(&animation_time, NULL);
    }

    // After finish drawing
    clear_screen();
    printf("\033[%dm\033[1;1H\033[?7h\033[?25h", WHITE);
    fflush(stdout);
    if (close(key_fd) < 0)
        exit_status = 1;
    if (close(sw_fd) < 0)
        exit_status = 1;
    return exit_status;
}

void catchSIGINT(int signum)
{
    (void)signum;
    stop = 1;
}

void get_screen_specs(void)
{
    min_x = 1;
    max_x = SCREEN_X;
    min_y = 2; // Reserve rows 1 and SCREEN_Y for the status labels
    max_y = SCREEN_Y - 1;
}

void clear_screen(void)
{
    printf("\033[2J");
}

void initialize_object(int i)
{
    const char colors[] = {RED, GREEN, YELLOW, BLUE, MAGENTA, CYAN, WHITE};
    object_x[i] = min_x + rand() % (max_x - min_x + 1);
    object_y[i] = min_y + rand() % (max_y - min_y + 1);
    delta_x[i] = (rand() % 2) ? 1 : -1;
    delta_y[i] = (rand() % 2) ? 1 : -1;
    object_color[i] = colors[rand() % (sizeof(colors) / sizeof(colors[0]))];
}

void initialize_objects(void)
{
    int i;
    for (i = 0; i < NUM_XS_MAX; ++i)
        initialize_object(i);
}

// Read all available bytes until the driver reports EOF
int read_hex_device(int fd, int max_bytes, unsigned int *value) // *value: Pointer where the converted integer will be stored
{
    char buffer[SW_BYTES + 1];
    int received = 0;
    ssize_t n;

    // continue reading until we received the expected bytes or the driver reports end-of-file
    while (received < max_bytes)
    {
        n = read(fd, buffer + received, (size_t)(max_bytes - received));
        if (n < 0)
        {
            if (errno == EINTR)
            {
                if (stop)
                    return -1;
                continue;
            }
            return -1;
        }
        if (n == 0) // EOF indicator for the read sequence
            break;
        received += (int)n;
    }

    // Consume EOF to reset the offset on the driver's next read
    if (received == max_bytes)
    {
        char extra;
        n = read(fd, &extra, 1);
        if (n < 0 && errno != EINTR)
            return -1;
    }

    buffer[received] = '\0';
    if (received == 0 || sscanf(buffer, "%x", value) != 1)
        return -1;
    return 0;
}

void read_controls(int key_fd, int sw_fd)
{
    unsigned int keys = 0, switches = 0;

    if (read_hex_device(key_fd, KEY_BYTES, &keys) < 0) // /dev/KEY returns a hex digit plus newline
    {
        if (!stop)
        {
            fprintf(stderr, "\nError reading /dev/KEY: %s\n", strerror(errno));
            stop = 1;
        }
        return;
    }
    if (read_hex_device(sw_fd, SW_BYTES, &switches) < 0) // /dev/SW returns 3 hex digits plus newline
    {
        if (!stop)
        {
            fprintf(stderr, "\nError reading /dev/SW: %s\n", strerror(errno));
            stop = 1;
        }
        return;
    }

    // KEY0/KEY1 control speed; lower nanosleep delay means faster
    if (keys & 0x1)
    {
        if (delay_ns > MIN_DELAY_NS)
        {
            delay_ns -= DELAY_STEP_NS;
            strcpy(speed_message, "Faster");
        }
        else
        {
            strcpy(speed_message, "Maximum speed");
        }
    }
    if (keys & 0x2)
    {
        if (delay_ns < MAX_DELAY_NS)
        {
            delay_ns += DELAY_STEP_NS;
            strcpy(speed_message, "Slower");
        }
        else
        {
            strcpy(speed_message, "Minimum speed");
        }
    }

    // KEY2 adds one object; KEY3 removes one
    if ((keys & 0x4) && number_of_objects < NUM_XS_MAX)
    {
        initialize_object(number_of_objects);
        ++number_of_objects;
    }
    if ((keys & 0x8) && number_of_objects > MIN_OBJECTS)
        --number_of_objects;

    // Any nonzero SW value hides ALL connecting lines
    show_lines = (switches == 0) ? TRUE : FALSE;
}

void draw(void)
{
    int i;

    clear_screen();

    if (show_lines)
    {
        for (i = 0; i < number_of_objects - 1; ++i)
        {
            draw_line(object_x[i], object_y[i], object_x[i + 1], object_y[i + 1], object_color[i]);
        }
    }
    for (i = 0; i < number_of_objects; ++i)
        plot_pixel(object_x[i], object_y[i], object_color[i], 'X');

    ++frames_rendered;
    printf("\033[%dm\033[1;1HFrames rendered: %lu   Objects: %d   Lines: %s", WHITE, frames_rendered, number_of_objects, show_lines ? "ON" : "OFF");
    printf("\033[%dm\033[%d;52H%-14s %3ld ms/frame", CYAN, SCREEN_Y, speed_message, delay_ns / 1000000L); // delay is stored in nanoseconds, the terminal displays milliseconds

    for (i = 0; i < number_of_objects; ++i)
    {
        if (object_x[i] <= min_x && delta_x[i] < 0)
            delta_x[i] = 1;
        else if (object_x[i] >= max_x && delta_x[i] > 0)
            delta_x[i] = -1;

        if (object_y[i] <= min_y && delta_y[i] < 0)
            delta_y[i] = 1;
        else if (object_y[i] >= max_y && delta_y[i] > 0)
            delta_y[i] = -1;

        object_x[i] += delta_x[i];
        object_y[i] += delta_y[i];
    }
}

// The same integer Bresenham algorithm
void draw_line(int x0, int y0, int x1, int y1, char color)
{
    int steep = (ABS(y1 - y0) > ABS(x1 - x0));
    int tmp, deltax, deltay, error, ystep, x, y;

    if (steep)
    {
        tmp = x0;
        x0 = y0;
        y0 = tmp;
        tmp = x1;
        x1 = y1;
        y1 = tmp;
    }
    if (x0 > x1)
    {
        tmp = x0;
        x0 = x1;
        x1 = tmp;
        tmp = y0;
        y0 = y1;
        y1 = tmp;
    }
    deltax = x1 - x0;
    deltay = ABS(y1 - y0);
    error = -(deltax / 2);
    y = y0;
    ystep = (y0 < y1) ? 1 : -1;

    for (x = x0; x <= x1; ++x)
    {
        if (steep)
            plot_pixel(y, x, color, '|');
        else
            plot_pixel(x, y, color, '*');
        error += deltay;
        if (error > 0)
        {
            y += ystep;
            error -= deltax;
        }
    }
}

void plot_pixel(int x, int y, char color, char c)
{
    printf("\033[%dm\033[%d;%dH%c", color, y, x, c);
}
