#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#define MAX_OBJECTS 16
#define INITIAL_OBJECTS 8
#define MIN_OBJECTS 1
#define BOX_SIZE 6
#define VIDEO_BYTES 8
#define WHITE 0xFFFF

/* Delay is in milliseconds, in addition to the VGA synchronization. */
#define INITIAL_DELAY_MS 100
#define MIN_DELAY_MS 0
#define MAX_DELAY_MS 300
#define DELAY_STEP_MS 20

typedef struct {
    int x, y;
    int dx, dy;
    unsigned int color;
} OBJECT;

static OBJECT objects[MAX_OBJECTS];
static int screen_x, screen_y;
static int object_count = INITIAL_OBJECTS;
static int delay_ms = INITIAL_DELAY_MS;
static volatile sig_atomic_t stop = 0;

static void handle_sigint(int signo)
{
    (void)signo;
    stop = 1;
}

/* The /dev/video driver accepts one complete command per write(). */
static int send_command(int fd, const char *command)
{
    size_t len = strlen(command);
    ssize_t n = write(fd, command, len);
    if (n != (ssize_t)len) {
        if (n < 0) perror("write /dev/video");
        else fprintf(stderr, "Short write to /dev/video\n");
        return -1;
    }
    return 0;
}

static int read_resolution(int fd)
{
    char buf[VIDEO_BYTES + 1];
    ssize_t n = read(fd, buf, VIDEO_BYTES);
    if (n <= 0) {
        perror("read /dev/video");
        return -1;
    }
    buf[n] = '\0';
    if (sscanf(buf, "%d %d", &screen_x, &screen_y) != 2 ||
        screen_x < BOX_SIZE || screen_y < BOX_SIZE) {
        fprintf(stderr, "Bad video resolution: %s\n", buf);
        return -1;
    }
    return 0;
}

/* KEY and SW character drivers return hexadecimal text.
 * Opening for each poll avoids depending on their EOF/file-offset policy.
 * For /dev/KEY, reading also acknowledges its captured press edges.
 */
static int read_hex_device(const char *path, unsigned int *value)
{
    int fd;
    ssize_t n;
    char buf[32];

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror(path);
        return -1;
    }
    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        fprintf(stderr, "Could not read %s\n", path);
        return -1;
    }
    buf[n] = '\0';
    if (sscanf(buf, "%x", value) != 1) {
        fprintf(stderr, "Invalid input from %s: %s\n", path, buf);
        return -1;
    }
    return 0;
}

static void initialize_objects(void)
{
    static const unsigned int colors[] = {
        0xF800, 0xFFE0, 0x07E0, 0x07FF,
        0x001F, 0xF81F, 0xFD20, 0xFFFF
    };
    int i;

    srand((unsigned int)time(NULL));
    for (i = 0; i < MAX_OBJECTS; ++i) {
        objects[i].x = rand() % (screen_x - BOX_SIZE + 1);
        objects[i].y = rand() % (screen_y - BOX_SIZE + 1);
        objects[i].dx = (rand() % 2) ? 1 : -1;
        objects[i].dy = (rand() % 2) ? 1 : -1;
        objects[i].color = colors[i % (int)(sizeof(colors) / sizeof(colors[0]))];
    }
}

static int draw_line_command(int fd, int x0, int y0,
                             int x1, int y1, unsigned int color)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "line %d,%d %d,%d %X\n",
             x0, y0, x1, y1, color);
    return send_command(fd, cmd);
}

static int draw_box_command(int fd, const OBJECT *obj)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "box %d,%d %d,%d %X\n",
             obj->x, obj->y,
             obj->x + BOX_SIZE - 1, obj->y + BOX_SIZE - 1,
             obj->color);
    return send_command(fd, cmd);
}

static int draw_frame(int fd, int show_lines)
{
    int i;
    const int center_offset = BOX_SIZE / 2;

    if (send_command(fd, "clear") < 0)
        return -1;

    /* Close the chain: last object connects back to object 0. */
    if (show_lines && object_count > 1) {
        for (i = 0; i < object_count; ++i) {
            int next = (i + 1) % object_count;
            if (draw_line_command(fd,
                    objects[i].x + center_offset,
                    objects[i].y + center_offset,
                    objects[next].x + center_offset,
                    objects[next].y + center_offset, WHITE) < 0)
                return -1;
        }
    }

    for (i = 0; i < object_count; ++i) {
        if (draw_box_command(fd, &objects[i]) < 0)
            return -1;
    }
    return 0;
}

static void move_objects(void)
{
    int i;
    for (i = 0; i < object_count; ++i) {
        OBJECT *obj = &objects[i];
        int nx = obj->x + obj->dx;
        int ny = obj->y + obj->dy;

        if (nx < 0 || nx > screen_x - BOX_SIZE) {
            obj->dx = -obj->dx;
            nx = obj->x + obj->dx;
        }
        if (ny < 0 || ny > screen_y - BOX_SIZE) {
            obj->dy = -obj->dy;
            ny = obj->y + obj->dy;
        }
        obj->x = nx;
        obj->y = ny;
    }
}

static void handle_keys(unsigned int keys)
{
    if ((keys & 0x1) && delay_ms > MIN_DELAY_MS) {
        delay_ms -= DELAY_STEP_MS;
        if (delay_ms < MIN_DELAY_MS) delay_ms = MIN_DELAY_MS;
    }
    if ((keys & 0x2) && delay_ms < MAX_DELAY_MS) {
        delay_ms += DELAY_STEP_MS;
        if (delay_ms > MAX_DELAY_MS) delay_ms = MAX_DELAY_MS;
    }
    if ((keys & 0x4) && object_count < MAX_OBJECTS)
        ++object_count;
    if ((keys & 0x8) && object_count > MIN_OBJECTS)
        --object_count;
}

static void sleep_delay(void)
{
    struct timespec req, rem;
    if (delay_ms == 0) return;
    req.tv_sec = delay_ms / 1000;
    req.tv_nsec = (long)(delay_ms % 1000) * 1000000L;
    while (!stop && nanosleep(&req, &rem) < 0 && errno == EINTR)
        req = rem;
}

int main(void)
{
    int video_fd;
    int failed = 0;
    unsigned int keys, switches;

    signal(SIGINT, handle_sigint);
    video_fd = open("/dev/video", O_RDWR);
    if (video_fd < 0) {
        perror("open /dev/video");
        return EXIT_FAILURE;
    }
    if (read_resolution(video_fd) < 0) {
        close(video_fd);
        return EXIT_FAILURE;
    }
    initialize_objects();
    printf("Part V: %dx%d, objects=%d, delay=%d ms\n",
           screen_x, screen_y, object_count, delay_ms);
    puts("KEY0 faster, KEY1 slower, KEY2 more, KEY3 fewer; SW != 0 hides lines");

    while (!stop) {
        if (read_hex_device("/dev/KEY", &keys) < 0 ||
            read_hex_device("/dev/SW", &switches) < 0) {
            failed = 1;
            break;
        }
        handle_keys(keys);

        /* Draw to BACK buffer; show lines only when every switch is off. */
        if (draw_frame(video_fd, switches == 0) < 0) {
            failed = 1;
            break;
        }
        if (send_command(video_fd, "sync") < 0) {
            failed = 1;
            break;
        }
        if (stop) break;

        move_objects();
        sleep_delay();
    }

    /* Present a blank frame, then clear the newly available back buffer. */
    if (send_command(video_fd, "clear") == 0 &&
        send_command(video_fd, "sync") == 0)
        send_command(video_fd, "clear");
    close(video_fd);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
