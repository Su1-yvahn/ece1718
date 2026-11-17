
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include "../include/defines.h"

#define NUM_OBJECTS 8

int min_x, min_y, max_x, max_y;

volatile sig_atomic_t stop = 0;

typedef struct {
    int x;
    int y;
    int dx;
    int dy;
} OBJECT;

OBJECT objects[NUM_OBJECTS];

void get_screen_specs(void);
void clear_screen(void);
void plot_pixel(int, int, char, char);
void draw_line(int, int, int, int, char);
void initialize_objects(void);
void draw_animation(void);
void handle_sigint(int);

int main(void)
{
    struct timespec delay = {
        .tv_sec = 0,
        .tv_nsec = 100000000
    };

    signal(SIGINT, handle_sigint);

    get_screen_specs();

    if (max_x < 2 || max_y < 2) {
        fprintf(stderr, "Screen is too small.\n");
        return 1;
    }

    srand((unsigned int)time(NULL));

    initialize_objects();

    clear_screen();
    printf("\033[?25l");  // Hide cursor
    fflush(stdout);

    while (!stop) {
        draw_animation();
        nanosleep(&delay, NULL);
    }

    // Restore terminal
    printf("\033[0m");
    clear_screen();
    printf("\033[1;1H");
    printf("\033[?25h");
    fflush(stdout);

    return 0;
}

void handle_sigint(int sig)
{
    (void)sig;
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
    printf("\033[2J\033[H");
}

void plot_pixel(int x, int y, char color, char c)
{
    if (x < min_x || x > max_x ||
        y < min_y || y > max_y)
        return;

    printf("\033[%dm\033[%d;%dH%c", color, y, x, c);
}

/* Bresenham's line drawing algorithm */
void draw_line(int x0, int y0, int x1, int y1, char color)
{
    int x_0 = x0;
    int y_0 = y0;
    int x_1 = x1;
    int y_1 = y1;

    int steep;
    int deltax, deltay;
    int tmp, error, ystep;
    int x, y;

    steep = (ABS(y_1 - y_0) > ABS(x_1 - x_0)) ? 1 : 0;

    if (steep) {
        tmp = x_0;
        x_0 = y_0;
        y_0 = tmp;

        tmp = x_1;
        x_1 = y_1;
        y_1 = tmp;
    }

    if (x_0 > x_1) {
        tmp = x_0;
        x_0 = x_1;
        x_1 = tmp;

        tmp = y_0;
        y_0 = y_1;
        y_1 = tmp;
    }

    deltax = x_1 - x_0;
    deltay = ABS(y_1 - y_0);
    error = -(deltax / 2);
    y = y_0;

    if (y_0 < y_1)
        ystep = 1;
    else
        ystep = -1;

    for (x = x_0; x <= x_1; x++) {

        if (steep)
            plot_pixel(y, x, color, '*');
        else
            plot_pixel(x, y, color, '*');

        error += deltay;

        if (error > 0) {
            y += ystep;
            error -= deltax;
        }
    }
}

/* Initialize random positions and directions */
void initialize_objects(void)
{
    int i;

    for (i = 0; i < NUM_OBJECTS; i++) {

        objects[i].x =
            min_x + rand() % (max_x - min_x + 1);

        objects[i].y =
            min_y + rand() % (max_y - min_y + 1);

        objects[i].dx = (rand() % 2) ? 1 : -1;
        objects[i].dy = (rand() % 2) ? 1 : -1;
    }
}

/* Draw one animation frame */
void draw_animation(void)
{
    int i;
    int next;

    clear_screen();

    // Connect all objects, including last to first
    for (i = 0; i < NUM_OBJECTS; i++) {

        next = (i + 1) % NUM_OBJECTS;

        draw_line(
            objects[i].x,
            objects[i].y,
            objects[next].x,
            objects[next].y,
            CYAN
        );
    }

    // Draw objects on top of connecting lines
    for (i = 0; i < NUM_OBJECTS; i++) {

        plot_pixel(
            objects[i].x,
            objects[i].y,
            YELLOW,
            'X'
        );
    }

    fflush(stdout);

    // Update positions and handle bouncing
    for (i = 0; i < NUM_OBJECTS; i++) {

        // Bounce off left and right boundaries
        if (objects[i].x + objects[i].dx > max_x ||
            objects[i].x + objects[i].dx < min_x) {

            objects[i].dx = -objects[i].dx;
        }

        // Bounce off top and bottom boundaries
        if (objects[i].y + objects[i].dy > max_y ||
            objects[i].y + objects[i].dy < min_y) {

            objects[i].dy = -objects[i].dy;
        }

        // Move one column and one row
        objects[i].x += objects[i].dx;
        objects[i].y += objects[i].dy;
    }
}
