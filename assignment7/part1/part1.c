#include "ADXL345.h"
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <sys/mman.h>
#include "address_map_arm.h"
#include "physical.h"

volatile sig_atomic_t stop;
void catchSIGINT(int signum)
{
    stop = 1;
}

int main(void)
{
    uint8_t devid;
    int16_t XYZ[3];
    time_t start_time, elapsed_time; // time limited sample solution

    // catch SIGINT from ^C, instead of having it abruptly close this program
    signal(SIGINT, catchSIGINT);
    start_time = time(NULL);

    // Set up virtual addresses for the I2C0 controller and System Manager (see ../ADXL345.c)
    if (map_mem() < 0)
        return 1;

    // Configure Pin Muxing
    Pinmux_Config();

    // Initialize I2C0 Controller
    I2C0_Init();

    // 0xE5 is read from DEVID(0x00) if I2C is functioning correctly
    ADXL345_REG_READ(0x00, &devid);

    // Correct Device ID
    if (devid == 0xE5)
    {
        // Initialize accelerometer chip
        ADXL345_Init();
        ADXL345_Calibrate();

        while (!stop)
        {
            if (ADXL345_IsDataReady())
            {
                ADXL345_XYZ_Read(XYZ);
                printf("X=%d mg, Y=%d mg, Z=%d mg\n", XYZ[0] * mg_per_lsb, XYZ[1] * mg_per_lsb,
                       XYZ[2] * mg_per_lsb);
            }
            sleep(1);
            elapsed_time = time(NULL) - start_time;
            if (elapsed_time > 30 || elapsed_time < 0)
                stop = 1;
        }
    }
    else
        printf("Incorrect device ID\n");

    unmap_mem();
    printf("Exiting sample solution program\n");
    return 0;
}
