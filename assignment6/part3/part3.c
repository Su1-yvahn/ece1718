#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include "../include/defines.h"

#define video_BYTES 8

volatile sig_atomic_t stop = 0;
int screen_x, screen_y;

void catchSIGINT(int signum);
void draw_line(int video_FD, int x0, int y0, int x1, int y1, int color);

int main(void)
{
    int video_FD;
    char video_buffer[video_BYTES + 1];
    int line_y, direction;

    signal(SIGINT, catchSIGINT);

    // Open /dev/video driver
    if ((video_FD = open("/dev/video", O_RDWR)) == -1)
    {
        printf("Error opening /dev/video: %s\n", strerror(errno));
        return -1;
    }

    // Read the display resolution from the video driver
    memset(video_buffer, 0, sizeof(video_buffer));
    if (read(video_FD, video_buffer, video_BYTES) <= 0 || sscanf(video_buffer, "%d %d", &screen_x, &screen_y) != 2 || screen_x <= 0 || screen_y <= 0)
    {
        printf("Error reading video screen dimensions\n");
        close(video_FD);
        return -1;
    }

    // Before drawing: clear the screen and draw one horizontal line
    write(video_FD, "clear", strlen("clear"));
    line_y = screen_y / 2;
    direction = -1; // Initially move upward
    draw_line(video_FD, 0, line_y, screen_x - 1, line_y, CYAN);

    // During drawing: one-row movement after each VGA synchronization
    while (!stop)
    {
        write(video_FD, "sync", strlen("sync"));
        if (stop)
            break;

        // Erase the previous line by drawing it in black
        draw_line(video_FD, 0, line_y, screen_x - 1, line_y, 0x0000);

        // Reverse direction at either screen edge
        if (line_y == 0)
            direction = 1;
        else if (line_y == screen_y - 1)
            direction = -1;

        line_y += direction;
        draw_line(video_FD, 0, line_y, screen_x - 1, line_y, CYAN);
    }

    // After finishing drawing
    write(video_FD, "clear", strlen("clear"));
    close(video_FD);
    return 0;
}

void catchSIGINT(int signum)
{
    (void)signum;
    stop = 1;
}

void draw_line(int video_FD, int x0, int y0, int x1, int y1, int color)
{
    char command[64];
    sprintf(command, "line %d,%d %d,%d %X\n", x0, y0, x1, y1, color);
    write(video_FD, command, strlen(command));
}
