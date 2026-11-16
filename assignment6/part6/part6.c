#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include "../include/defines.h"

#define video_BYTES 8
#define KEY_BYTES 2
#define SW_BYTES 4
#define NUM_XS 8
#define NUM_XS_MAX 25
#define MIN_OBJECTS 1
#define BOX_SIZE 5

#define KEY0 0x1
#define KEY1 0x2
#define KEY2 0x4
#define KEY3 0x8

#define MIN_SPEED 1
#define NORMAL_SPEED 5
#define MAX_SPEED 9
#define SLOW_STEP_NS 25000000L // Each lower speed level adds 25 ms per frame

volatile sig_atomic_t stop = 0;
int screen_x, screen_y;
int object_x[NUM_XS_MAX], object_y[NUM_XS_MAX];
int delta_x[NUM_XS_MAX], delta_y[NUM_XS_MAX];
int object_color[NUM_XS_MAX];
int number_of_objects = NUM_XS;
int speed_level = NORMAL_SPEED;
int show_lines = 1;
unsigned long frame_count = 0;

void catchSIGINT(int signum);
void initialize_object(int i);
void initialize_objects(void);
int read_hex_device(int fd, int max_bytes, unsigned int *value);
int read_controls(int KEY_FD, int SW_FD);
void draw(int video_FD);
void draw_line(int video_FD, int x0, int y0, int x1, int y1, int color);
void draw_box(int video_FD, int x0, int y0, int x1, int y1, int color);

int main(void)
{
    int video_FD, KEY_FD, SW_FD;
    char video_buffer[video_BYTES + 1];
    struct timespec animation_time;

    signal(SIGINT, catchSIGINT);

    if ((video_FD = open("/dev/video", O_RDWR)) == -1)
    {
        printf("Error opening /dev/video: %s\n", strerror(errno));
        return -1;
    }

    if ((KEY_FD = open("/dev/KEY", O_RDONLY)) == -1)
    {
        printf("Error opening /dev/KEY: %s\n", strerror(errno));
        close(video_FD);
        return -1;
    }
    if ((SW_FD = open("/dev/SW", O_RDONLY)) == -1)
    {
        printf("Error opening /dev/SW: %s\n", strerror(errno));
        close(KEY_FD);
        close(video_FD);
        return -1;
    }

    memset(video_buffer, 0, sizeof(video_buffer));
    if (read(video_FD, video_buffer, video_BYTES) <= 0 ||
        sscanf(video_buffer, "%d %d", &screen_x, &screen_y) != 2 ||
        screen_x < BOX_SIZE || screen_y < BOX_SIZE)
    {
        printf("Error reading video screen dimensions\n");
        close(SW_FD);
        close(KEY_FD);
        close(video_FD);
        return -1;
    }

    srand((unsigned int)time(NULL));
    initialize_objects();
    write(video_FD, "erase", strlen("erase"));

    while (!stop)
    {
        // Read KEY presses and the current positions of the slide switches.
        if (read_controls(KEY_FD, SW_FD) < 0)
            break;

        draw(video_FD);

        // update the VGA character buffer with the frame count.
        // Character buffer is separate from the double-buffered pixel buffers
        {
            char text_command[64];
            ++frame_count;
            // draw on the coordinate (0,0)
            snprintf(text_command, sizeof(text_command), "text 0,0 Frames rendered: %lu", frame_count); // "text" is a command that the driver recognizes
            write(video_FD, text_command, strlen(text_command));
        }

        // Wait for VGA synchronization and swap the front/back buffers.
        write(video_FD, "sync", strlen("sync"));

        // Below normal speed, add sleep after VGA synchronization.
        // Above normal speed, move the rectangles more pixels per frame.
        if (speed_level < NORMAL_SPEED)
        {
            animation_time.tv_sec = 0;
            animation_time.tv_nsec = (NORMAL_SPEED - speed_level) * SLOW_STEP_NS;
            nanosleep(&animation_time, NULL);
        }
    }

    close(SW_FD);
    close(KEY_FD);
    close(video_FD);
    return 0;
}

void catchSIGINT(int signum)
{
    (void)signum;
    stop = 1;
}

void initialize_object(int i)
{
    const int colors[] = {RED, GREEN, YELLOW, BLUE, MAGENTA, CYAN, WHITE, ORANGE};

    object_x[i] = rand() % (screen_x - BOX_SIZE + 1);
    object_y[i] = rand() % (screen_y - BOX_SIZE + 1);
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

// Read the driver's hexadecimal text and consume EOF to reset its file offset.
// KEY returns 1 hex digit + newline, while SW returns 3 digits + newline.
int read_hex_device(int fd, int max_bytes, unsigned int *value)
{
    char buffer[SW_BYTES + 1];
    int chars_read = 0;
    ssize_t ret_val;

    while (chars_read < max_bytes)
    {
        ret_val = read(fd, buffer + chars_read, max_bytes - chars_read);
        if (ret_val < 0)
        {
            if (errno == EINTR && !stop)
                continue;
            return -1;
        }
        if (ret_val == 0)
            break;
        chars_read += ret_val;
    }

    // the next read returns EOF and resets the offset.
    if (chars_read == max_bytes)
    {
        char extra;
        ret_val = read(fd, &extra, 1);
        if (ret_val < 0)
            return -1;
    }

    buffer[chars_read] = '\0';
    if (chars_read == 0 || sscanf(buffer, "%X", value) != 1)
        return -1;
    return 0;
}

int read_controls(int KEY_FD, int SW_FD)
{
    unsigned int KEY_value = 0, SW_value = 0;

    if (read_hex_device(KEY_FD, KEY_BYTES, &KEY_value) < 0)
    {
        if (!stop)
            printf("Error reading /dev/KEY: %s\n", strerror(errno));
        return -1;
    }
    if (read_hex_device(SW_FD, SW_BYTES, &SW_value) < 0)
    {
        if (!stop)
            printf("Error reading /dev/SW: %s\n", strerror(errno));
        return -1;
    }

    // KEY0 speeds up the animation, and KEY1 slows it down.
    if ((KEY_value & KEY0) && speed_level < MAX_SPEED)
        ++speed_level;
    if ((KEY_value & KEY1) && speed_level > MIN_SPEED)
        --speed_level;

    // KEY2 adds one rectangle, and KEY3 removes one rectangle.
    if ((KEY_value & KEY2) && number_of_objects < NUM_XS_MAX)
        ++number_of_objects;
    if ((KEY_value & KEY3) && number_of_objects > MIN_OBJECTS)
        --number_of_objects;

    // All switches OFF: show lines; any switch ON: hide lines.
    show_lines = (SW_value == 0);
    return 0;
}

void draw(int video_FD)
{
    int i, step;

    // Clear and redraw the back buffer before displaying the next frame.
    write(video_FD, "clear", strlen("clear"));

    if (show_lines)
    {
        for (i = 0; i < number_of_objects - 1; ++i)
        {
            draw_line(video_FD, object_x[i] + BOX_SIZE / 2, object_y[i] + BOX_SIZE / 2, object_x[i + 1] + BOX_SIZE / 2, object_y[i + 1] + BOX_SIZE / 2, object_color[i]);
        }
    }

    for (i = 0; i < number_of_objects; ++i)
    {
        draw_box(video_FD, object_x[i], object_y[i], object_x[i] + BOX_SIZE - 1, object_y[i] + BOX_SIZE - 1, object_color[i]);
    }

    // One pixel per frame normally; faster levels move 2-5 pixels per frame.
    step = (speed_level > NORMAL_SPEED) ? speed_level - NORMAL_SPEED + 1 : 1;
    for (i = 0; i < number_of_objects; ++i)
    {
        if (object_x[i] + delta_x[i] * step < 0 || object_x[i] + delta_x[i] * step > screen_x - BOX_SIZE)
            delta_x[i] = -delta_x[i];

        if (object_y[i] + delta_y[i] * step < 0 || object_y[i] + delta_y[i] * step > screen_y - BOX_SIZE)
            delta_y[i] = -delta_y[i];

        object_x[i] += delta_x[i] * step;
        object_y[i] += delta_y[i] * step;
    }
}

void draw_line(int video_FD, int x0, int y0, int x1, int y1, int color)
{
    char command[64];
    sprintf(command, "line %d,%d %d,%d %X\n", x0, y0, x1, y1, color);
    write(video_FD, command, strlen(command));
}

void draw_box(int video_FD, int x0, int y0, int x1, int y1, int color)
{
    char command[64];
    sprintf(command, "box %d,%d %d,%d %X\n", x0, y0, x1, y1, color);
    write(video_FD, command, strlen(command));
}
