
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>
#include <sys/select.h>

#define KEY_BYTES 2
#define SW_BYTES 4
#define STOPWATCH_BYTES 9
#define INPUT_BYTES 128

volatile sig_atomic_t stop = 0;

void catchSIGINT(int signum)
{
    (void)signum;
    stop = 1;
}

/*
 * Part IV - Math Quiz Game
 *
 * Before game:
 * KEY0: Start game
 * KEY1: Set DD from SW (max 99)
 * KEY2: Set SS from SW (max 59)
 * KEY3: Select mode using MM (0=+, 1=-, 2=*, 3=mixed)
 *
 * After KEY0:
 * - Stopwatch starts counting down
 * - KEY inputs no longer modify the time
 * - Generate questions using the selected arithmetic mode
 * - Correct answer resets stopwatch
 * - Incorrect numeric answer costs one life (3 lives total)
 * - LEDR2:0 display remaining lives; zero lives ends the game
 * - Timeout ends the game
 * - Display statistics at the end
 */

int main(void)
{
    int KEY_FD, SW_FD, LEDR_FD, STOPWATCH_FD;

    char KEY_buffer[KEY_BYTES + 1];
    char SW_buffer[SW_BYTES + 1];
    char STOPWATCH_buffer[STOPWATCH_BYTES + 1];
    char TIME_buffer[16];

    // Save configured stopwatch time
    char initial_time[STOPWATCH_BYTES + 1];
    int initial_ticks = 0;
    int game_mode = 0; // 0: addition, 1: subtraction, 2: multiplication, 3: mixed

    int ret_val, chars_read;
    unsigned int KEY_value, SW_value;
    int MM, SS, DD;

    int game_start = 0;
    int game_over = 0;
    int exit_status = 0;

    int lives = 3;
    int correct_count = 0;
    double total_time = 0.0;

    signal(SIGINT, catchSIGINT);
    srand((unsigned int)time(NULL));

    /* ====================================
     * Open character device drivers
     * ==================================== */

    if ((KEY_FD = open("/dev/KEY", O_RDONLY)) == -1) {
        printf("Error opening /dev/KEY: %s\n", strerror(errno));
        return 1;
    }

    if ((SW_FD = open("/dev/SW", O_RDONLY)) == -1) {
        printf("Error opening /dev/SW: %s\n", strerror(errno));
        close(KEY_FD);
        return 1;
    }

    if ((LEDR_FD = open("/dev/LEDR", O_WRONLY)) == -1) {
        printf("Error opening /dev/LEDR: %s\n", strerror(errno));
        close(KEY_FD);
        close(SW_FD);
        return 1;
    }

    if ((STOPWATCH_FD = open("/dev/stopwatch", O_RDWR)) == -1) {
        printf("Error opening /dev/stopwatch: %s\n", strerror(errno));
        close(KEY_FD);
        close(SW_FD);
        close(LEDR_FD);
        return 1;
    }

    /* ====================================
     * Initialize stopwatch
     * ==================================== */

    if (write(STOPWATCH_FD, "stop", 4) != 4 ||
        write(STOPWATCH_FD, "00:10:00", 8) != 8 ||
        write(STOPWATCH_FD, "disp", 4) != 4) {

        perror("Initialize stopwatch");
        stop = 1;
        exit_status = 1;
    }

    if (!stop) {
        printf("Set KEY3/MM to 0:+, 1:-, 2:*, or 3:mixed.\n");
        printf("Set KEY2/SS and KEY1/DD for the question timer. Press KEY0 to start.\n");
        fflush(stdout);
    }

    /* ====================================
     * Phase 1: Configure stopwatch
     * ==================================== */

    while (!stop && !game_start) {

        // Read KEY edge-capture until EOF
        chars_read = 0;
        ret_val = 0;

        while ((ret_val = read(KEY_FD,
                               KEY_buffer + chars_read,
                               KEY_BYTES - chars_read)) > 0) {
            chars_read += ret_val;
        }

        if (stop)
            break;

        if (ret_val < 0) {
            printf("Error reading KEY: %s\n", strerror(errno));
            exit_status = 1;
            break;
        }

        KEY_buffer[chars_read] = '\0';

        KEY_value = 0;
        sscanf(KEY_buffer, "%X", &KEY_value);

        /* KEY0: Start game */
        if (KEY_value & 0x1) {

            // Save current stopwatch time
            chars_read = 0;
            ret_val = 0;

            while ((ret_val = read(STOPWATCH_FD, initial_time + chars_read, STOPWATCH_BYTES - chars_read)) > 0) {
                chars_read += ret_val;
            }

            if (stop)
                break;

            if (ret_val < 0) {
                perror("Read initial stopwatch");
                exit_status = 1;
                break;
            }

            initial_time[chars_read] = '\0';

            if (sscanf(initial_time, "%d:%d:%d",
                       &MM, &SS, &DD) != 3) {
                printf("Invalid initial stopwatch time\n");
                exit_status = 1;
                break;
            }

            // During configuration, MM represents the game mode,
            // not the countdown minutes.
            if (MM < 0 || MM > 3) {
                printf("Invalid mode: set MM to 00, 01, 02 or 03.\n");
                continue;
            }
            game_mode = MM;
            initial_ticks = SS * 100 + DD;

            if (initial_ticks <= 0) {
                printf("Please set a nonzero time using SS/DD.\n");
                continue;
            }

            // Convert configuration MM:SS:DD to actual timer 00:SS:DD.
            snprintf(initial_time, sizeof(initial_time), "00:%02d:%02d", SS, DD);
            if (write(STOPWATCH_FD, initial_time, strlen(initial_time)) !=
                (ssize_t)strlen(initial_time)) {
                perror("Set countdown stopwatch");
                exit_status = 1;
                break;
            }

            // Start stopwatch
            if (write(STOPWATCH_FD, "run", 3) != 3) {
                perror("Start stopwatch");
                exit_status = 1;
                break;
            }

            // Three lives: LEDR2, LEDR1, LEDR0 are all on.
            if (write(LEDR_FD, "007", 3) != 3) {
                perror("Initialize lives on LEDR");
                exit_status = 1;
                break;
            }

            game_start = 1;
        }

        /* KEY1 / KEY2 / KEY3 */
        if ((KEY_value & 0xE) && !game_start) {

            // Read SW
            chars_read = 0;
            ret_val = 0;

            while ((ret_val = read(SW_FD,
                                   SW_buffer + chars_read,
                                   SW_BYTES - chars_read)) > 0) {
                chars_read += ret_val;
            }

            if (stop)
                break;

            if (ret_val < 0) {
                printf("Error reading SW: %s\n", strerror(errno));
                exit_status = 1;
                break;
            }

            SW_buffer[chars_read] = '\0';

            SW_value = 0;
            sscanf(SW_buffer, "%X", &SW_value);

            // Display SW value on LEDR
            if (write(LEDR_FD, SW_buffer,
                      strlen(SW_buffer)) < 0) {
                perror("Write LEDR");
                exit_status = 1;
                break;
            }

            // Read current stopwatch
            chars_read = 0;
            ret_val = 0;

            while ((ret_val = read(STOPWATCH_FD,
                                   STOPWATCH_buffer + chars_read,
                                   STOPWATCH_BYTES - chars_read)) > 0) {
                chars_read += ret_val;
            }

            if (stop)
                break;

            if (ret_val < 0) {
                printf("Error reading stopwatch: %s\n",
                       strerror(errno));
                exit_status = 1;
                break;
            }

            STOPWATCH_buffer[chars_read] = '\0';

            if (sscanf(STOPWATCH_buffer, "%d:%d:%d",
                       &MM, &SS, &DD) != 3) {
                printf("Invalid stopwatch time\n");
                exit_status = 1;
                break;
            }

            // KEY1: Set hundredths
            if (KEY_value & 0x2)
                DD = SW_value > 99 ? 99 : SW_value;

            // KEY2: Set seconds
            if (KEY_value & 0x4)
                SS = SW_value > 59 ? 59 : SW_value;

            // KEY3: Set arithmetic mode (00 to 03) using MM display
            if (KEY_value & 0x8)
                MM = SW_value > 3 ? 3 : SW_value;

            // Format updated time
            sprintf(TIME_buffer, "%02d:%02d:%02d",
                    MM, SS, DD);

            if (write(STOPWATCH_FD, TIME_buffer,
                      strlen(TIME_buffer)) < 0) {
                perror("Write stopwatch");
                exit_status = 1;
                break;
            }
        }

        usleep(10000);
    }

    /* ====================================
     * Phase 2: Math Quiz Game
     * ==================================== */

    if (!stop && game_start && !exit_status) {

        const char *mode_names[] = {"Addition", "Subtraction", "Multiplication", "Mixed"};
        printf("\nGame started! Mode: %s; time per question: %.2f seconds; lives: %d\n",
               mode_names[game_mode], initial_ticks / 100.0, lives);

        while (!stop && !game_over && !exit_status) {

            int a, b, answer, correct_answer;
            int operation = (game_mode == 3) ? rand() % 3 : game_mode;
            char operator_symbol;
            int remaining_ticks;
            int question_done = 0;
            char input[INPUT_BYTES];

            // Generate random math question
            if (correct_count < 5) {
                a = rand() % 10;
                b = rand() % 10;
            }
            else if (correct_count < 12) {
                a = rand() % 100;
                b = rand() % 10;
            }
            else {
                a = rand() % 1000;
                b = rand() % 100;
            }

            // Multiplication gets smaller operands as difficulty increases.
            if (operation == 2) {
                if (correct_count < 5) {
                    a = rand() % 10;
                    b = rand() % 10;
                } else if (correct_count < 12) {
                    a = rand() % 20;
                    b = rand() % 10;
                } else {
                    a = rand() % 30;
                    b = rand() % 20;
                }
            }

            // Keep subtraction answers nonnegative.
            if (operation == 1 && a < b) {
                int temp = a;
                a = b;
                b = temp;
            }

            switch (operation) {
                case 0:
                    operator_symbol = '+';
                    correct_answer = a + b;
                    break;
                case 1:
                    operator_symbol = '-';
                    correct_answer = a - b;
                    break;
                default:
                    operator_symbol = '*';
                    correct_answer = a * b;
                    break;
            }

            printf("%d %c %d = ", a, operator_symbol, b);
            fflush(stdout);

            /* Wait for correct answer or timeout */
            while (!stop && !game_over &&
                   !question_done && !exit_status) {

                // Read stopwatch time
                chars_read = 0;
                ret_val = 0;

                while ((ret_val = read(STOPWATCH_FD,
                                       STOPWATCH_buffer + chars_read,
                                       STOPWATCH_BYTES - chars_read)) > 0) {
                    chars_read += ret_val;
                }

                if (stop)
                    break;

                if (ret_val < 0) {
                    perror("Read stopwatch");
                    exit_status = 1;
                    break;
                }

                STOPWATCH_buffer[chars_read] = '\0';

                if (sscanf(STOPWATCH_buffer, "%d:%d:%d",
                           &MM, &SS, &DD) != 3) {
                    printf("Invalid stopwatch time\n");
                    exit_status = 1;
                    break;
                }

                remaining_ticks = (MM * 60 + SS) * 100 + DD;

                // Check timeout
                if (remaining_ticks == 0) {
                    printf("\nTime expired! ");
                    game_over = 1;
                    break;
                }

                // Check terminal input every 10 ms
                fd_set readfds;
                struct timeval timeout;

                FD_ZERO(&readfds);
                FD_SET(STDIN_FILENO, &readfds);

                timeout.tv_sec = 0;
                timeout.tv_usec = 10000;

                int ready = select(STDIN_FILENO + 1,
                                   &readfds, NULL, NULL,
                                   &timeout);

                if (ready < 0) {
                    if (errno == EINTR)
                        continue;

                    perror("select");
                    exit_status = 1;
                    break;
                }

                if (ready == 0)
                    continue;

                // Read user answer
                if (fgets(input, sizeof(input), stdin) == NULL) {
                    if (stop)
                        break;

                    printf("\nInput closed.\n");
                    game_over = 1;
                    break;
                }

                // Recheck stopwatch after input
                chars_read = 0;
                ret_val = 0;

                while ((ret_val = read(STOPWATCH_FD,
                                       STOPWATCH_buffer + chars_read,
                                       STOPWATCH_BYTES - chars_read)) > 0) {
                    chars_read += ret_val;
                }

                if (stop)
                    break;

                if (ret_val < 0) {
                    perror("Read stopwatch");
                    exit_status = 1;
                    break;
                }

                STOPWATCH_buffer[chars_read] = '\0';

                if (sscanf(STOPWATCH_buffer, "%d:%d:%d",
                           &MM, &SS, &DD) != 3) {
                    printf("Invalid stopwatch time\n");
                    exit_status = 1;
                    break;
                }

                remaining_ticks = (MM * 60 + SS) * 100 + DD;

                if (remaining_ticks == 0) {
                    printf("\nTime expired! ");
                    game_over = 1;
                    break;
                }

                // Parse answer
                if (sscanf(input, "%d", &answer) != 1) {
                    printf("Invalid input. Try again: ");
                    fflush(stdout);
                    continue;
                }

                if (answer == correct_answer) {

                    // Calculate elapsed time
                    double elapsed = (initial_ticks - remaining_ticks) / 100.0;

                    total_time += elapsed;
                    correct_count++;

                    // Reset stopwatch
                    if (write(STOPWATCH_FD, "stop", 4) != 4 ||
                        write(STOPWATCH_FD, initial_time,
                              strlen(initial_time)) !=
                              (ssize_t)strlen(initial_time) ||
                        write(STOPWATCH_FD, "run", 3) != 3) {

                        perror("Reset stopwatch");
                        exit_status = 1;
                        break;
                    }

                    question_done = 1;

                } else {
                    const char *led_value;
                    lives--;

                    // Explicit LEDR life display: 2 -> 003, 1 -> 001, 0 -> 000.
                    if (lives == 2)
                        led_value = "003";
                    else if (lives == 1)
                        led_value = "001";
                    else
                        led_value = "000";
                    if (write(LEDR_FD, led_value, 3) != 3) {
                        perror("Update lives on LEDR");
                        exit_status = 1;
                        break;
                    }

                    if (lives == 0) {
                        printf("Wrong! No lives remaining. Game over!\n");
                        game_over = 1;
                        break;
                    }

                    printf("Wrong! %d %s remaining. Try again: ",
                           lives, lives == 1 ? "life" : "lives");
                    fflush(stdout);
                }
            }
        }

        // Display statistics
        if (!exit_status) {
            double average = correct_count > 0
                           ? total_time / correct_count
                           : 0.0;

            printf("You answered %d questions, "
                   "in an average of %.2f seconds.\n",
                   correct_count, average);
            printf("Lives remaining: %d\n", lives);
        }
    }

    /* ====================================
     * Cleanup
     * ==================================== */

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

    printf("Program exited.\n");

    return exit_status;
}
