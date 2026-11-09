#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include "defines.h"

volatile sig_atomic_t stop = 0; // used to exit the program cleanly

int min_x, min_y, max_x, max_y;

// Position, diagonal movement direction, and color of each object
int object_x[NUM_XS], object_y[NUM_XS];
int delta_x[NUM_XS], delta_y[NUM_XS];
char object_color[NUM_XS];

void catchSIGINT(int signum); // capture Ctrl+C
void get_screen_specs(void);
void clear_screen(void);
void plot_pixel(int x, int y, char color, char c);
void draw_line(int x0, int y0, int x1, int y1, char color);
void initialize_objects(void);
void draw(void);

int main(void)
{
    struct timespec animation_time; // control the animation timing

    // Capture Ctrl+C to stop the animation and restores the terminal
    signal(SIGINT, catchSIGINT);

    get_screen_specs();

    // generate pseudo-random numbers based on a seed
    srand((unsigned int)time(NULL)); // return the current calendar time as a 'time_t' value
    initialize_objects();

    animation_time.tv_sec = 0;
    animation_time.tv_nsec = 200000000L; // 0.2 seconds per frame

    // Before drawing
    clear_screen();
    printf("\033[?25l\033[?7l"); // Hide cursor and disable auto-wrap
    fflush(stdout);

    while (!stop)
    {
        draw();
        fflush(stdout);
        nanosleep(&animation_time, NULL); // int nanosleep(const struct timespec *req, struct timespec *rem);
    }

    // After finishing drawing, Restore screen, text color, cursor position, and cursor visibility
    clear_screen();
    printf("\033[%dm\033[1;1H\033[?7h\033[?25h", WHITE);
    fflush(stdout);
    return 0;
}

void catchSIGINT(int signum)
{
    (void)signum;
    stop = 1;
}

void get_screen_specs(void)
{
    min_x = min_y = 1;
    max_x = SCREEN_X;
    max_y = SCREEN_Y;
}

void clear_screen(void)
{
    printf("\033[2J");
}

// Set random starting positions, diagonal directions, and colors
void initialize_objects(void)
{
    const char colors[] = {RED, GREEN, YELLOW, BLUE, MAGENTA, CYAN, WHITE};
    int i;

    for (i = 0; i < NUM_XS; ++i)
    {
        object_x[i] = min_x + rand() % (max_x - min_x + 1);                      // starting location of x-coordinate
        object_y[i] = min_y + rand() % (max_y - min_y + 1);                      // starting location of y-coordinate
        delta_x[i] = (rand() % 2) ? 1 : -1;                                      // x direction
        delta_y[i] = (rand() % 2) ? 1 : -1;                                      // y direction (y=-1 mean up)
        object_color[i] = colors[rand() % (sizeof(colors) / sizeof(colors[0]))]; // total size of the array in bytes
    }
}

// Draw each chain link, then its X endpoints; update by one diagonal step
void draw(void)
{
    int i;

    clear_screen();

    for (i = 0; i < NUM_XS - 1; ++i)
    {
        draw_line(object_x[i], object_y[i], object_x[i + 1], object_y[i + 1], object_color[i]); // draw line first
    }

    for (i = 0; i < NUM_XS; ++i)
    {
        plot_pixel(object_x[i], object_y[i], object_color[i], 'X'); // plot character X
    }

    // Each object independently bounces at all four screen boundaries
    for (i = 0; i < NUM_XS; ++i)
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

// Bresenham's integer line-drawing algorithm
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
