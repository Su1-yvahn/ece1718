
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>

#define VIDEO_BYTES 8
#define BLACK 0x0000
#define WHITE 0xFFFF

int screen_x, screen_y;
volatile sig_atomic_t stop = 0;

void handle_sigint(int signum)
{
    (void)signum;
    stop = 1;
}

int send_command(int video_fd, const char *command)
{
    size_t length = strlen(command);

    if (write(video_fd, command, length) != (ssize_t)length) {
        perror("Error writing to /dev/video");
        return -1;
    }

    return 0;
}

int draw_horizontal_line(int video_fd, int y, int color)
{
    char command[64];

    // The line occupies half of the screen width
    int line_length = screen_x / 2;

    // Center the line horizontally
    int x_start = (screen_x - line_length) / 2;
    int x_end = x_start + line_length - 1;

    snprintf(command, sizeof(command),
             "line %d,%d %d,%d %X\n",
             x_start, y, x_end, y, color);

    return send_command(video_fd, command);
}

int main(int argc, char *argv[])
{
    int video_fd;
    char video_buffer[VIDEO_BYTES + 1];
    ssize_t bytes_read;

    int y = 0;
    int direction = 1;

    (void)argc;
    (void)argv;

    signal(SIGINT, handle_sigint);

    // Open the video character device
    video_fd = open("/dev/video", O_RDWR);

    if (video_fd == -1) {
        printf("Error opening /dev/video: %s\n",
               strerror(errno));
        return -1;
    }

    // Read screen resolution from the driver
    bytes_read = read(video_fd, video_buffer, VIDEO_BYTES);

    if (bytes_read <= 0) {
        printf("Error reading screen resolution\n");
        close(video_fd);
        return -1;
    }

    video_buffer[bytes_read] = '\0';

    if (sscanf(video_buffer, "%d %d",
               &screen_x, &screen_y) != 2 ||
        screen_x < 2 || screen_y < 2) {
        printf("Invalid screen resolution\n");
        close(video_fd);
        return -1;
    }

    printf("Screen resolution: %d x %d\n",
           screen_x, screen_y);

    // Clear the screen
    if (send_command(video_fd, "clear") != 0) {
        close(video_fd);
        return -1;
    }

    // Draw the first horizontal line
    if (draw_horizontal_line(video_fd, y, WHITE) != 0) {
        close(video_fd);
        return -1;
    }

    // Animation loop
    while (!stop) {

        // Wait for VGA vertical synchronization
        if (send_command(video_fd, "sync") != 0)
            break;

        if (stop)
            break;
        usleep(50000);
        // Erase the previous line
        if (draw_horizontal_line(video_fd, y, BLACK) != 0)
            break;
        
        // Reverse direction at screen edges
        if (y == screen_y - 1)
            direction = -1;
        else if (y == 0)
            direction = 1;

        // Move one row
        y += direction;

        // Draw the line at its new position
        if (draw_horizontal_line(video_fd, y, WHITE) != 0)
            break;
    }

    // Clear the screen before exiting
    send_command(video_fd, "clear");

    close(video_fd);

    printf("\nExiting Part III animation\n");

    return 0;
}
