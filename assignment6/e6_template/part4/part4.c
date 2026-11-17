#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#define NUM_OBJECTS 8
#define BOX_SIZE 6
#define VIDEO_BYTES 8
#define WHITE 0xFFFF

struct object {
    int x, y;                 /* top-left corner */
    int dx, dy;               /* each is +1 or -1 */
    unsigned int color;
};

static volatile sig_atomic_t stop = 0;
static struct object objects[NUM_OBJECTS];
static int screen_x, screen_y;

static void handle_sigint(int signo)
{
    (void)signo;
    stop = 1;
}

static int send_command(int fd, const char *command)
{
    size_t len = strlen(command);
    ssize_t n;

    /* The driver accepts one command per write. */
    do {
        n = write(fd, command, len);
    } while (n < 0 && errno == EINTR && !stop);

    if (n != (ssize_t)len) {
        if (n >= 0)
            fprintf(stderr, "Short write to /dev/video\n");
        else
            perror("write /dev/video");
        return -1;
    }
    return 0;
}

static int get_resolution(int fd)
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
        fprintf(stderr, "Invalid VGA resolution: %s\n", buf);
        return -1;
    }
    return 0;
}

static void initialize_objects(void)
{
    static const unsigned int colors[NUM_OBJECTS] = {
        0xF800, 0xFFE0, 0x07E0, 0x07FF,
        0x001F, 0xF81F, 0xFD20, 0xFFFF
    };
    int i;

    srand((unsigned int)time(NULL));
    for (i = 0; i < NUM_OBJECTS; i++) {
        objects[i].x = rand() % (screen_x - BOX_SIZE + 1);
        objects[i].y = rand() % (screen_y - BOX_SIZE + 1);
        objects[i].dx = (rand() % 2) ? 1 : -1;
        objects[i].dy = (rand() % 2) ? 1 : -1;
        objects[i].color = colors[i];
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

static int draw_box_command(int fd, const struct object *obj)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "box %d,%d %d,%d %X\n",
             obj->x, obj->y,
             obj->x + BOX_SIZE - 1, obj->y + BOX_SIZE - 1,
             obj->color);
    return send_command(fd, cmd);
}

/* Render the complete next frame into the back buffer. */
static int draw_frame(int fd)
{
    int i;
    int half = BOX_SIZE / 2;

    if (send_command(fd, "clear") < 0)
        return -1;

    /* Draw connections first so the colored rectangles remain visible. */
    for (i = 0; i < NUM_OBJECTS; i++) {
        int next = (i + 1) % NUM_OBJECTS;

        if (draw_line_command(fd,
                objects[i].x + half,
                objects[i].y + half,
                objects[next].x + half,
                objects[next].y + half,
                WHITE) < 0) {
            return -1;
        }
    }

    for (i = 0; i < NUM_OBJECTS; i++) {
        if (draw_box_command(fd, &objects[i]) < 0)
            return -1;
    }
    return 0;
}

static void move_objects(void)
{
    int i;
    for (i = 0; i < NUM_OBJECTS; i++) {
        struct object *obj = &objects[i];
        int next_x = obj->x + obj->dx;
        int next_y = obj->y + obj->dy;

        if (next_x < 0 || next_x > screen_x - BOX_SIZE) {
            obj->dx = -obj->dx;
            next_x = obj->x + obj->dx;
        }
        if (next_y < 0 || next_y > screen_y - BOX_SIZE) {
            obj->dy = -obj->dy;
            next_y = obj->y + obj->dy;
        }
        obj->x = next_x;
        obj->y = next_y;
    }
}

int main(void)
{
    int fd;
    int failed = 0;

    signal(SIGINT, handle_sigint);
    fd = open("/dev/video", O_RDWR);
    if (fd < 0) {
        perror("open /dev/video");
        return EXIT_FAILURE;
    }
    if (get_resolution(fd) < 0) {
        close(fd);
        return EXIT_FAILURE;
    }
    printf("VGA: %d x %d; Part IV, %d objects\n",
           screen_x, screen_y, NUM_OBJECTS);
    initialize_objects();

    while (!stop) {
        if (draw_frame(fd) < 0) {
            failed = 1;
            break;
        }
        /* Present the finished back buffer at vertical sync. */
        if (send_command(fd, "sync") < 0) {
            failed = 1;
            break;
        }
        if (stop)
            break;
        usleep(50000);
        move_objects();
    }


    /* Clear both buffers, not just whichever happens to be back. */
    if (send_command(fd, "clear") == 0 &&
        send_command(fd, "sync") == 0)
        send_command(fd, "clear");
    close(fd);
    puts("Part IV animation stopped");
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
