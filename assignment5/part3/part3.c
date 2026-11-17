
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include "../include/defines.h"

int min_x, min_y, max_x, max_y;
volatile sig_atomic_t stop = 0;

void get_screen_specs(void);
void clear_screen(void);
void draw_line(int);
void handle_sigint(int);

int main(void)
{
    int y, direction;

    struct timespec delay = {
        .tv_sec = 0,
        .tv_nsec = 100000000
    };

    signal(SIGINT, handle_sigint);

    get_screen_specs();
    clear_screen();

    printf("\033[?25l");  
    fflush(stdout);

    y = min_y;
    direction = 1;

    // Draw initial horizontal line
    draw_line(y);

    while (!stop) {

        // Animation delay
        nanosleep(&delay, NULL);

        // Erase previous line
        clear_screen();

        // Bounce at top and bottom
        if (y >= max_y) {
            direction = -1;
        }
        else if (y <= min_y) {
            direction = 1;
        }

        // Move one row
        y += direction;

        // Draw green horizontal line
        draw_line(y);
    }

    // Restore terminal
    clear_screen();
    printf("\033[0m");
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
    printf("\033[2J");
    fflush(stdout);
}

void draw_line(int y)
{
    int x;
    int line_length = SCREEN_X / 2;
    int start_x = (SCREEN_X - line_length) / 2 + 1;

    printf("\033[32m");  // Green

    for (x = start_x; x < start_x + line_length; x++) {
        printf("\033[%d;%dH*", y, x);
    }

    fflush(stdout);
}
