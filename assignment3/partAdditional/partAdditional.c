#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#define KEY_BYTES 2 // one hexadecimal digit plus '\n'
#define SW_BYTES 4  // three hexadecimal digits plus '\n'
#define MAX_TOTAL 999999

volatile sig_atomic_t stop;

void catchSIGINT(int signum)
{
    (void)signum;
    stop = 1;
}

/* Additional Part based on Part 4.
 *
 * SW9-SW0 provide the input value.
 *
 * KEY0: add the current SW value to the total
 * KEY1: subtract the current SW value from the total (minimum 0)
 * KEY2: reset the total to 0
 * KEY3: set the total equal to the current SW value
 *
 * Whenever any KEY is pressed:
 *   - LEDR9-LEDR0 show the current SW value
 *   - HEX5-HEX0 show the resulting total as a six-digit decimal value
 *   - the terminal prints the operation, SW value, and resulting total
 */
int main(void)
{
    int KEY_FD, SW_FD, LEDR_FD, HEX_FD;
    char KEY_buffer[KEY_BYTES + 1];
    char SW_buffer[SW_BYTES + 1];
    char LEDR_buffer[16];
    char HEX_buffer[32];
    int ret_val, chars_read;
    unsigned int KEY_value, SW_value;
    unsigned int total = 0;

    // Catch Ctrl+C so the device files can be closed cleanly.
    signal(SIGINT, catchSIGINT);

    // Open KEY and SW for reading.
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

    // Open LEDR and HEX for writing.
    if ((LEDR_FD = open("/dev/LEDR", O_WRONLY)) == -1)
    {
        printf("Error opening /dev/LEDR: %s\n", strerror(errno));
        close(KEY_FD);
        close(SW_FD);
        return -1;
    }

    if ((HEX_FD = open("/dev/HEX", O_WRONLY)) == -1)
    {
        printf("Error opening /dev/HEX: %s\n", strerror(errno));
        close(KEY_FD);
        close(SW_FD);
        close(LEDR_FD);
        return -1;
    }

    printf("Additional Part running. Press Ctrl+C to exit.\n");
    printf("KEY0=add, KEY1=subtract, KEY2=reset, KEY3=set total to SW.\n");

    while (!stop)
    {
        /* Read /dev/KEY until EOF. This is important because the Part 2
         * KEY driver uses the EOF read to reset its file offset for the
         * next poll. */
        chars_read = 0;
        while ((ret_val = read(KEY_FD, KEY_buffer, KEY_BYTES)) > 0)
            chars_read += ret_val;

        KEY_buffer[chars_read] = '\0';
        KEY_value = 0;
        sscanf(KEY_buffer, "%X", &KEY_value);

        // A nonzero value means one or more KEYs were pressed.
        if (KEY_value != 0)
        {
            /* Capture the current SW value. Read until EOF so that the
             * SW driver's file offset is reset and the next KEY press
             * immediately gets a fresh switch value. */
            chars_read = 0;
            while ((ret_val = read(SW_FD, SW_buffer, SW_BYTES)) > 0)
                chars_read += ret_val;

            SW_buffer[chars_read] = '\0';
            SW_value = 0;
            sscanf(SW_buffer, "%X", &SW_value);
            SW_value &= 0x3FF; // only SW9-SW0 are used

            // Always display the current SW value on LEDR.
            sprintf(LEDR_buffer, "%03X", SW_value);
            write(LEDR_FD, LEDR_buffer, strlen(LEDR_buffer));

            // KEY0: add SW value to total.
            if (KEY_value & 0x1)
            {
                if (total + SW_value > MAX_TOTAL)
                    total = MAX_TOTAL;
                else
                    total += SW_value;

                printf("KEY0 ADD:      SW = %u, total = %u\n", SW_value, total);
            }

            // KEY1: subtract SW value from total.
            if (KEY_value & 0x2)
            {
                if (SW_value > total)
                    total = 0;
                else
                    total -= SW_value;

                printf("KEY1 SUBTRACT: SW = %u, total = %u\n", SW_value, total);
            }

            // KEY2: reset total to zero.
            if (KEY_value & 0x4)
            {
                total = 0;

                printf("KEY2 RESET:    SW = %u, total = %u\n", SW_value, total);
            }

            // KEY3: set total equal to the current SW value.
            if (KEY_value & 0x8)
            {
                total = SW_value;

                printf("KEY3 SET:      SW = %u, total = %u\n", SW_value, total);
            }

            // Display the resulting total on HEX5-HEX0.
            sprintf(HEX_buffer, "%06u", total);
            write(HEX_FD, HEX_buffer, strlen(HEX_buffer));
        }

        // Avoid continuously using 100% CPU while polling.
        usleep(10000);
    }

    close(KEY_FD);
    close(SW_FD);
    close(LEDR_FD);
    close(HEX_FD);

    printf("\nExiting additional part.\n");
    return 0;
}
