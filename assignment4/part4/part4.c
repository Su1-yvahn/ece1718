#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/select.h>

#define KEY_DEVICE "/dev/IntelFPGAUP/KEY"
#define SW_DEVICE "/dev/IntelFPGAUP/SW"
#define LEDR_DEVICE "/dev/IntelFPGAUP/LEDR"
#define STOPWATCH_DEVICE "/dev/stopwatch"

#define KEY_BYTES 2
#define SW_BYTES 4
#define STOPWATCH_BYTES 9

#define KEY0 0x1
#define KEY1 0x2
#define KEY2 0x4
#define KEY3 0x8

// default stopwatch time: 10 seconds
#define DEFAULT_MM 0
#define DEFAULT_SS 10
#define DEFAULT_DD 0

volatile sig_atomic_t stop;

void catchSIGINT(int signum)
{
    (void)signum;
    stop = 1;
}

static int read_stopwatch(int STOPWATCH_FD, int *MM, int *SS, int *DD)
{
    char STOPWATCH_buffer[STOPWATCH_BYTES + 1];
    int chars_read = 0;
    int ret_val;

    while ((ret_val = read(STOPWATCH_FD, STOPWATCH_buffer + chars_read, STOPWATCH_BYTES - chars_read)) > 0)
    {
        chars_read += ret_val;
    }

    if (ret_val < 0)
    {
        if (errno == EINTR && stop)
            return -1;

        printf("Error reading stopwatch: %s\n", strerror(errno));
        return -1;
    }

    STOPWATCH_buffer[chars_read] = '\0';

    if (sscanf(STOPWATCH_buffer, "%d:%d:%d", MM, SS, DD) != 3)
    {
        printf("Invalid stopwatch time\n");
        return -1;
    }

    return 0;
}

static int write_stopwatch_time(int STOPWATCH_FD, int MM, int SS, int DD)
{
    char TIME_buffer[16];

    sprintf(TIME_buffer, "%02d:%02d:%02d", MM, SS, DD);

    if (write(STOPWATCH_FD, TIME_buffer, strlen(TIME_buffer)) < 0)
    {
        printf("Error writing stopwatch: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

static int hundredths_from_time(int MM, int SS, int DD)
{
    return ((MM * 60) + SS) * 100 + DD;
}

static void print_remaining_time(int remaining_hundredths)
{
    int MM, SS, DD;

    if (remaining_hundredths < 0)
        remaining_hundredths = 0;

    MM = remaining_hundredths / 6000;
    remaining_hundredths %= 6000;
    SS = remaining_hundredths / 100;
    DD = remaining_hundredths % 100;

    // print the time in red, then reset the terminal color
    printf("\033[1A\033[20G\033[31m%02d:%02d:%02d\033[0m\n", MM, SS, DD);
}

static void make_question(int questions_answered, int *a, int *b)
{
    int maximum;

    if (questions_answered < 5)
        maximum = 9; // one-digit operands
    else if (questions_answered < 10)
        maximum = 99; // two-digit operands
    else
        maximum = 999; // three-digit operands

    *a = rand() % (maximum + 1);
    *b = rand() % (maximum + 1);
}

/*
 * Wait for an answer while periodically checking the hardware stopwatch.
 * Return:
 *   1 = a complete line was received
 *   0 = stopwatch expired
 *  -1 = error / interrupted
 */
static int wait_for_answer(int STOPWATCH_FD, char *buffer, size_t size, int *remaining_hundredths)
{
    while (!stop)
    {
        int MM, SS, DD;
        fd_set readfds;
        struct timeval timeout;
        int ready;

        if (read_stopwatch(STOPWATCH_FD, &MM, &SS, &DD) != 0)
            return -1;

        *remaining_hundredths = hundredths_from_time(MM, SS, DD);

        if (*remaining_hundredths <= 0)
            return 0;

        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);

        timeout.tv_sec = 0;
        timeout.tv_usec = 10000; // 10 ms

        ready = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout);

        if (ready < 0)
        {
            if (errno == EINTR)
            {
                if (stop)
                    return -1;
                continue;
            }

            perror("select");
            return -1;
        }

        if (ready > 0 && FD_ISSET(STDIN_FILENO, &readfds))
        {
            if (fgets(buffer, (int)size, stdin) == NULL)
                return -1;

            // Re-read the stopwatch after the user presses Enter so an answer entered after expiration is not accepted.
            if (read_stopwatch(STOPWATCH_FD, &MM, &SS, &DD) != 0)
                return -1;

            *remaining_hundredths = hundredths_from_time(MM, SS, DD);

            if (*remaining_hundredths <= 0)
                return 0;

            return 1;
        }
    }
    return -1;
}

static void cleanup_devices(int LEDR_FD, int STOPWATCH_FD)
{
    write(STOPWATCH_FD, "stop", 4);
    write(STOPWATCH_FD, "nodisp", 6);
    write(LEDR_FD, "000", 3);
}

int main(void)
{
    int KEY_FD, SW_FD, LEDR_FD, STOPWATCH_FD;

    char KEY_buffer[KEY_BYTES + 1];
    char SW_buffer[SW_BYTES + 1];
    char LEDR_buffer[16];

    int ret_val, chars_read;
    unsigned int KEY_value, SW_value;

    int configured_MM = DEFAULT_MM;
    int configured_SS = DEFAULT_SS;
    int configured_DD = DEFAULT_DD;

    int configured_hundredths;
    int questions_answered = 0;
    long total_answer_hundredths = 0;

    signal(SIGINT, catchSIGINT);
    srand((unsigned int)time(NULL));

    // Open KEY and SW character device drivers for reading
    if ((KEY_FD = open(KEY_DEVICE, O_RDONLY)) == -1)
    {
        printf("Error opening %s: %s\n", KEY_DEVICE, strerror(errno));
        return -1;
    }

    if ((SW_FD = open(SW_DEVICE, O_RDONLY)) == -1)
    {
        printf("Error opening %s: %s\n", SW_DEVICE, strerror(errno));
        close(KEY_FD);
        return -1;
    }

    // Open LEDR character device driver for writing
    if ((LEDR_FD = open(LEDR_DEVICE, O_WRONLY)) == -1)
    {
        printf("Error opening %s: %s\n", LEDR_DEVICE, strerror(errno));
        close(KEY_FD);
        close(SW_FD);
        return -1;
    }

    // Open stopwatch character device driver for reading and writing
    if ((STOPWATCH_FD = open(STOPWATCH_DEVICE, O_RDWR)) == -1)
    {
        printf("Error opening %s: %s\n", STOPWATCH_DEVICE, strerror(errno));
        close(KEY_FD);
        close(SW_FD);
        close(LEDR_FD);
        return -1;
    }

    // Setup phase:
    // show the default time, keep the stopwatch paused while the user edits
    // MM/SS/DD, and wait for KEY0 to start the game
    if (write(STOPWATCH_FD, "disp", 4) < 0 ||
        write(STOPWATCH_FD, "stop", 4) < 0 ||
        write_stopwatch_time(STOPWATCH_FD,
                             configured_MM,
                             configured_SS,
                             configured_DD) != 0)
    {
        cleanup_devices(LEDR_FD, STOPWATCH_FD);
        close(KEY_FD);
        close(SW_FD);
        close(LEDR_FD);
        close(STOPWATCH_FD);
        return 1;
    }

    printf("Set stopwatch if desired. Press KEY0 to start\n");

    while (!stop)
    {
        // Read KEY edge-capture register until EOF
        chars_read = 0;
        while ((ret_val = read(KEY_FD,
                               KEY_buffer + chars_read,
                               KEY_BYTES - chars_read)) > 0)
        {
            chars_read += ret_val;
        }

        if (ret_val < 0)
        {
            if (errno == EINTR && stop)
                break;

            printf("Error reading KEY: %s\n", strerror(errno));
            cleanup_devices(LEDR_FD, STOPWATCH_FD);
            close(KEY_FD);
            close(SW_FD);
            close(LEDR_FD);
            close(STOPWATCH_FD);
            return 1;
        }

        KEY_buffer[chars_read] = '\0';

        KEY_value = 0;
        sscanf(KEY_buffer, "%X", &KEY_value);
        KEY_value &= 0xF;

        if (KEY_value == 0)
        {
            usleep(10000);
            continue;
        }

        // KEY0: start the game
        if (KEY_value & KEY0)
            break;

        // KEY1, KEY2, KEY3: set stopwatch time
        if (KEY_value & (KEY1 | KEY2 | KEY3))
        {
            // Read current SW value until EOF
            chars_read = 0;
            while ((ret_val = read(SW_FD,
                                   SW_buffer + chars_read,
                                   SW_BYTES - chars_read)) > 0)
            {
                chars_read += ret_val;
            }

            if (ret_val < 0)
            {
                if (errno == EINTR && stop)
                    break;

                printf("Error reading SW: %s\n", strerror(errno));
                cleanup_devices(LEDR_FD, STOPWATCH_FD);
                close(KEY_FD);
                close(SW_FD);
                close(LEDR_FD);
                close(STOPWATCH_FD);
                return 1;
            }

            SW_buffer[chars_read] = '\0';

            SW_value = 0;
            sscanf(SW_buffer, "%X", &SW_value);
            SW_value &= 0x3FF;

            // Display current SW value on LEDR
            sprintf(LEDR_buffer, "%03X", SW_value);
            write(LEDR_FD, LEDR_buffer, strlen(LEDR_buffer));

            // KEY1: set hundredths of a second
            if (KEY_value & KEY1)
                configured_DD = (SW_value > 99) ? 99 : (int)SW_value;

            // KEY2: set seconds
            if (KEY_value & KEY2)
                configured_SS = (SW_value > 59) ? 59 : (int)SW_value;

            // KEY3: set minutes
            if (KEY_value & KEY3)
                configured_MM = (SW_value > 59) ? 59 : (int)SW_value;

            if (write_stopwatch_time(STOPWATCH_FD, configured_MM, configured_SS, configured_DD) != 0)
            {
                cleanup_devices(LEDR_FD, STOPWATCH_FD);
                close(KEY_FD);
                close(SW_FD);
                close(LEDR_FD);
                close(STOPWATCH_FD);
                return 1;
            }
        }
        usleep(10000);
    }

    if (stop)
    {
        cleanup_devices(LEDR_FD, STOPWATCH_FD);
        close(KEY_FD);
        close(SW_FD);
        close(LEDR_FD);
        close(STOPWATCH_FD);
        return 0;
    }

    configured_hundredths =
        hundredths_from_time(configured_MM, configured_SS, configured_DD);

    // A zero configured time cannot provide a question interval. expires immediately
    while (!stop)
    {
        int a, b;
        int correct_answer;
        int remaining_hundredths = 0;
        int question_finished = 0;
        char input[128];

        make_question(questions_answered, &a, &b);
        correct_answer = a + b;

        if (write(STOPWATCH_FD, "stop", 4) < 0 ||
            write_stopwatch_time(STOPWATCH_FD,
                                 configured_MM,
                                 configured_SS,
                                 configured_DD) != 0 ||
            write(STOPWATCH_FD, "run", 3) < 0)
        {
            cleanup_devices(LEDR_FD, STOPWATCH_FD);
            close(KEY_FD);
            close(SW_FD);
            close(LEDR_FD);
            close(STOPWATCH_FD);
            return 1;
        }

        printf("%d + %d = ", a, b);
        fflush(stdout);

        while (!question_finished && !stop)
        {
            int status;
            char *endptr;
            long answer;

            status = wait_for_answer(STOPWATCH_FD, input, sizeof(input), &remaining_hundredths);

            if (status < 0)
            {
                if (!stop)
                    fprintf(stderr, "\nError while waiting for answer.\n");

                cleanup_devices(LEDR_FD, STOPWATCH_FD);
                close(KEY_FD);
                close(SW_FD);
                close(LEDR_FD);
                close(STOPWATCH_FD);
                return stop ? 0 : 1;
            }

            if (status == 0)
            {
                double average_seconds = (questions_answered > 0) ? ((double)total_answer_hundredths / (double)questions_answered / 100.0) : 0.0;

                write(STOPWATCH_FD, "stop", 4);

                printf("\nTime expired! You answered %d question%s", questions_answered, (questions_answered == 1) ? "" : "s");

                if (questions_answered > 0)
                    printf(", in an average of %.2f seconds.\n", average_seconds);
                else
                    printf(".\n");

                cleanup_devices(LEDR_FD, STOPWATCH_FD);
                close(KEY_FD);
                close(SW_FD);
                close(LEDR_FD);
                close(STOPWATCH_FD);
                return 0;
            }

            // print the stopwatch value that remained when Enter was pressed
            print_remaining_time(remaining_hundredths);

            errno = 0;
            answer = strtol(input, &endptr, 10);

            while (*endptr == ' ' || *endptr == '\t')
                endptr++;

            if (errno == 0 && endptr != input && (*endptr == '\n' || *endptr == '\0') && answer == correct_answer)
            {
                int elapsed_hundredths = configured_hundredths - remaining_hundredths;

                if (elapsed_hundredths < 0)
                    elapsed_hundredths = 0;

                write(STOPWATCH_FD, "stop", 4);

                total_answer_hundredths += elapsed_hundredths;
                questions_answered++;
                question_finished = 1;
            }
            else
            {
                printf("Try again: ");
                fflush(stdout);
            }
        }
    }

    cleanup_devices(LEDR_FD, STOPWATCH_FD);

    close(KEY_FD);
    close(SW_FD);
    close(LEDR_FD);
    close(STOPWATCH_FD);

    return 0;
}
