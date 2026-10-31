#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

// Global flag for program termination
static volatile sig_atomic_t program_running = 1;

// SIGINT handler: Ctrl+C
void handle_sigint(int sig)
{
    (void)sig;
    program_running = 0;
}

// Read a device using a new file descriptor
int read_device(const char *path, char *buffer, size_t size)
{
    int fd = open(path, O_RDONLY);

    if (fd < 0) {
        perror(path);
        return -1;
    }

    ssize_t n = read(fd, buffer, size - 1);
    close(fd);

    if (n <= 0) {
        fprintf(stderr, "Failed to read %s\n", path);
        return -1;
    }

    buffer[n] = '\0';
    return 0;
}

// Write a command to a device
int send_command(int fd, const char *command)
{
    size_t length = strlen(command);

    if (write(fd, command, length) != (ssize_t)length) {
        perror("write device");
        return -1;
    }

    return 0;
}

int main(void)
{
    int fd_stopwatch;
    int fd_ledr;

    char key_buffer[16];
    char sw_buffer[16];
    char time_buffer[32];

    unsigned int key_value, sw_value;
    int MM, SS, DD;
    int running = 1;

    // Register SIGINT handler
    struct sigaction sa = {0};
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) < 0) {
        perror("sigaction");
        return 1;
    }

    // Open stopwatch
    fd_stopwatch = open("/dev/stopwatch", O_WRONLY);

    if (fd_stopwatch < 0) {
        perror("open stopwatch");
        return 1;
    }

    // Open LEDR
    fd_ledr = open("/dev/LEDR", O_WRONLY);

    if (fd_ledr < 0) {
        perror("open LEDR");
        close(fd_stopwatch);
        return 1;
    }

    // Enable display and start stopwatch
    if (send_command(fd_stopwatch, "disp") < 0 ||
        send_command(fd_stopwatch, "run") < 0)
        goto cleanup;

    //printf("Part3 running. Press Ctrl+C to exit.\n");

    while (program_running) {

        // Read KEY edge-capture
        if (read_device("/dev/KEY",
                        key_buffer, sizeof(key_buffer)) < 0) {
            if (!program_running)
                break;
            goto cleanup;
        }

        key_value = (unsigned int)strtoul(
            key_buffer, NULL, 16);

        if (key_value == 0) {
            usleep(10000);
            continue;
        }

        // KEY0: toggle run / pause
        if (key_value & 0x1) {

            const char *command =
                running ? "stop" : "run";

            if (send_command(fd_stopwatch, command) < 0)
                goto cleanup;

            running = !running;

            //printf("Stopwatch: %s\n", running ? "RUN" : "PAUSE");
        }

        // KEY1 / KEY2 / KEY3
        if (key_value & 0xE) {

            // Read SW only when KEY1/2/3 pressed
            if (read_device("/dev/SW",
                            sw_buffer, sizeof(sw_buffer)) < 0)
                goto cleanup;

            sw_value = (unsigned int)strtoul(
                sw_buffer, NULL, 16);

            // Update LEDR
            if (send_command(fd_ledr, sw_buffer) < 0)
                goto cleanup;

            // Read stopwatch
            if (read_device("/dev/stopwatch",
                            time_buffer, sizeof(time_buffer)) < 0)
                goto cleanup;

            if (sscanf(time_buffer, "%d:%d:%d",
                       &MM, &SS, &DD) != 3) {
                fprintf(stderr, "Invalid stopwatch time\n");
                goto cleanup;
            }

            // KEY1: set hundredths
            if (key_value & 0x2)
                DD = sw_value > 99 ? 99 : sw_value;

            // KEY2: set seconds
            if (key_value & 0x4)
                SS = sw_value > 59 ? 59 : sw_value;

            // KEY3: set minutes
            if (key_value & 0x8)
                MM = sw_value > 59 ? 59 : sw_value;

            snprintf(time_buffer, sizeof(time_buffer),
                     "%02d:%02d:%02d", MM, SS, DD);

            //printf("Set stopwatch: %s\n", time_buffer);
        }

        usleep(10000);
    }

    // Stop the countdown
    send_command(fd_stopwatch, "stop");

    // Turn off HEX display
    send_command(fd_stopwatch, "nodisp");

    // Turn off all LEDs
    send_command(fd_ledr, "000");

    close(fd_ledr);
    close(fd_stopwatch);

    printf("\nProgram exited. HEX and LEDR cleared.\n");

    return 0;
}