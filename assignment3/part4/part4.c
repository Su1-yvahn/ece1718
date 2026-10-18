#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#define KEY_BYTES 2 // one hexadecimal digit plus '\n'
#define SW_BYTES 4  // three hexadecimal digits plus '\n'

volatile sig_atomic_t stop;

void catchSIGINT(int signum)
{
    (void)signum;
    stop = 1;
}

/* This program uses the character device drivers /dev/KEY, /dev/SW,
 * /dev/LEDR, and /dev/HEX. Whenever any pushbutton KEY is pressed,
 * the current SW value is displayed on the red LEDs. The SW value is
 * also added to a running sum, which is displayed as a decimal value
 * on HEX5-HEX0. */
int main(void)
{
    int KEY_FD, SW_FD, LEDR_FD, HEX_FD;
    char KEY_buffer[KEY_BYTES + 1];
    char SW_buffer[SW_BYTES + 1];
    char LEDR_buffer[16];
    char HEX_buffer[32];
    int ret_val, chars_read;
    unsigned int KEY_value, SW_value;
    unsigned int sum = 0;

    // Catch SIGINT from Ctrl+C, instead of abruptly closing this program
    signal(SIGINT, catchSIGINT);

    // Open the KEY and SW character device drivers for reading
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

    // Open the LEDR and HEX character device drivers for writing
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

    while (!stop)
    {
        /* Read /dev/KEY until EOF. This also leaves the driver's file
         * offset ready for the next KEY read. */
        chars_read = 0;
        while ((ret_val = read(KEY_FD, KEY_buffer, KEY_BYTES)) > 0)
            chars_read += ret_val;

        KEY_buffer[chars_read] = '\0';
        KEY_value = 0;
        sscanf(KEY_buffer, "%X", &KEY_value);

        // Any nonzero KEY value means that one or more KEYs were pressed
        if (KEY_value != 0)
        {
            /* Capture the current SW value. The SW driver returns three
             * hexadecimal digits, such as 005 or 3FF. */
            chars_read = 0;
            while ((ret_val = read(SW_FD, SW_buffer, SW_BYTES)) > 0)
                chars_read += ret_val;

            SW_buffer[chars_read] = '\0';
            SW_value = 0;
            sscanf(SW_buffer, "%X", &SW_value);

            // Display the current SW value on LEDR
            sprintf(LEDR_buffer, "%03X", SW_value);
            write(LEDR_FD, LEDR_buffer, strlen(LEDR_buffer));

            // Add the SW value to the running sum and display it on HEX
            if (SW_value == 0)
                sum = 0; // reset accumulator
            else
                sum += SW_value;
            sprintf(HEX_buffer, "%06u", sum);
            write(HEX_FD, HEX_buffer, strlen(HEX_buffer));
        }

        // Avoid continuously using 100% CPU while polling the KEY driver
        usleep(10000);
    }

    close(KEY_FD);
    close(SW_FD);
    close(LEDR_FD);
    close(HEX_FD);

    return 0;
}
