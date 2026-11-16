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
#define SHAPE_TYPES 3
#define MAX_SHAPES_PER_TYPE 8

#define SW_RECTANGLE (1u << 0)
#define SW_TRIANGLE (1u << 1)
#define SW_SQUARE (1u << 2)
#define SW_MIXED (1u << 3)
#define SW_HIDE (1u << 9)
#define KEY0 0x1
#define KEY1 0x2
#define KEY2 0x4
#define KEY3 0x8
#define MIN_SPEED 1
#define NORMAL_SPEED 5
#define MAX_SPEED 9
#define SLOW_STEP_NS 25000000L

enum shape_type
{
    RECTANGLE,
    TRIANGLE,
    SQUARE
};

typedef struct
{
    int x, y, dx, dy, width, height, color;
} Shape;

volatile sig_atomic_t stop = 0;
int screen_x, screen_y;
int object_x[NUM_XS_MAX], object_y[NUM_XS_MAX];
int delta_x[NUM_XS_MAX], delta_y[NUM_XS_MAX];
int object_color[NUM_XS_MAX];
Shape shapes[SHAPE_TYPES][MAX_SHAPES_PER_TYPE];
int number_of_objects = NUM_XS;
int number_of_shapes = 1;
int speed_level = NORMAL_SPEED;
unsigned int switch_state = 0;
unsigned int enabled_types = 0;
unsigned long frame_count = 0;

void catchSIGINT(int signum);
void initialize_object(int i);
void initialize_objects(void);
void initialize_shapes(void);
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
    initialize_shapes();
    write(video_FD, "erase", strlen("erase"));

    while (!stop)
    {
        // Read KEY presses and the current positions of the slide switches.
        if (read_controls(KEY_FD, SW_FD) < 0)
            break;

        draw(video_FD);

        // update the VGA character buffer with the frame count.
        // This text is separate from the double-buffered pixel graphics.
        {
            char text_command[64];
            ++frame_count;
            snprintf(text_command, sizeof(text_command),
                     "text 0,0 Frames rendered: %lu", frame_count);
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
    if (read_hex_device(KEY_FD, KEY_BYTES, &KEY_value) < 0 ||
        read_hex_device(SW_FD, SW_BYTES, &SW_value) < 0)
    {
        if (!stop)
            printf("Error reading KEY/SW devices: %s\n", strerror(errno));
        return -1;
    }
    switch_state = SW_value;
    enabled_types = (SW_value & SW_MIXED) ? 0x7u : (SW_value & 0x7u);

    if ((KEY_value & KEY0) && speed_level < MAX_SPEED)
        ++speed_level;
    if ((KEY_value & KEY1) && speed_level > MIN_SPEED)
        --speed_level;

    // With SW0-3 OFF, they instead change the original Part 5 box-chain count.
    if (enabled_types)
    {
        if ((KEY_value & KEY2) && number_of_shapes < MAX_SHAPES_PER_TYPE)
            ++number_of_shapes;
        if ((KEY_value & KEY3) && number_of_shapes > 1)
            --number_of_shapes;
    }
    else
    {
        if ((KEY_value & KEY2) && number_of_objects < NUM_XS_MAX)
            ++number_of_objects;
        if ((KEY_value & KEY3) && number_of_objects > MIN_OBJECTS)
            --number_of_objects;
    }
    return 0;
}

void initialize_shapes(void)
{
    const int colors[] = {RED, GREEN, YELLOW, BLUE, MAGENTA, CYAN, WHITE, ORANGE};
    int type, i;
    for (type = 0; type < SHAPE_TYPES; ++type)
    {
        for (i = 0; i < MAX_SHAPES_PER_TYPE; ++i)
        {
            Shape *s = &shapes[type][i];
            // Approximate relative proportions of Exercise 5's terminal shapes.
            s->width = (type == RECTANGLE) ? 45 : (type == TRIANGLE ? 35 : 26);
            s->height = (type == RECTANGLE) ? 18 : (type == TRIANGLE ? 31 : 26);
            s->x = rand() % (screen_x - s->width + 1);
            s->y = rand() % (screen_y - s->height + 1);
            s->dx = (rand() % 2) ? 1 : -1;
            s->dy = (rand() % 2) ? 1 : -1;
            s->color = colors[rand() % (sizeof(colors) / sizeof(colors[0]))];
        }
    }
}

void update_shape(Shape *s, int step)
{
    int next_x = s->x + s->dx * step;
    int next_y = s->y + s->dy * step;
    if (next_x < 0 || next_x > screen_x - s->width)
        s->dx = -s->dx;
    if (next_y < 0 || next_y > screen_y - s->height)
        s->dy = -s->dy;
    s->x += s->dx * step;
    s->y += s->dy * step;
    // Keep the whole outline within the 320x240 pixel area.
    if (s->x < 0)
        s->x = 0;
    if (s->y < 0)
        s->y = 0;
    if (s->x > screen_x - s->width)
        s->x = screen_x - s->width;
    if (s->y > screen_y - s->height)
        s->y = screen_y - s->height;
}

void draw_shape(int video_FD, const Shape *s, int type, int hide_outline)
{
    int left = s->x, top = s->y;
    int right = left + s->width - 1, bottom = top + s->height - 1;
    int center_x = (left + right) / 2, center_y = (top + bottom) / 2;
    if (!hide_outline)
    {
        if (type == TRIANGLE)
        {
            draw_line(video_FD, left, bottom, center_x, top, s->color);
            draw_line(video_FD, center_x, top, right, bottom, s->color);
            draw_line(video_FD, right, bottom, left, bottom, s->color);
        }
        else
        {
            draw_line(video_FD, left, top, right, top, s->color);
            draw_line(video_FD, right, top, right, bottom, s->color);
            draw_line(video_FD, right, bottom, left, bottom, s->color);
            draw_line(video_FD, left, bottom, left, top, s->color);
        }
    }
    // Keep the moving center marker when SW9 hides the shape outline.
    draw_box(video_FD, center_x - 2, center_y - 2, center_x + 2, center_y + 2, s->color);
}

void draw(int video_FD)
{
    int i, type;
    int step = (speed_level > NORMAL_SPEED) ? speed_level - NORMAL_SPEED + 1 : 1;
    int hide_outline = (switch_state & SW_HIDE) != 0;
    write(video_FD, "clear", strlen("clear"));

    if (enabled_types)
    {
        // SW3 overrides SW0-2: all three shape families are displayed.
        for (type = 0; type < SHAPE_TYPES; ++type)
        {
            if (!(enabled_types & (1u << type)))
                continue;
            for (i = 0; i < number_of_shapes; ++i)
                draw_shape(video_FD, &shapes[type][i], type, hide_outline);
        }
    }
    else
    {
        // No shape switches: preserve Part 5's original connected-box animation.
        // SW9 alone hides the connecting lines, not the boxes.
        if (switch_state == 0)
        {
            for (i = 0; i < number_of_objects - 1; ++i)
                draw_line(video_FD, object_x[i] + BOX_SIZE / 2,
                          object_y[i] + BOX_SIZE / 2, object_x[i + 1] + BOX_SIZE / 2,
                          object_y[i + 1] + BOX_SIZE / 2, object_color[i]);
        }
        for (i = 0; i < number_of_objects; ++i)
            draw_box(video_FD, object_x[i], object_y[i],
                     object_x[i] + BOX_SIZE - 1, object_y[i] + BOX_SIZE - 1,
                     object_color[i]);
    }

    // Maintain each animation's positions, even when its mode is inactive.
    for (type = 0; type < SHAPE_TYPES; ++type)
        for (i = 0; i < MAX_SHAPES_PER_TYPE; ++i)
            update_shape(&shapes[type][i], step);

    for (i = 0; i < number_of_objects; ++i)
    {
        if (object_x[i] + delta_x[i] * step < 0 ||
            object_x[i] + delta_x[i] * step > screen_x - BOX_SIZE)
            delta_x[i] = -delta_x[i];
        if (object_y[i] + delta_y[i] * step < 0 ||
            object_y[i] + delta_y[i] * step > screen_y - BOX_SIZE)
            delta_y[i] = -delta_y[i];
        object_x[i] += delta_x[i] * step;
        object_y[i] += delta_y[i] * step;
        if (object_x[i] < 0)
            object_x[i] = 0;
        if (object_y[i] < 0)
            object_y[i] = 0;
        if (object_x[i] > screen_x - BOX_SIZE)
            object_x[i] = screen_x - BOX_SIZE;
        if (object_y[i] > screen_y - BOX_SIZE)
            object_y[i] = screen_y - BOX_SIZE;
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
