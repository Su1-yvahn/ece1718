/* This program implements a basic animation. */
#include <stdio.h>
#include "defines.h"

/* This program implements a line drawing algorithm */
int min_x, min_y, max_x, max_y;

void get_screen_specs (void);
void wait_for_vsync(void);

void clear_screen(void);
void plot_pixel(int, int, char, char);
void draw_line(int, int, int, int, char);

int main(void)
{
    get_screen_specs ();
    clear_screen( );
    printf ("\e[?25l");   // hides the cursor
    fflush (stdout);
    draw_line(min_x, min_y, max_x, min_y, WHITE);
    draw_line(min_x, max_y, min_x, min_y, YELLOW);
    draw_line(min_x, max_y, max_x >> 2, min_y, CYAN);
    draw_line(min_x, max_y, max_x >> 1, min_y, MAGENTA);
    draw_line(min_x, max_y, max_x, min_y, GREEN);
    draw_line(min_x, max_y, max_x, max_y >> 2, BLUE);
    draw_line(min_x, max_y, max_x, max_y >> 1, WHITE);
    draw_line(min_x, max_y, max_x, max_y, WHITE);

    printf ("\e[%d;%dH", 1, 1);            // move cursor to upper left
    printf ("\n\n\nsample solution\n");

    (void) getchar ();                     // wait for user to press return
    clear_screen( );
    printf ("\e[%d;%dH", 1, 1);            // move cursor to upper left
    printf ("\e[?25h");                    // shows the cursor
    fflush (stdout);
    return (0);
}

/* Function to find the screen specifications */
void get_screen_specs( ) {
    min_x = min_y = 1;
    max_x = SCREEN_X;
    max_y = SCREEN_Y;
}

/* Function to blank the screen */
void clear_screen( ) {
    printf ("\e[2J"); // clears the screen
    fflush (stdout);
}

/* Bresenham's line-drawing algorithm */
/* This function draws a line between points (x0, y0) and (x1, y1). The function assumes
 * that the coordinates are valid */
void draw_line(int x0, int y0, int x1, int y1, char color) {
    register int x_0 = x0;
    register int y_0 = y0;
    register int x_1 = x1;
    register int y_1 = y1;
    register char steep = (ABS(y_1 - y_0) > ABS(x_1 - x_0)) ? 1 : 0;
    register int deltax, deltay, tmp, error, ystep, x, y;

    /* Preprocessing inputs */
    if (steep > 0) {
        // Swap x_0 and y_0
        tmp = x_0;
        x_0 = y_0;
        y_0 = tmp;
        // Swap x_1 and y_1
        tmp = x_1;
        x_1 = y_1;
        y_1 = tmp;
    }
    if (x_0 > x_1) {
        // Swap x_0 and x_1
        tmp = x_0;
        x_0 = x_1;
        x_1 = tmp;
        // Swap y_0 and y_1
        tmp = y_0;
        y_0 = y_1;
        y_1 = tmp;
    }

    /* Setup local variables */
    deltax = x_1 - x_0;
    deltay = ABS(y_1 - y_0);
    error = -(deltax / 2);
    y = y_0;
    if (y_0 < y_1)
        ystep = 1;
   else
        ystep = -1;

    /* Draw a line - either go along the x-axis (steep = 0) or along the y-axis (steep = 1) */
    for (x = x_0; x <= x_1; x++) {
        if (steep == 1)
            plot_pixel (y, x, color, '|');
        else
            plot_pixel (x, y, color, '*');
        error = error + deltay;
        if (error > 0) {
            y = y + ystep;
            error = error - deltax;
        }
    }
}

void plot_pixel(int x, int y, char color, char c) {
    printf ("\e[%2dm\e[%d;%dH%c", color, y, x, c);
    fflush (stdout);
}

