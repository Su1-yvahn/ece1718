#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h> // provides nanosleep()
#include "ADXL345.h"
#include "accel_wrappers.h"

// ASCII terminal display
#define SCREEN_X 80
#define SCREEN_Y 24
#define MIN_X 2 // Horizontal bubble boundaries
#define MAX_X (SCREEN_X - 1)
#define MIN_Y 3 // Vertical bubble boundaries
#define MAX_Y (SCREEN_Y - 1)
#define DELAY_NS 30000000L // 30 ms polling interval
#define DEAD_ZONE 90       // leave bubble centered for small idle offsets (mg)
#define X_MG_PER_COL 25    // sensitivity of horizontal movement
#define Y_MG_PER_ROW 100   // sensitivity of vertical movement
#define FILTER_ALPHA 0.65  // running average reduces jitter

volatile sig_atomic_t stop = 0;

void catchSIGINT(int signum);
void clear_screen(void);
void plot_pixel(int x, int y, char color, char c);
int clamp(int value, int min, int max);
void draw(int accel_x, int accel_y, int accel_z);

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

void draw(int accel_x, int accel_y, int accel_z)
{
    // static local variables retain their values between function calls
    static int old_x = -1, old_y = -1; // Remembering where the bubble was drawn during the previous frame
    static double av_x = 0, av_y = 0;  // Remembering filtered acceleration values
    int x, y, dx, dy;
    // Initial center: (40, 13)
    int center_x = SCREEN_X / 2;
    int center_y = (MIN_Y + MAX_Y) / 2;

    // Filtering the acceleration measurements
    av_x = av_x * FILTER_ALPHA + accel_x * (1.0 - FILTER_ALPHA);
    av_y = av_y * FILTER_ALPHA + accel_y * (1.0 - FILTER_ALPHA);

    // Converting acceleration into bubble movement
    dx = ((int)av_x) / X_MG_PER_COL;
    dy = ((int)av_y) / Y_MG_PER_ROW;

    // further reduce jitter: small sensor offsets, electrical noise, or calibration errors, which prevents the bubble from visibly reacting to every small fluctuation
    // small readings between -90 mg and +90 mg produce zero displacement along that axis
    if (av_x > -DEAD_ZONE && av_x < DEAD_ZONE)
        dx = 0;
    if (av_y > -DEAD_ZONE && av_y < DEAD_ZONE)
        dy = 0;

    // Calculating the final position
    x = clamp(center_x + dx, MIN_X, MAX_X); // x = 40 + dx
    y = clamp(center_y - dy, MIN_Y, MAX_Y); // y = 13 - dy: positive Y acceleration to move the bubble upward

    if (old_x != -1)
        plot_pixel(old_x, old_y, 37, ' '); // erase previous bubble

    printf("\033[33m\033[1;1HX = %6d mg, Y = %6d mg, Z = %6d mg     ", accel_x, accel_y, accel_z); // yellow text
    plot_pixel(x, y, 32, 'o');                                                                     // green bubble
    old_x = x;
    old_y = y;
    fflush(stdout); // forces buffered terminal output to be sent immediately
}

int main(void)
{
    int ready, x, y, z, mg_per_lsb;
    struct timespec animation_time;
    int exit_status = 0;

    signal(SIGINT, catchSIGINT);
    if (!accel_open())
        return 1;

    // Start with the same 10-bit, +/-16 g measurement range, 12.5 Hz settings
    accel_init();
    accel_format(0, 16);
    accel_rate(12.5f);

    animation_time.tv_sec = 0;
    animation_time.tv_nsec = DELAY_NS; // 30 ms sleep interval
    clear_screen();
    printf("\033[?25l\033[?7l"); // hide cursor, disable auto-wrap
    fflush(stdout);

    while (!stop)
    {
        if (!accel_read(&ready, &x, &y, &z, &mg_per_lsb))
        {
            exit_status = 1;
            break;
        }
        if (ready)
            draw(x * mg_per_lsb, y * mg_per_lsb, z * mg_per_lsb); // update bubble
        nanosleep(&animation_time, NULL);                         // sleep for 30 ms
    }

    printf("\033[0m\033[?7h\033[?25h\033[%d;1H\n", SCREEN_Y);
    fflush(stdout);
    accel_close();
    return exit_status;
}
