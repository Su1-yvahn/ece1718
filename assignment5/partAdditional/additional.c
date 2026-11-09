#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include "defines.h"

#define SHAPE_TYPES 3
#define MAX_SHAPES_PER_TYPE 8
#define START_DELAY_NS 200000000L
#define DELAY_STEP_NS 25000000L
#define MIN_DELAY_NS 50000000L
#define MAX_DELAY_NS 500000000L
#define SW_RECTANGLE (1u << 0) // left shift unsigned integer 1 by 0 poosition
#define SW_TRIANGLE (1u << 1)
#define SW_SQUARE (1u << 2)
#define SW_MIXED (1u << 3)
#define SW_HIDE (1u << 9)

enum shape_type
{
    RECTANGLE,
    TRIANGLE,
    SQUARE
};

typedef struct
{
    int x, y;          // top-left corner of the shape's bounding box
    int dx, dy;        // diagonal movement (+1 or -1 on each axis)
    int width, height; // width and height measured in terminal steps
    char color;
} Shape;

static volatile sig_atomic_t stop = 0;
static Shape shapes[SHAPE_TYPES][MAX_SHAPES_PER_TYPE];
static Shape objects[NUM_XS_MAX];
static const char colors[] = {RED, GREEN, YELLOW, BLUE, MAGENTA, CYAN, WHITE};
static int shape_count = 1;
static int object_count = NUM_XS;
static unsigned int switch_state = 0;
static unsigned int enabled_types = 0;
static long delay_ns = START_DELAY_NS;
static unsigned long frames_rendered = 0;
static char speed_message[24] = "Normal speed";

static void catchSIGINT(int signum)
{
    (void)signum;
    stop = 1;
}
static void clear_screen(void) { printf("\033[2J"); }

static int random_between(int low, int high)
{
    return low + rand() % (high - low + 1);
}

static void plot_pixel(int x, int y, char color, char c)
{
    if (x >= 1 && x <= SCREEN_X && y >= 2 && y < SCREEN_Y)
        printf("\033[%dm\033[%d;%dH%c", color, y, x, c);
}

// Bresenham integer line drawing
static void draw_line(int x0, int y0, int x1, int y1, char color)
{
    int steep = ABS(y1 - y0) > ABS(x1 - x0);
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
    ystep = y0 < y1 ? 1 : -1;
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

static void init_shape(Shape *s, int type)
{
    /* Widths are larger than heights to compensate for tall terminal glyphs. */
    if (type == RECTANGLE)
    {
        s->width = 15;
        s->height = 4;
    }
    else if (type == TRIANGLE)
    {
        s->width = 12;
        s->height = 6;
    }
    else if (type == SQUARE)
    {
        s->width = 10;
        s->height = 5;
    }
    else
    {
        s->width = 0;
        s->height = 0;
    } // Single X for chain mode

    s->x = random_between(1, SCREEN_X - s->width);
    s->y = random_between(2, SCREEN_Y - 1 - s->height);
    s->dx = rand() % 2 ? 1 : -1;
    s->dy = rand() % 2 ? 1 : -1;
    s->color = colors[rand() % (sizeof(colors) / sizeof(colors[0]))];
}

static void initialize_everything(void)
{
    int type, i;
    for (type = 0; type < SHAPE_TYPES; ++type)
        for (i = 0; i < MAX_SHAPES_PER_TYPE; ++i)
            init_shape(&shapes[type][i], type);
    for (i = 0; i < NUM_XS_MAX; ++i)
        init_shape(&objects[i], -1);
}

// Checking boundary, The whole bounding box stays within the screen
static void update_position(Shape *s)
{
    if ((s->x <= 1 && s->dx < 0) || (s->x + s->width >= SCREEN_X && s->dx > 0))
        s->dx = -s->dx;
    if ((s->y <= 2 && s->dy < 0) || (s->y + s->height >= SCREEN_Y - 1 && s->dy > 0))
        s->dy = -s->dy;
    s->x += s->dx;
    s->y += s->dy;
}

static void draw_shape(const Shape *s, int type, int hide_outline)
{
    int x = s->x, y = s->y;
    int w = s->width, h = s->height;
    if (!hide_outline)
    {
        if (type == TRIANGLE)
        {
            draw_line(x, y + h, x + w / 2, y, s->color);
            draw_line(x + w / 2, y, x + w, y + h, s->color);
            draw_line(x + w, y + h, x, y + h, s->color);
        }
        else
        { // Rectangle or square, all four edges
            draw_line(x, y, x + w, y, s->color);
            draw_line(x + w, y, x + w, y + h, s->color);
            draw_line(x + w, y + h, x, y + h, s->color);
            draw_line(x, y + h, x, y, s->color);
        }
    }
    // The X travels together with its shape; SW9 hides only the outline
    plot_pixel(x + w / 2, y + h / 2, s->color, 'X');
}

// In shape mode SW3 has highest priority, overrides SW0-2. Otherwise SW0-2 can combine
static unsigned int get_enabled_types(unsigned int sw)
{
    return (sw & SW_MIXED) ? 0x7u : (sw & 0x7u);
}

// Drivers return hex text, then EOF. Explicitly consume EOF each frame, so the next read returns fresh KEY edge-capture and SW state data
static int read_hex_device(int fd, int max_bytes, unsigned int *value)
{
    char buffer[SW_BYTES + 1];
    int received = 0;
    ssize_t n;
    while (received < max_bytes)
    {
        n = read(fd, buffer + received, (size_t)(max_bytes - received));
        if (n < 0)
        {
            if (errno == EINTR && !stop)
                continue;
            return -1;
        }
        if (n == 0)
            break;
        received += (int)n;
    }
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

static int read_controls(int key_fd, int sw_fd)
{
    unsigned int keys = 0, sw = 0;
    if (read_hex_device(key_fd, KEY_BYTES, &keys) < 0 ||
        read_hex_device(sw_fd, SW_BYTES, &sw) < 0)
        return -1;

    switch_state = sw;
    enabled_types = get_enabled_types(sw);

    if (keys & 0x1u)
    {
        if (delay_ns > MIN_DELAY_NS)
        {
            delay_ns -= DELAY_STEP_NS;
            strcpy(speed_message, "Faster");
        }
        else
            strcpy(speed_message, "Maximum speed");
    }
    if (keys & 0x2u)
    {
        if (delay_ns < MAX_DELAY_NS)
        {
            delay_ns += DELAY_STEP_NS;
            strcpy(speed_message, "Slower");
        }
        else
            strcpy(speed_message, "Minimum speed");
    }
    if (keys & 0x4u)
    {
        if (enabled_types)
        {
            if (shape_count < MAX_SHAPES_PER_TYPE)
                ++shape_count;
        }
        else if (object_count < NUM_XS_MAX)
            ++object_count;
    }
    if (keys & 0x8u)
    {
        if (enabled_types)
        {
            if (shape_count > 1)
                --shape_count;
        }
        else if (object_count > 1)
            --object_count;
    }
    return 0;
}

static int active_type_count(unsigned int mask)
{
    return (int)((mask & 1u) != 0) + (int)((mask & 2u) != 0) +
           (int)((mask & 4u) != 0);
}

static void render_frame(void)
{
    int i, type;
    int hide_outline = (switch_state & SW_HIDE) != 0;
    clear_screen();

    if (enabled_types)
    {
        // Draw all outlines first, then all X markers above the outlines
        for (type = 0; type < SHAPE_TYPES; ++type)
        {
            if (!(enabled_types & (1u << type)))
                continue;
            for (i = 0; i < shape_count; ++i)
                draw_shape(&shapes[type][i], type, hide_outline);
        }
    }
    else
    {
        if (switch_state == 0)
        {
            for (i = 0; i < object_count - 1; ++i)
                draw_line(objects[i].x, objects[i].y, objects[i + 1].x, objects[i + 1].y, objects[i].color);
        }
        for (i = 0; i < object_count; ++i)
            plot_pixel(objects[i].x, objects[i].y, objects[i].color, 'X');
    }

    ++frames_rendered;
    if (enabled_types)
    {
        printf("\033[%dm\033[1;1HFrames: %lu  Shapes: %d (%d/type)  Types:%s%s%s  SW9:%s",
               WHITE, frames_rendered,
               shape_count * active_type_count(enabled_types), shape_count,
               (enabled_types & 1u) ? " Rect" : "",
               (enabled_types & 2u) ? " Tri" : "",
               (enabled_types & 4u) ? " Square" : "",
               hide_outline ? "HIDE" : "SHOW");
    }
    else
    {
        printf("\033[%dm\033[1;1HFrames: %lu  Objects: %d  Mode: Part 5  Lines: %s",
               WHITE, frames_rendered, object_count,
               switch_state == 0 ? "ON" : "OFF");
    }
    printf("\033[%dm\033[%d;48H%-14s %3ld ms/frame",
           CYAN, SCREEN_Y, speed_message, delay_ns / 1000000L);

    // Keep the old state alive while switching between display modes
    for (type = 0; type < SHAPE_TYPES; ++type)
    {
        for (i = 0; i < shape_count; ++i)
            update_position(&shapes[type][i]);
    }
    for (i = 0; i < object_count; ++i)
        update_position(&objects[i]);
}

int main(void)
{
    int key_fd, sw_fd;
    struct timespec animation_time;
    signal(SIGINT, catchSIGINT);

    key_fd = open("/dev/KEY", O_RDONLY);
    if (key_fd < 0)
    {
        fprintf(stderr, "Cannot open /dev/KEY: %s\n", strerror(errno));
        return 1;
    }
    sw_fd = open("/dev/SW", O_RDONLY);
    if (sw_fd < 0)
    {
        fprintf(stderr, "Cannot open /dev/SW: %s\n", strerror(errno));
        close(key_fd);
        return 1;
    }

    srand((unsigned int)time(NULL));
    initialize_everything();
    printf("\033[2J\033[?25l\033[?7l");
    fflush(stdout);

    while (!stop)
    {
        if (read_controls(key_fd, sw_fd) < 0)
        {
            if (!stop)
                fprintf(stderr, "\nError reading KEY/SW driver\n");
            break;
        }
        render_frame();
        fflush(stdout);
        animation_time.tv_sec = 0;
        animation_time.tv_nsec = delay_ns;
        nanosleep(&animation_time, NULL);
    }

    clear_screen();
    printf("\033[%dm\033[1;1H\033[?7h\033[?25h", WHITE);
    fflush(stdout);
    close(key_fd);
    close(sw_fd);
    return 0;
}
