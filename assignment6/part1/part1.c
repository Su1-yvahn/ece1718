#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include "../include/defines.h"

#define video_BYTES 8               // number of characters to read from /dev/video

int screen_x, screen_y;

/* This code uses the following character device driver: /dev/video. It fills the screen
 * with one color, and then exits */
int main(int argc, char *argv[]){

    int video_FD;                   // file descriptor
    char video_buffer[video_BYTES]; // buffer for video char data
    char command[256];              // buffer for commands to send to /dev/video
    int x, y;
    
    // Open the character device driver
    if ((video_FD = open("/dev/video", O_RDWR)) == -1) {
        printf("Error opening /dev/video: %s\n", strerror(errno));
        return -1;
    }
    
    while (read (video_FD, video_buffer, video_BYTES))
        ;    // read the video port until EOF
    sscanf (video_buffer, "%3d %3d", &screen_x, &screen_y);

    for (x = 0; x < screen_x; ++x)
        for (y = 0; y < screen_y; ++y) {
            sprintf (command, "pixel %d,%d %X\n", x, y, 0x041F);
            write (video_FD, command, strlen(command));
        }
    close (video_FD);
    printf ("\nExiting sample solution program\n");
    return 0;
}
