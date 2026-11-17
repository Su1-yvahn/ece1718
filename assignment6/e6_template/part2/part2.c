#include <stdio.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include "../include/defines.h"

#define video_BYTES 8   // number of characters to read from /dev/video

int screen_x, screen_y;

/* This code uses the following character device driver: /dev/video. It draws several lines 
 * on the VGA display, and then exits. */
int main(int argc, char *argv[])
{
    int video_FD;                       // file descriptor
    char video_buffer[video_BYTES];  // buffer for video char data
    char command[64];
    
    // Open the character device driver
    if ((video_FD = open("/dev/video", O_RDWR)) == -1) {
        printf("Error opening /dev/video: %s\n", strerror(errno));
        return -1;
    }
    
    while (read (video_FD, video_buffer, video_BYTES))
        ;    // read the video port until EOF
    sscanf (video_buffer, "%3d %3d", &screen_x, &screen_y);

    sprintf (command, "line %d,%d %d,%d %X\n", 0, screen_y - 1, screen_x - 1, 0, YELLOW);
    write (video_FD, command, strlen(command));
    sprintf (command, "line %d,%d %d,%d %X\n", 0, screen_y - 1, 
        screen_x - (screen_x >> 2) - 1, 0, ORANGE);
    write (video_FD, command, strlen(command));
    sprintf (command, "line %d,%d %d,%d %X\n", 0, screen_y - 1, 
        (screen_x >> 1) - 1, 0, CYAN);
    write (video_FD, command, strlen(command));
    sprintf (command, "line %d,%d %d,%d %X\n", 0, screen_y - 1, 
        (screen_x >> 2) - 1, 0, GREEN);
    write (video_FD, command, strlen(command));
    sprintf (command, "line %d,%d %d,%d %X\n", 0, screen_y - 1, 
        (screen_x >> 3) - 1, 0, WHITE);
    write (video_FD, command, strlen(command));

    printf ("\nExiting sample solution program\n");
    close (video_FD);
    return 0;
}
