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
#define NUM_XS 8
#define BOX_SIZE 5

volatile sig_atomic_t stop = 0;
int screen_x, screen_y;

// Store the position, direction, and color of each moving rectangle
int object_x[NUM_XS], object_y[NUM_XS];
int delta_x[NUM_XS], delta_y[NUM_XS];
int object_color[NUM_XS];

void catchSIGINT(int signum);
void initialize_objects(void);
void draw(int video_FD);
void draw_line(int video_FD, int x0, int y0, int x1, int y1, int color);
void draw_box(int video_FD, int x0, int y0, int x1, int y1, int color);

int main(void)
{
    int video_FD;
    char video_buffer[video_BYTES + 1];

    signal(SIGINT, catchSIGINT);

    if ((video_FD = open("/dev/video", O_RDWR)) == -1)
    {
        printf("Error opening /dev/video: %s\n", strerror(errno));
        return -1;
    }

    // Read the VGA display dimensions from the driver
    memset(video_buffer, 0, sizeof(video_buffer));
    if (read(video_FD, video_buffer, video_BYTES) <= 0 || sscanf(video_buffer, "%d %d", &screen_x, &screen_y) != 2 || screen_x < BOX_SIZE || screen_y < BOX_SIZE)
    {
        printf("Error reading video screen dimensions\n");
        close(video_FD);
        return -1;
    }

    srand((unsigned int)time(NULL));
    initialize_objects();

    // Draw one complete frame into the back buffer, then display it
    while (!stop)
    {
        draw(video_FD);
        write(video_FD, "sync", strlen("sync"));
    }

    close(video_FD);
    return 0;
}

void catchSIGINT(int signum)
{
    (void)signum;
    stop = 1;
}

void initialize_objects(void)
{
    const int colors[] = {RED, GREEN, YELLOW, BLUE, MAGENTA, CYAN, WHITE, ORANGE};
    int i;

    for (i = 0; i < NUM_XS; ++i)
    {
        // Keep the entire rectangle within the valid VGA coordinates
        object_x[i] = rand() % (screen_x - BOX_SIZE + 1);
        object_y[i] = rand() % (screen_y - BOX_SIZE + 1);
        delta_x[i] = (rand() % 2) ? 1 : -1;
        delta_y[i] = (rand() % 2) ? 1 : -1;
        object_color[i] = colors[rand() % (sizeof(colors) / sizeof(colors[0]))];
    }
}

void draw(int video_FD)
{
    int i;

    // Clear the back buffer, not the currently displayed front buffer
    write(video_FD, "clear", strlen("clear"));

    // Draw lines connecting the centers of neighboring rectangles
    for (i = 0; i < NUM_XS - 1; ++i)
    {
        draw_line(video_FD, object_x[i] + BOX_SIZE / 2, object_y[i] + BOX_SIZE / 2, object_x[i + 1] + BOX_SIZE / 2, object_y[i + 1] + BOX_SIZE / 2, object_color[i]);
    }

    // Draw all eight filled rectangles
    for (i = 0; i < NUM_XS; ++i)
    {
        draw_box(video_FD, object_x[i], object_y[i], object_x[i] + BOX_SIZE - 1, object_y[i] + BOX_SIZE - 1, object_color[i]);
    }

    // Move every rectangle one pixel diagonally and bounce at the screen edges
    for (i = 0; i < NUM_XS; ++i)
    {
        if (object_x[i] + delta_x[i] < 0 || object_x[i] + delta_x[i] > screen_x - BOX_SIZE)
            delta_x[i] = -delta_x[i];

        if (object_y[i] + delta_y[i] < 0 || object_y[i] + delta_y[i] > screen_y - BOX_SIZE)
            delta_y[i] = -delta_y[i];

        object_x[i] += delta_x[i];
        object_y[i] += delta_y[i];
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
