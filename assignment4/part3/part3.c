
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#define KEY_BYTES 2
#define SW_BYTES 4
#define STOPWATCH_BYTES 9

volatile sig_atomic_t stop = 0;

void catchSIGINT(int signum)
{
    (void)signum;
    stop = 1;
}

/* This program uses /dev/KEY, /dev/SW, /dev/LEDR,
 * and /dev/stopwatch.
 *
 * KEY0: toggle run/pause
 * KEY1: set DD from SW (max 99)
 * KEY2: set SS from SW (max 59)
 * KEY3: set MM from SW (max 59)
 *
 * KEY1/2/3 also update LEDR with the SW value.
 * Ctrl+C stops the stopwatch and clears HEX/LEDR.
 */

int main(void)
{
    int KEY_FD, SW_FD, LEDR_FD, STOPWATCH_FD;

    char KEY_buffer[KEY_BYTES + 1];
    char SW_buffer[SW_BYTES + 1];
    char STOPWATCH_buffer[STOPWATCH_BYTES + 1];
    char TIME_buffer[16];

    int ret_val, chars_read;
    unsigned int KEY_value, SW_value;
    int MM, SS, DD;
    int running = 1;
    int exit_status = 0;

    // Catch Ctrl+C
    signal(SIGINT, catchSIGINT);

    // Open all character device drivers
    if ((KEY_FD = open("/dev/KEY", O_RDONLY)) == -1){
        printf("Error opening /dev/KEY: %s\n", strerror(errno));
        return 1;
    }

    if ((SW_FD = open("/dev/SW", O_RDONLY)) == -1){
        printf("Error opening /dev/SW: %s\n", strerror(errno));
        close(KEY_FD);
        return 1;
    }

    if ((LEDR_FD = open("/dev/LEDR", O_WRONLY)) == -1){
        printf("Error opening /dev/LEDR: %s\n", strerror(errno));
        close(KEY_FD);
        close(SW_FD);
        return 1;
    }

    if ((STOPWATCH_FD = open("/dev/stopwatch", O_RDWR)) == -1){
        printf("Error opening /dev/stopwatch: %s\n", strerror(errno));
        close(KEY_FD);
        close(SW_FD);
        close(LEDR_FD);
        return 1;
    }

    // Enable HEX display and start stopwatch
    if (write(STOPWATCH_FD, "disp", 4) != 4 || write(STOPWATCH_FD, "run", 3) != 3){
        perror("Initialize stopwatch");
        stop = 1;
        exit_status = 1;
    }

    while (!stop){
        // Read KEY edge-capture until EOF
        chars_read = 0;

        while ((ret_val = read(KEY_FD, KEY_buffer + chars_read, KEY_BYTES - chars_read)) > 0){
            chars_read += ret_val;
        }

        if (stop){
            break;
        }

        if (ret_val < 0){
            printf("Error reading KEY: %s\n", strerror(errno));
            exit_status = 1;
            break;
        }

        KEY_buffer[chars_read] = '\0';

        KEY_value = 0;
        sscanf(KEY_buffer, "%X", &KEY_value);

        // KEY0: toggle run/pause
        if (KEY_value & 0x1)
        {
            if (running)
            {
                write(STOPWATCH_FD, "stop", 4);
                running = 0;
            }
            else
            {
                write(STOPWATCH_FD, "run", 3);
                running = 1;
            }
        }

        // KEY1 / KEY2 / KEY3
        if (KEY_value & 0xE){
            // Read SW only when KEY1/2/3 pressed
            chars_read = 0;

            while ((ret_val = read(SW_FD, SW_buffer + chars_read, SW_BYTES - chars_read)) > 0){
                chars_read += ret_val;
            }

            if (stop)
                break;

            if (ret_val < 0)
            {
                printf("Error reading SW: %s\n", strerror(errno));
                exit_status = 1;
                break;
            }

            SW_buffer[chars_read] = '\0';

            SW_value = 0;
            sscanf(SW_buffer, "%X", &SW_value);

            // Display SW value on LEDR
            if (write(LEDR_FD, SW_buffer,  strlen(SW_buffer)) < 0){
                perror("Write LEDR");
                exit_status = 1;
                break;
            }

            // Read current stopwatch time until EOF
            chars_read = 0;

            while ((ret_val = read(STOPWATCH_FD, STOPWATCH_buffer + chars_read, STOPWATCH_BYTES - chars_read)) > 0)
            {
                chars_read += ret_val;
            }

            if (stop)
                break;

            if (ret_val < 0)
            {
                printf("Error reading stopwatch: %s\n",
                       strerror(errno));
                exit_status = 1;
                break;
            }

            STOPWATCH_buffer[chars_read] = '\0';

            if (sscanf(STOPWATCH_buffer, "%d:%d:%d", &MM, &SS, &DD) != 3){
                printf("Invalid stopwatch time\n");
                exit_status = 1;
                break;
            }

            // KEY1: set hundredths
            if (KEY_value & 0x2)
                DD = SW_value > 99 ? 99 : SW_value;

            // KEY2: set seconds
            if (KEY_value & 0x4)
                SS = SW_value > 59 ? 59 : SW_value;

            // KEY3: set minutes
            if (KEY_value & 0x8)
                MM = SW_value > 59 ? 59 : SW_value;

            // Format and write updated time
            sprintf(TIME_buffer, "%02d:%02d:%02d",
                    MM, SS, DD);

            if (write(STOPWATCH_FD, TIME_buffer,  strlen(TIME_buffer)) < 0){
                perror("Write stopwatch");
                exit_status = 1;
                break;
            }
        }

        // Avoid busy waiting
        usleep(10000);
    }

    // Stop stopwatch
    write(STOPWATCH_FD, "stop", 4);

    // Turn off HEX display
    write(STOPWATCH_FD, "nodisp", 6);

    // Turn off LEDR
    write(LEDR_FD, "000", 3);

    // Close all devices
    close(KEY_FD);
    close(SW_FD);
    close(LEDR_FD);
    close(STOPWATCH_FD);

    printf("\nProgram exited.\n");

    return exit_status;
}
