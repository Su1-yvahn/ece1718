#include <stdio.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#define accel_BYTES 20 // number of characters to read from /dev/accel

volatile sig_atomic_t stop;
void catchSIGINT(int signum)
{
    stop = 1;
}

/* This code uses the following character device driver: /dev/accel. It prints acceleration
 * data whenever available. */
int main(int argc, char *argv[])
{
    int accel_FD;                   // file descriptor
    char accel_buffer[accel_BYTES]; // buffer for accel char data
    int bytes, bytes_read, ready, accel_x, accel_y, accel_z;
    int mG_per_LSB;
    time_t start_time, elapsed_time; // time limited sample solution

    // catch SIGINT from ctrl+c, instead of having it abruptly close this program
    signal(SIGINT, catchSIGINT);
    start_time = time(NULL);

    // Open the character device driver
    if ((accel_FD = open("/dev/accel", O_RDWR)) == -1)
    {
        printf("Error opening /dev/accel: %s\n", strerror(errno));
        return -1;
    }

    while (!stop)
    {
        bytes_read = 0;
        while (bytes = read(accel_FD, accel_buffer, accel_BYTES))
            bytes_read += bytes; // read the accel port until EOF
        if (bytes_read != accel_BYTES)
        {
            printf("Error: %d bytes expected, but %d bytes read\n", accel_BYTES, bytes_read);
            stop = 1;
        }
        else if (sscanf(accel_buffer, "%d %d %d %d %d", &ready, &accel_x, &accel_y, &accel_z, &mG_per_LSB) != 5)
        {
            printf("Error parsing data from /dev/accel\n");
            stop = 1;
        }
        else if (ready)
        {
            printf("X = %d, Y = %d, Z = %d\n", accel_x * mG_per_LSB, accel_y * mG_per_LSB, accel_z * mG_per_LSB); // print x, y, z acceleration data
        }
        elapsed_time = time(NULL) - start_time;
        if (elapsed_time > 12 || elapsed_time < 0)
            stop = 1;
    }

    close(accel_FD);
    printf("Exiting sample solution program\n");
    return 0;
}
