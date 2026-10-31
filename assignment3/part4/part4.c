#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#define KEY_BYTES 2       // one hexadecimal digit plus '\n'
#define SW_BYTES 4        // three hexadecimal digits plus '\n'
#define STOPWATCH_BYTES 9 // MM:SS:DD plus '\n'

volatile sig_atomic_t stop;

void catchSIGINT(int signum)
{
    (void)signum;
    stop = 1;
}

/* This program uses the character device drivers /dev/KEY,
 * /dev/SW, /dev/LEDR, and /dev/stopwatch.
 *
 * KEY0: toggle stopwatch run / pause.
 * KEY1: set DD using SW value (maximum 99).
 * KEY2: set SS using SW value (maximum 59).
 * KEY3: set MM using SW value (maximum 59).
 *
 * Whenever KEY1/2/3 is pressed, the current SW value
 * is also displayed on LEDR.
 *
 * Press Ctrl+C to stop the stopwatch, clear HEX and LEDR,
 * and exit the program.
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

    // Catch SIGINT from Ctrl+C
    signal(SIGINT, catchSIGINT);

    // Open KEY and SW character device drivers for reading
    if ((KEY_FD = open("/dev/KEY", O_RDONLY)) == -1)
    {
        printf("Error opening /dev/KEY: %s\n", strerror(errno));
        return -1;
    }

    if ((SW_FD = open("/dev/SW", O_RDONLY)) == -1)
    {
        printf("Error opening /dev/SW: %s\n", strerror(errno));
        close(KEY_FD);
        return -1;
    }

    // Open LEDR character device driver for writing
    if ((LEDR_FD = open("/dev/LEDR", O_WRONLY)) == -1)
    {
        printf("Error opening /dev/LEDR: %s\n", strerror(errno));
        close(KEY_FD);
        close(SW_FD);
        return -1;
    }

    // Open stopwatch character device driver for reading and writing
    if ((STOPWATCH_FD = open("/dev/stopwatch", O_RDWR)) == -1)
    {
        printf("Error opening /dev/stopwatch: %s\n", strerror(errno));
        close(KEY_FD);
        close(SW_FD);
        close(LEDR_FD);
        return -1;
    }

    // Enable HEX display and start stopwatch
    write(STOPWATCH_FD, "disp", 4);
    write(STOPWATCH_FD, "run", 3);

    while (!stop)
    {
        // Read KEY edge-capture register until EOF
        chars_read = 0;
        while ((ret_val = read(KEY_FD, KEY_buffer + chars_read,
                               KEY_BYTES - chars_read)) > 0)
        {
            chars_read += ret_val;
        }

        if (ret_val < 0)
        {
            if (errno == EINTR && stop)
                break;

            printf("Error reading KEY: %s\n", strerror(errno));
            break;
        }

        KEY_buffer[chars_read] = '\0';

        KEY_value = 0;
        sscanf(KEY_buffer, "%X", &KEY_value);

        // KEY0: toggle run / pause
        if (KEY_value & 0x1)
        {
            if (running)
            {
                write(STOPWATCH_FD, "stop", 4);
                running = 0;
                printf("Stopwatch paused\n");
            }
            else
            {
                write(STOPWATCH_FD, "run", 3);
                running = 1;
                printf("Stopwatch running\n");
            }
        }

        // KEY1, KEY2, KEY3: set stopwatch time
        if (KEY_value & 0xE)
        {
            // Read current SW value until EOF
            chars_read = 0;
            while ((ret_val = read(SW_FD, SW_buffer + chars_read,
                                   SW_BYTES - chars_read)) > 0)
            {
                chars_read += ret_val;
            }

            if (ret_val < 0)
            {
                if (errno == EINTR && stop)
                    break;

                printf("Error reading SW: %s\n", strerror(errno));
                break;
            }

            SW_buffer[chars_read] = '\0';

            SW_value = 0;
            sscanf(SW_buffer, "%X", &SW_value);

            // Display current SW value on LEDR
            write(LEDR_FD, SW_buffer, strlen(SW_buffer));

            // Read current stopwatch time until EOF
            chars_read = 0;
            while ((ret_val = read(STOPWATCH_FD,
                                   STOPWATCH_buffer + chars_read,
                                   STOPWATCH_BYTES - chars_read)) > 0)
            {
                chars_read += ret_val;
            }

            if (ret_val < 0)
            {
                if (errno == EINTR && stop)
                    break;

                printf("Error reading stopwatch: %s\n",
                       strerror(errno));
                break;
            }

            STOPWATCH_buffer[chars_read] = '\0';

            if (sscanf(STOPWATCH_buffer, "%d:%d:%d",
                       &MM, &SS, &DD) != 3)
            {
                printf("Invalid stopwatch time\n");
                break;
            }

            // KEY1: set hundredths of a second
            if (KEY_value & 0x2)
                DD = SW_value > 99 ? 99 : SW_value;

            // KEY2: set seconds
            if (KEY_value & 0x4)
                SS = SW_value > 59 ? 59 : SW_value;

            // KEY3: set minutes
            if (KEY_value & 0x8)
                MM = SW_value > 59 ? 59 : SW_value;

            // Write updated time to stopwatch
            sprintf(TIME_buffer, "%02d:%02d:%02d", MM, SS, DD);

            write(STOPWATCH_FD, TIME_buffer, strlen(TIME_buffer));

            printf("Stopwatch set to %s\n", TIME_buffer);
        }

        // Avoid continuously using 100% CPU
        usleep(10000);
    }

    // Stop the stopwatch
    write(STOPWATCH_FD, "stop", 4);

    // Clear HEX display
    write(STOPWATCH_FD, "nodisp", 6);

    // Clear LEDR
    write(LEDR_FD, "000", 3);

    // Close all character device files
    close(KEY_FD);
    close(SW_FD);
    close(LEDR_FD);
    close(STOPWATCH_FD);

    printf("\nProgram exited.\n");

    return 0;
}
