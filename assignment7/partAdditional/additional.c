#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include "ADXL345.h"
#include "accel_wrappers.h"

/* Additional Part: tilt the board to move 'o' onto a target 'X'.
 * Based on Part 4; a single tap resets the score and target. */
#define SCREEN_X 80
#define SCREEN_Y 24
#define MIN_X 2
#define MAX_X (SCREEN_X - 1)
#define MIN_Y 4
#define MAX_Y (SCREEN_Y - 1)
#define DELAY_NS 30000000L
#define DEAD_ZONE 90
#define X_MG_PER_COL 25
#define Y_MG_PER_ROW 100
#define FILTER_ALPHA 0.65

#define HIT_X 2 /* target can be within two columns */
#define HIT_Y 1 /* and within one row */

volatile sig_atomic_t stop = 0;

void catchSIGINT(int signum);
void clear_screen(void);
void plot_pixel(int x, int y, int color, char c);
int clamp(int value, int low, int high);
void new_target(int bubble_x, int bubble_y, int *target_x, int *target_y);
void draw(int x, int y, int z, int score, int target_x, int target_y,
          int *bubble_x, int *bubble_y);

void catchSIGINT(int signum)
{
    (void)signum;
    stop = 1;
}

void clear_screen(void)
{
    printf("\033[2J");
}

void plot_pixel(int x, int y, int color, char c)
{
    printf("\033[%dm\033[%d;%dH%c", color, y, x, c);
}

int clamp(int value, int low, int high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

/* Keep targets inside the comfortable static-tilt range.
 * Requiring some distance from the bubble prevents free points. */
void new_target(int bubble_x, int bubble_y, int *target_x, int *target_y)
{
    int x, y;
    do
    {
        x = 20 + rand() % 41; /* columns 20..60 */
        y = 7 + rand() % 13;  /* rows 7..19 */
    } while (abs(x - bubble_x) <= HIT_X + 3 &&
             abs(y - bubble_y) <= HIT_Y + 2);

    *target_x = x;
    *target_y = y;
}

/* Same filtered movement logic and ASCII drawing approach as Part 4. */
void draw(int x, int y, int z, int score, int target_x, int target_y,
          int *bubble_x, int *bubble_y)
{
    static int old_x = -1, old_y = -1;
    static double av_x = 0, av_y = 0;
    int dx, dy;
    int center_x = SCREEN_X / 2;
    int center_y = (MIN_Y + MAX_Y) / 2;

    av_x = av_x * FILTER_ALPHA + x * (1.0 - FILTER_ALPHA);
    av_y = av_y * FILTER_ALPHA + y * (1.0 - FILTER_ALPHA);

    dx = ((int)av_x) / X_MG_PER_COL;
    dy = ((int)av_y) / Y_MG_PER_ROW;
    if (av_x > -DEAD_ZONE && av_x < DEAD_ZONE)
        dx = 0;
    if (av_y > -DEAD_ZONE && av_y < DEAD_ZONE)
        dy = 0;

    *bubble_x = clamp(center_x + dx, MIN_X, MAX_X);
    *bubble_y = clamp(center_y - dy, MIN_Y, MAX_Y);

    if (old_x != -1)
        plot_pixel(old_x, old_y, 37, ' ');

    printf("\033[33m\033[1;1HX = %6d mg, Y = %6d mg, Z = %6d mg       ",
           x, y, z);
    printf("\033[36m\033[2;1HScore: %-5d  Tilt to reach X | Single tap: reset score | Ctrl-C: exit   ",
           score);
    plot_pixel(target_x, target_y, 31, 'X');
    plot_pixel(*bubble_x, *bubble_y, 32, 'o');

    old_x = *bubble_x;
    old_y = *bubble_y;
    fflush(stdout);
}

int main(void)
{
    int ready, tap, dtap, x, y, z, mg_per_lsb;
    int accel_x = 0, accel_y = 0, accel_z = 0;
    int bubble_x = SCREEN_X / 2;
    int bubble_y = (MIN_Y + MAX_Y) / 2;
    int target_x, target_y;
    int score = 0;
    int redraw = 1;
    int exit_status = 0;
    struct timespec animation_time;

    signal(SIGINT, catchSIGINT);
    if (!accel_open())
        return 1;

    accel_init();
    accel_format(0, 16);
    accel_rate(12.5f);
    srand((unsigned int)time(NULL));
    new_target(bubble_x, bubble_y, &target_x, &target_y);

    animation_time.tv_sec = 0;
    animation_time.tv_nsec = DELAY_NS;
    clear_screen();
    printf("\033[?25l\033[?7l");
    fflush(stdout);

    while (!stop)
    {
        if (!accel_read_with_taps(&ready, &tap, &dtap,
                                  &x, &y, &z, &mg_per_lsb))
        {
            exit_status = 1;
            break;
        }

        /* Reset only on a single tap, not on a simultaneous double-tap flag. */
        if (tap && !dtap)
        {
            score = 0;
            /* Erase the previous target before selecting another one. */
            plot_pixel(target_x, target_y, 37, ' ');
            new_target(bubble_x, bubble_y, &target_x, &target_y);
            redraw = 1;
        }

        if (ready)
        {
            accel_x = x * mg_per_lsb;
            accel_y = y * mg_per_lsb;
            accel_z = z * mg_per_lsb;
            redraw = 1;
        }

        if (redraw)
        {
            draw(accel_x, accel_y, accel_z, score, target_x, target_y,
                 &bubble_x, &bubble_y);

            if (abs(bubble_x - target_x) <= HIT_X &&
                abs(bubble_y - target_y) <= HIT_Y)
            {
                score++;
                /* Clear the collected target before displaying the next X. */
                plot_pixel(target_x, target_y, 37, ' ');
                new_target(bubble_x, bubble_y, &target_x, &target_y);
                draw(accel_x, accel_y, accel_z, score, target_x, target_y,
                     &bubble_x, &bubble_y);
            }
            redraw = 0;
        }
        nanosleep(&animation_time, NULL);
    }

    printf("\033[0m\033[?7h\033[?25h\033[%d;1H\n", SCREEN_Y);
    fflush(stdout);
    accel_close();
    return exit_status;
}
