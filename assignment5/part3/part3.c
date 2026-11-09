#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include "defines.h"

volatile sig_atomic_t stop = 0;

int min_x, min_y, max_x, max_y;

void catchSIGINT(int signum);
void get_screen_specs(void);
void clear_screen(void);
void plot_pixel(int x, int y, char color, char c);
void draw_line(int x0, int y0, int x1, int y1, char color);

int main(void)
{
    int line_y, direction;
    int line_x0, line_x1;
    struct timespec animation_time;

    // Capture Ctrl+C to stop the animation and restore the terminal
    signal(SIGINT, catchSIGINT);

    get_screen_specs();
    line_x0 = min_x + 20;
    line_x1 = max_x - 20;
    line_y = (min_y + max_y) / 2;
    direction = -1; // Initially move upward

    // 1 sec = 1000000000ns, so that 0.1 sec = 100000000ns
    animation_time.tv_sec = 0;           // whole second is 0
    animation_time.tv_nsec = 100000000L; // 0.1 seconds per row

    // Before drawing
    clear_screen();
    printf("\033[?25l"); // Hide cursor during animation
    fflush(stdout);

    // During drawing: forever loop
    while (!stop)
    {
        // Draw the current frame
        clear_screen();                                    // clear a whole screen before drawing a new line
        draw_line(line_x0, line_y, line_x1, line_y, CYAN); // draw a single horizontal line starting from the middle of the screen
        fflush(stdout);

        // 0.1 second Delay before moving to the next row
        nanosleep(&animation_time, NULL);
        if (stop)
            break;

        // Bounce at the top and bottom screen boundaries
        if (line_y == min_y)
            direction = 1;
        else if (line_y == max_y)
            direction = -1;

        line_y += direction;
    }

    // After finishing drawing
    clear_screen();
    printf("\033[%dm\033[1;1H\033[?25h", WHITE);
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

/* Bresenham's line-drawing algorithm */
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
