#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include "ADXL345.h"
#include "accel_wrappers.h"

#define SCREEN_X 80
#define SCREEN_Y 24
#define MIN_X 2
#define MAX_X (SCREEN_X - 1)
#define MIN_Y 3
#define MAX_Y (SCREEN_Y - 1)
#define DELAY_NS 30000000L // 30 ms between polls
#define DEAD_ZONE 90       // leave bubble centered for small idle offsets (mg)
#define X_MG_PER_COL 25    // sensitivity of horizontal movement
#define Y_MG_PER_ROW 100   // sensitivity of vertical movement
#define FILTER_ALPHA 0.65
#define SINGLE_TAP_WAIT_MS 350 // wait for the 350 ms double-tap window & running average reduces jitter

volatile sig_atomic_t stop = 0;

void catchSIGINT(int signum);
void clear_screen(void);
void plot_pixel(int x, int y, char color, char c);
int clamp(int value, int min, int max);
void draw(int accel_x, int accel_y, int accel_z, int bubble_color, char bubble_char);
long long get_time_ms(void);

void catchSIGINT(int signum)
{
    (void)signum;
    stop = 1;
}

void clear_screen(void)
{
    printf("\033[2J");
}

void plot_pixel(int x, int y, char color, char c)
{
    printf("\033[%dm\033[%d;%dH%c", color, y, x, c);
}

int clamp(int value, int min, int max)
{
    if (value < min)
        return min;
    if (value > max)
        return max;
    return value;
}

void draw(int accel_x, int accel_y, int accel_z, int bubble_color, char bubble_char)
{
    static int old_x = -1, old_y = -1;
    static double av_x = 0, av_y = 0;
    int x, y, dx, dy;
    int center_x = SCREEN_X / 2;
    int center_y = (MIN_Y + MAX_Y) / 2;

    av_x = av_x * FILTER_ALPHA + accel_x * (1.0 - FILTER_ALPHA);
    av_y = av_y * FILTER_ALPHA + accel_y * (1.0 - FILTER_ALPHA);

    dx = ((int)av_x) / X_MG_PER_COL;
    dy = ((int)av_y) / Y_MG_PER_ROW;
    if (av_x > -DEAD_ZONE && av_x < DEAD_ZONE)
        dx = 0;
    if (av_y > -DEAD_ZONE && av_y < DEAD_ZONE)
        dy = 0;

    // Positive X points right, positive Y points up on the terminal
    x = clamp(center_x + dx, MIN_X, MAX_X);
    y = clamp(center_y - dy, MIN_Y, MAX_Y);

    if (old_x != -1)
        plot_pixel(old_x, old_y, 37, ' '); // erase previous bubble

    printf("\033[33m\033[1;1HX = %6d mg, Y = %6d mg, Z = %6d mg     ", accel_x, accel_y, accel_z);
    plot_pixel(x, y, bubble_color, bubble_char);
    old_x = x;
    old_y = y;
    fflush(stdout);
}

long long get_time_ms(void)
{
    struct timespec time_now;
    clock_gettime(CLOCK_MONOTONIC, &time_now);
    return (long long)time_now.tv_sec * 1000 + time_now.tv_nsec / 1000000;
}

int main(void)
{
    int ready, tap, dtap, x, y, z, mg_per_lsb;
    int accel_x = 0, accel_y = 0, accel_z = 0;
    int bubble_color = 32;  // 32: green, 31: red
    char bubble_char = 'o'; // o: small bubble, O: large bubble
    int pending_single_tap = 0;
    int redraw = 1;
    long long tap_time = 0;
    struct timespec animation_time;
    int exit_status = 0;

    signal(SIGINT, catchSIGINT);
    if (!accel_open())
        return 1;

    // Same initial accelerometer settings as before
    accel_init();
    accel_format(0, 16);
    accel_rate(12.5f);

    animation_time.tv_sec = 0;
    animation_time.tv_nsec = DELAY_NS;
    clear_screen();
    printf("\033[?25l\033[?7l"); // hide cursor, disable auto-wrap
    fflush(stdout);

    while (!stop)
    {
        if (!accel_read_with_taps(&ready, &tap, &dtap, &x, &y, &z, &mg_per_lsb))
        {
            exit_status = 1;
            break;
        }

        // A double tap toggle only o/O, not the bubble colour
        if (dtap)
        {
            bubble_char = (bubble_char == 'o') ? 'O' : 'o'; // ternary operator
            pending_single_tap = 0;
            redraw = 1;
        }
        else if (tap)
        {
            pending_single_tap = 1;
            tap_time = get_time_ms();
        }

        if (pending_single_tap && get_time_ms() - tap_time >= SINGLE_TAP_WAIT_MS) // After 350 ms, if the pending action hasn't been cancelled by a double tap, we change the colour
        {
            bubble_color = (bubble_color == 32) ? 31 : 32;
            pending_single_tap = 0;
            redraw = 1;
        }

        if (ready)
        {
            accel_x = x * mg_per_lsb;
            accel_y = y * mg_per_lsb;
            accel_z = z * mg_per_lsb;
            redraw = 1;
        }

        // Update the bubble on a tap even if no fresh XYZ data arrived
        if (redraw)
        {
            draw(accel_x, accel_y, accel_z, bubble_color, bubble_char);
            redraw = 0;
        }
        nanosleep(&animation_time, NULL);
    }

    printf("\033[0m\033[?7h\033[?25h\033[%d;1H\n", SCREEN_Y);
    fflush(stdout);
    accel_close();
    return exit_status;
}
