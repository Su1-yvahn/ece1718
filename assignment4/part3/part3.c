#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#define STOPWATCH_DEV "/dev/stopwatch"

#define KEY_DEV_PRIMARY "/dev/IntelFPGAUP/KEY"
#define KEY_DEV_FALLBACK "/dev/KEY"

#define SW_DEV_PRIMARY "/dev/IntelFPGAUP/SW"
#define SW_DEV_FALLBACK "/dev/SW"

#define LEDR_DEV_PRIMARY "/dev/IntelFPGAUP/LEDR"
#define LEDR_DEV_FALLBACK "/dev/LEDR"

#define KEY0 0x1
#define KEY1 0x2
#define KEY2 0x4
#define KEY3 0x8

static volatile sig_atomic_t stop_program = 0;

static void catchSIGINT(int signum)
{
    (void)signum;
    stop_program = 1;
}

/* Open a device using the professor's /dev/IntelFPGAUP/... path first.
 * If that node does not exist, fall back to /dev/KEY, /dev/SW, or /dev/LEDR.
 * This makes Part 3 work with either driver naming convention. */
static int open_input_device(const char *primary, const char *fallback)
{
    int fd = open(primary, O_RDONLY);

    if (fd == -1 && errno == ENOENT)
        fd = open(fallback, O_RDONLY);

    if (fd == -1)
        fprintf(stderr, "Error opening %s or %s: %s\n",
                primary, fallback, strerror(errno));

    return fd;
}

/* Read an ASCII hexadecimal value from a character device.
 * Opening the device for each read resets the file offset used by the driver. */
static int read_hex_device(const char *primary, const char *fallback,
                           unsigned int *value)
{
    int fd;
    ssize_t n;
    char buffer[32];
    char *endptr;
    unsigned long parsed;

    fd = open_input_device(primary, fallback);
    if (fd == -1)
        return -1;

    n = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);

    if (n < 0)
    {
        fprintf(stderr, "Error reading %s/%s: %s\n",
                primary, fallback, strerror(errno));
        return -1;
    }

    if (n == 0)
    {
        *value = 0;
        return 0;
    }

    buffer[n] = '\0';

    errno = 0;
    parsed = strtoul(buffer, &endptr, 16);
    if (errno != 0 || endptr == buffer)
    {
        fprintf(stderr, "Invalid value read from device: %s\n", buffer);
        return -1;
    }

    *value = (unsigned int)parsed;
    return 0;
}

/* Write a command/string to a device with a fixed pathname. */
static int write_text_device(const char *device, const char *text)
{
    int fd;
    ssize_t n;
    size_t length = strlen(text);

    fd = open(device, O_WRONLY);
    if (fd == -1)
    {
        fprintf(stderr, "Error opening %s: %s\n", device, strerror(errno));
        return -1;
    }

    n = write(fd, text, length);
    close(fd);

    if (n != (ssize_t)length)
    {
        if (n < 0)
            fprintf(stderr, "Error writing %s: %s\n", device, strerror(errno));
        else
            fprintf(stderr, "Short write to %s\n", device);
        return -1;
    }

    return 0;
}

/* Write to an FPGA character device using either pathname convention. */
static int write_fpga_device(const char *primary, const char *fallback,
                             const char *text)
{
    int fd;
    ssize_t n;
    size_t length = strlen(text);

    fd = open(primary, O_WRONLY);

    if (fd == -1 && errno == ENOENT)
        fd = open(fallback, O_WRONLY);

    if (fd == -1)
    {
        fprintf(stderr, "Error opening %s or %s: %s\n",
                primary, fallback, strerror(errno));
        return -1;
    }

    n = write(fd, text, length);
    close(fd);

    if (n != (ssize_t)length)
    {
        if (n < 0)
            fprintf(stderr, "Error writing FPGA device: %s\n", strerror(errno));
        else
            fprintf(stderr, "Short write to FPGA device\n");
        return -1;
    }

    return 0;
}

/* Read the current stopwatch value in MM:SS:DD format. */
static int read_stopwatch(int *MM, int *SS, int *DD)
{
    int fd;
    ssize_t n;
    char buffer[32];

    fd = open(STOPWATCH_DEV, O_RDONLY);
    if (fd == -1)
    {
        fprintf(stderr, "Error opening %s: %s\n", STOPWATCH_DEV, strerror(errno));
        return -1;
    }

    n = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);

    if (n <= 0)
    {
        if (n < 0)
            fprintf(stderr, "Error reading %s: %s\n", STOPWATCH_DEV, strerror(errno));
        return -1;
    }

    buffer[n] = '\0';

    if (sscanf(buffer, "%d:%d:%d", MM, SS, DD) != 3)
    {
        fprintf(stderr, "Invalid stopwatch value: %s\n", buffer);
        return -1;
    }

    return 0;
}

/* Update only the stopwatch field(s) selected by KEY1/KEY2/KEY3.
 * The other fields keep their current values. */
static int set_stopwatch_field(unsigned int keys, unsigned int sw_value)
{
    int MM, SS, DD;
    char command[16];

    if (read_stopwatch(&MM, &SS, &DD) != 0)
        return -1;

    if (keys & KEY1)
        DD = (sw_value > 99) ? 99 : (int)sw_value;

    if (keys & KEY2)
        SS = (sw_value > 59) ? 59 : (int)sw_value;

    if (keys & KEY3)
        MM = (sw_value > 59) ? 59 : (int)sw_value;

    snprintf(command, sizeof(command), "%02d:%02d:%02d", MM, SS, DD);

    /* Important: actually write the updated time back to the Part II driver. */
    return write_text_device(STOPWATCH_DEV, command);
}

int main(void)
{
    unsigned int keys;
    unsigned int switches;
    int running = 1;
    char led_command[16];

    signal(SIGINT, catchSIGINT);

    /* DE1-SoC has HEX5-HEX0. Start Part 3 with the display enabled and the
     * stopwatch running so that the software running flag matches the driver. */
    if (write_text_device(STOPWATCH_DEV, "disp") != 0 ||
        write_text_device(STOPWATCH_DEV, "run") != 0)
        return 1;

    printf("Part 3 stopwatch control\n");
    printf("KEY0: run/pause toggle\n");
    printf("KEY1: set DD from SW (max 99)\n");
    printf("KEY2: set SS from SW (max 59)\n");
    printf("KEY3: set MM from SW (max 59)\n");
    printf("SW is copied to LEDR when KEY1, KEY2, or KEY3 is pressed.\n");
    printf("Press Ctrl-C to quit.\n");

    while (!stop_program)
    {
        /* KEY is an edge-capture device, so poll for a new key press. */
        if (read_hex_device(KEY_DEV_PRIMARY, KEY_DEV_FALLBACK, &keys) != 0)
            break;

        keys &= 0xF; /* DE1-SoC has KEY3 ... KEY0 */

        if (keys == 0)
        {
            usleep(10000);
            continue;
        }

        /* KEY0 toggles the stopwatch between run and pause. */
        if (keys & KEY0)
        {
            const char *command = running ? "stop" : "run";

            if (write_text_device(STOPWATCH_DEV, command) != 0)
                break;

            running = !running;
        }

        /* Match the professor's Part III behavior: SW is read and copied to
         * LEDR only after KEY1, KEY2, or KEY3 is pressed. */
        if (keys & (KEY1 | KEY2 | KEY3))
        {
            if (read_hex_device(SW_DEV_PRIMARY, SW_DEV_FALLBACK, &switches) != 0)
                break;

            switches &= 0x3FF; /* DE1-SoC has SW9 ... SW0 */

            snprintf(led_command, sizeof(led_command), "%03X", switches);
            if (write_fpga_device(LEDR_DEV_PRIMARY, LEDR_DEV_FALLBACK, led_command) != 0)
                break;

            if (set_stopwatch_field(keys, switches) != 0)
                break;
        }

        usleep(10000);
    }

    /* Clean up using the same good behavior as the teammate's version. */
    write_text_device(STOPWATCH_DEV, "stop");
    write_text_device(STOPWATCH_DEV, "nodisp");
    write_fpga_device(LEDR_DEV_PRIMARY, LEDR_DEV_FALLBACK, "000");

    printf("\nProgram exited. Stopwatch stopped; HEX and LEDR cleared.\n");
    return 0;
}
