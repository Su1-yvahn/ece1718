#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/select.h>

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

/*
 * The lab manual requires a default stopwatch time but does not prescribe
 * a particular value. This implementation uses 10 seconds.
 */
#define DEFAULT_MM 0
#define DEFAULT_SS 10
#define DEFAULT_DD 0

#define STARTING_LIVES 3

static volatile sig_atomic_t stop_program = 0;

static void catchSIGINT(int signum)
{
    (void)signum;
    stop_program = 1;
}

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
            fprintf(stderr, "Error writing %s: %s\n",
                    device, strerror(errno));
        else
            fprintf(stderr, "Short write to %s\n", device);
        return -1;
    }

    return 0;
}

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
            fprintf(stderr, "Error writing FPGA device: %s\n",
                    strerror(errno));
        else
            fprintf(stderr, "Short write to FPGA device\n");
        return -1;
    }

    return 0;
}

static int read_stopwatch(int *MM, int *SS, int *DD)
{
    int fd;
    ssize_t n;
    char buffer[32];

    fd = open(STOPWATCH_DEV, O_RDONLY);
    if (fd == -1)
    {
        fprintf(stderr, "Error opening %s: %s\n",
                STOPWATCH_DEV, strerror(errno));
        return -1;
    }

    n = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);

    if (n <= 0)
    {
        if (n < 0)
            fprintf(stderr, "Error reading %s: %s\n",
                    STOPWATCH_DEV, strerror(errno));
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

static int write_stopwatch_time(int MM, int SS, int DD)
{
    char command[16];

    snprintf(command, sizeof(command),
             "%02d:%02d:%02d", MM, SS, DD);

    return write_text_device(STOPWATCH_DEV, command);
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

    /*
     * Match the professor's sample:
     * move the cursor to a fixed column, print the time in red,
     * then reset the terminal color.
     */
    printf("\033[1A\033[20G\033[31m%02d:%02d:%02d\033[0m\n", MM, SS, DD);
}

static void make_question(int questions_answered, int *a, int *b)
{
    int maximum;

    /*
     * Increase difficulty as the game progresses:
     *   questions 1-5   : one-digit operands
     *   questions 6-10  : two-digit operands
     *   questions 11+   : three-digit operands
     */
    if (questions_answered < 5)
        maximum = 9;
    else if (questions_answered < 10)
        maximum = 99;
    else
        maximum = 999;

    *a = rand() % (maximum + 1);
    *b = rand() % (maximum + 1);
}

/*
 * Wait for an answer while periodically checking the hardware stopwatch.
 *
 * Return:
 *   1 = a complete line was received
 *   0 = stopwatch expired
 *  -1 = error / interrupted
 */
static int wait_for_answer(char *buffer, size_t size,
                           int *remaining_hundredths)
{
    while (!stop_program)
    {
        int MM, SS, DD;
        fd_set readfds;
        struct timeval timeout;
        int ready;

        if (read_stopwatch(&MM, &SS, &DD) != 0)
            return -1;

        *remaining_hundredths = hundredths_from_time(MM, SS, DD);

        if (*remaining_hundredths <= 0)
            return 0;

        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);

        timeout.tv_sec = 0;
        timeout.tv_usec = 10000; /* 10 ms */

        ready = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout);

        if (ready < 0)
        {
            if (errno == EINTR)
            {
                if (stop_program)
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

            /*
             * Re-read the stopwatch after the user presses Enter so an answer
             * entered after expiration is not accepted.
             */
            if (read_stopwatch(&MM, &SS, &DD) != 0)
                return -1;

            *remaining_hundredths = hundredths_from_time(MM, SS, DD);

            if (*remaining_hundredths <= 0)
                return 0;

            return 1;
        }
    }

    return -1;
}

static int display_lives(int lives)
{
    unsigned int pattern;
    char led_command[16];

    if (lives <= 0)
        pattern = 0x0;
    else
        pattern = lives & 0x3; // LEDR display is 11 -> 10 -> 01 -> 00(all OFF)

    snprintf(led_command, sizeof(led_command), "%03X", pattern);

    return write_fpga_device(LEDR_DEV_PRIMARY,
                             LEDR_DEV_FALLBACK,
                             led_command);
}

static void cleanup_devices(void)
{
    write_text_device(STOPWATCH_DEV, "stop");
    write_text_device(STOPWATCH_DEV, "nodisp");
    write_fpga_device(LEDR_DEV_PRIMARY, LEDR_DEV_FALLBACK, "000");
}

int main(void)
{
    unsigned int keys;
    unsigned int switches;
    char led_command[16];

    int configured_MM = DEFAULT_MM;
    int configured_SS = DEFAULT_SS;
    int configured_DD = DEFAULT_DD;

    int configured_hundredths;
    int total_questions = 0;
    int score = 0;
    int correct_answers = 0;
    int wrong_answers = 0;
    int lives = STARTING_LIVES;
    long total_answer_hundredths = 0;

    signal(SIGINT, catchSIGINT);
    srand((unsigned int)time(NULL));

    /*
     * Setup phase:
     * show the default time, keep the stopwatch paused while the user edits
     * MM/SS/DD, and wait for KEY0 to start the game.
     */
    if (write_text_device(STOPWATCH_DEV, "disp") != 0 ||
        write_text_device(STOPWATCH_DEV, "stop") != 0 ||
        write_stopwatch_time(configured_MM,
                             configured_SS,
                             configured_DD) != 0)
    {
        cleanup_devices();
        return 1;
    }

    printf("Set stopwatch if desired. Press KEY0 to start\n");

    while (!stop_program)
    {
        if (read_hex_device(KEY_DEV_PRIMARY, KEY_DEV_FALLBACK, &keys) != 0)
        {
            cleanup_devices();
            return 1;
        }

        keys &= 0xF;

        if (keys == 0)
        {
            usleep(10000);
            continue;
        }

        if (keys & KEY0)
            break;

        if (keys & (KEY1 | KEY2 | KEY3))
        {
            if (read_hex_device(SW_DEV_PRIMARY, SW_DEV_FALLBACK,
                                &switches) != 0)
            {
                cleanup_devices();
                return 1;
            }

            switches &= 0x3FF;

            snprintf(led_command, sizeof(led_command),
                     "%03X", switches);

            if (write_fpga_device(LEDR_DEV_PRIMARY,
                                  LEDR_DEV_FALLBACK,
                                  led_command) != 0)
            {
                cleanup_devices();
                return 1;
            }

            if (keys & KEY1)
                configured_DD = (switches > 99) ? 99 : (int)switches;

            if (keys & KEY2)
                configured_SS = (switches > 59) ? 59 : (int)switches;

            if (keys & KEY3)
                configured_MM = (switches > 59) ? 59 : (int)switches;

            if (write_stopwatch_time(configured_MM,
                                     configured_SS,
                                     configured_DD) != 0)
            {
                cleanup_devices();
                return 1;
            }
        }

        usleep(10000);
    }

    if (stop_program)
    {
        cleanup_devices();
        return 0;
    }

    /*
     * Game phase: LEDR now represents remaining lives instead of SW.
     * 3 lives = LEDR2..0 = 111, 2 lives = 011, 1 life = 001.
     */
    if (display_lives(lives) != 0)
    {
        cleanup_devices();
        return 1;
    }

    configured_hundredths =
        hundredths_from_time(configured_MM,
                             configured_SS,
                             configured_DD);

    /*
     * A zero configured time cannot provide a question interval. It simply
     * expires immediately, which is consistent with the stopwatch behavior.
     */
    while (!stop_program)
    {
        int a, b;
        int correct_answer;
        int remaining_hundredths = 0;
        int question_finished = 0;
        char input[128];

        make_question(correct_answers, &a, &b);
        total_questions++;
        correct_answer = a + b;

        if (write_text_device(STOPWATCH_DEV, "stop") != 0 ||
            write_stopwatch_time(configured_MM,
                                 configured_SS,
                                 configured_DD) != 0 ||
            write_text_device(STOPWATCH_DEV, "run") != 0)
        {
            cleanup_devices();
            return 1;
        }

        printf("%d + %d = ", a, b);
        fflush(stdout);

        while (!question_finished && !stop_program)
        {
            int status;
            char *endptr;
            long answer;

            status = wait_for_answer(input, sizeof(input),
                                     &remaining_hundredths);

            if (status < 0)
            {
                if (!stop_program)
                    fprintf(stderr, "\nError while waiting for answer.\n");
                cleanup_devices();
                return stop_program ? 0 : 1;
            }

            if (status == 0)
            {
                double average_seconds =
                    (correct_answers > 0)
                        ? ((double)total_answer_hundredths /
                           (double)correct_answers / 100.0)
                        : 0.0;

                write_text_device(STOPWATCH_DEV, "stop");

                printf("\nTime expired!\n");
                printf("Total questions asked: %d\n", total_questions);
                printf("Total score earned: %d\n", score);
                printf("Correct answers: %d\n", correct_answers);
                printf("Total wrong answer attempts: %d\n", wrong_answers);
                printf("Remaining lives: %d\n", lives);
                printf("Average response time: %.2f seconds\n",
                       average_seconds);

                cleanup_devices();
                return 0;
            }

            /*
             * Match the professor's sample output: after every submitted
             * answer, print the stopwatch value that remained when Enter
             * was pressed. This happens for both correct and incorrect
             * answers.
             */
            print_remaining_time(remaining_hundredths);

            errno = 0;
            answer = strtol(input, &endptr, 10);

            while (*endptr == ' ' || *endptr == '\t')
                endptr++;

            if (errno == 0 &&
                endptr != input &&
                (*endptr == '\n' || *endptr == '\0') &&
                answer == correct_answer)
            {

                int elapsed_hundredths =
                    configured_hundredths - remaining_hundredths;

                if (elapsed_hundredths < 0)
                    elapsed_hundredths = 0;

                write_text_device(STOPWATCH_DEV, "stop");

                total_answer_hundredths += elapsed_hundredths;
                correct_answers++;
                score++;
                question_finished = 1;
            }
            else
            {
                double average_seconds;

                wrong_answers++;
                lives--;

                if (display_lives(lives) != 0)
                {
                    cleanup_devices();
                    return 1;
                }

                if (lives <= 0)
                {
                    write_text_device(STOPWATCH_DEV, "stop");

                    average_seconds =
                        (correct_answers > 0)
                            ? ((double)total_answer_hundredths /
                               (double)correct_answers / 100.0)
                            : 0.0;

                    printf("\nNo lives remaining!\n");
                    printf("Total questions asked: %d\n",
                           total_questions);
                    printf("Total score earned: %d\n", score);
                    printf("Correct answers: %d\n", correct_answers);
                    printf("Total wrong answer attempts: %d\n", wrong_answers);
                    printf("Remaining lives: %d\n", lives);
                    printf("Average response time: %.2f seconds\n", average_seconds);

                    cleanup_devices();
                    return 0;
                }

                printf("Try again: ");
                fflush(stdout);
            }
        }
    }

    cleanup_devices();
    return 0;
}
