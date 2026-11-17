#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/jiffies.h>
#include <asm/io.h>
#include "../include/address_map_arm.h"

#define DEV_NAME "video"
#define VIDEO_SIZE 64
#define BUFFER_REG 0
#define BACKBUFFER_REG 4
#define RESOLUTION_REG 8
#define STATUS_REG 12

/* DE1-SoC: 16-bit pixels; each row occupies 1024 bytes. */
#define ROW_STRIDE 1024
#define FRAMEBUFFER_BYTES (256 * ROW_STRIDE)

static void __iomem *lw_virtual;
static void __iomem *pixel_ctrl;
static void __iomem *onchip_buffer;
static void __iomem *sdram_buffer;
static void __iomem *draw_buffer;
static int resolution_x;
static int resolution_y;
static DEFINE_MUTEX(video_mutex);

static int video_open(struct inode *inode, struct file *file)
{
    return 0;
}

static int video_release(struct inode *inode, struct file *file)
{
    return 0;
}

static ssize_t video_read(struct file *file, char __user *buffer,
                          size_t length, loff_t *offset)
{
    char message[16];
    int message_length;
    size_t n;

    message_length = scnprintf(message, sizeof(message), "%03d %03d\n",
                               resolution_x, resolution_y);
    if (*offset >= message_length)
        return 0;
    n = message_length - *offset;
    if (n > length)
        n = length;
    if (copy_to_user(buffer, message + *offset, n))
        return -EFAULT;
    *offset += n;
    return n;
}

static inline void plot_pixel(int x, int y, u16 color)
{
    unsigned int offset = (y << 10) + (x << 1);
    iowrite16(color, (u8 __iomem *)draw_buffer + offset);
}

static void clear_screen(void)
{
    int x, y;
    for (y = 0; y < resolution_y; y++)
        for (x = 0; x < resolution_x; x++)
            plot_pixel(x, y, 0);
}

static void draw_box(int x0, int y0, int x1, int y1, u16 color)
{
    int x, y;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
            plot_pixel(x, y, color);
}

/* Bresenham's integer line drawing, inclusive of both endpoints. */
static void draw_line(int x0, int y0, int x1, int y1, u16 color)
{
    int dx, dy, sx, sy, err, e2;

    dx = (x1 >= x0) ? x1 - x0 : x0 - x1;
    dy = (y1 >= y0) ? y1 - y0 : y0 - y1;
    sx = x0 < x1 ? 1 : -1;
    sy = y0 < y1 ? 1 : -1;
    err = dx - dy;

    for (;;) {
        plot_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1)
            break;
        e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/* Wait for buffer swap at VSYNC. Fail instead of hanging forever. */
static int swap_buffers(void)
{
    unsigned long deadline = jiffies + HZ;
    u32 back_addr;

    iowrite32(1, (u8 __iomem *)pixel_ctrl + BUFFER_REG);
    while (ioread32((u8 __iomem *)pixel_ctrl + STATUS_REG) & 1) {
        if (time_after(jiffies, deadline))
            return -ETIMEDOUT;
        cpu_relax();
    }

    back_addr = ioread32((u8 __iomem *)pixel_ctrl + BACKBUFFER_REG);
    if (back_addr == FPGA_ONCHIP_BASE)
        draw_buffer = onchip_buffer;
    else if (back_addr == SDRAM_BASE)
        draw_buffer = sdram_buffer;
    else
        return -EIO;
    return 0;
}

static bool valid_pixel(int x, int y)
{
    return x >= 0 && x < resolution_x &&
           y >= 0 && y < resolution_y;
}

static ssize_t video_write(struct file *file, const char __user *buffer,
                           size_t length, loff_t *offset)
{
    char str[VIDEO_SIZE + 1];
    char command[8];
    int x0, y0, x1, y1;
    unsigned int color;
    int parsed, ret = 0;

    if (length == 0)
        return 0;
    if (length > VIDEO_SIZE)
        return -EINVAL;
    if (copy_from_user(str, buffer, length))
        return -EFAULT;
    str[length] = '\0';
    if (sscanf(str, "%7s", command) != 1)
        return -EINVAL;

    if (mutex_lock_interruptible(&video_mutex))
        return -ERESTARTSYS;

    if (!strcmp(command, "clear")) {
        clear_screen();
    } else if (!strcmp(command, "sync")) {
        ret = swap_buffers();
    } else if (!strcmp(command, "pixel")) {
        parsed = sscanf(str, "%7s %d,%d %x", command,
                        &x0, &y0, &color);
        if (parsed != 4 || !valid_pixel(x0, y0) || color > 0xFFFF)
            ret = -EINVAL;
        else
            plot_pixel(x0, y0, (u16)color);
    } else if (!strcmp(command, "line") || !strcmp(command, "box")) {
        bool box = !strcmp(command, "box");
        parsed = sscanf(str, "%7s %d,%d %d,%d %x", command,
                        &x0, &y0, &x1, &y1, &color);
        if (parsed != 6 || !valid_pixel(x0, y0) ||
            !valid_pixel(x1, y1) || color > 0xFFFF) {
            ret = -EINVAL;
        } else if (box) {
            if (x0 > x1 || y0 > y1)
                ret = -EINVAL;
            else
                draw_box(x0, y0, x1, y1, (u16)color);
        } else {
            draw_line(x0, y0, x1, y1, (u16)color);
        }
    } else {
        ret = -EINVAL;
    }

    mutex_unlock(&video_mutex);
    return ret ? ret : length;
}

static const struct file_operations video_fops = {
    .owner = THIS_MODULE,
    .open = video_open,
    .release = video_release,
    .read = video_read,
    .write = video_write,
};

static struct miscdevice video_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = DEV_NAME,
    .fops = &video_fops,
    .mode = 0666,
};

static int __init start_video(void)
{
    u32 res;
    int ret;

    lw_virtual = ioremap_nocache(LW_BRIDGE_BASE, LW_BRIDGE_SPAN);
    if (!lw_virtual)
        return -ENOMEM;
    pixel_ctrl = (u8 __iomem *)lw_virtual + PIXEL_BUF_CTRL_BASE;

    res = ioread32((u8 __iomem *)pixel_ctrl + RESOLUTION_REG);
    resolution_x = res & 0xffff;
    resolution_y = (res >> 16) & 0xffff;
    if (resolution_x < 1 || resolution_x > 512 ||
        resolution_y < 1 || resolution_y > 256) {
        pr_err("video: unsupported resolution %dx%d\n",
               resolution_x, resolution_y);
        iounmap(lw_virtual);
        return -EINVAL;
    }

    onchip_buffer = ioremap_nocache(FPGA_ONCHIP_BASE,
                                    FRAMEBUFFER_BYTES);
    if (!onchip_buffer) {
        iounmap(lw_virtual);
        return -ENOMEM;
    }
    sdram_buffer = ioremap_nocache(SDRAM_BASE, FRAMEBUFFER_BYTES);
    if (!sdram_buffer) {
        iounmap(onchip_buffer);
        iounmap(lw_virtual);
        return -ENOMEM;
    }

    /* Initialize BOTH memories before either can become visible. */
    draw_buffer = onchip_buffer;
    clear_screen();
    draw_buffer = sdram_buffer;
    clear_screen();

    /* Set a known front buffer (on-chip), regardless of prior state. */
    iowrite32(FPGA_ONCHIP_BASE,
              (u8 __iomem *)pixel_ctrl + BACKBUFFER_REG);
    ret = swap_buffers();
    if (ret) {
        iowrite32(FPGA_ONCHIP_BASE,
                  (u8 __iomem *)pixel_ctrl + BACKBUFFER_REG);
        iounmap(sdram_buffer);
        iounmap(onchip_buffer);
        iounmap(lw_virtual);
        return ret;
    }

    /* Use SDRAM as the initial back buffer. */
    iowrite32(SDRAM_BASE,
              (u8 __iomem *)pixel_ctrl + BACKBUFFER_REG);
    draw_buffer = sdram_buffer;

    ret = misc_register(&video_dev);
    if (ret) {
        iowrite32(FPGA_ONCHIP_BASE,
                  (u8 __iomem *)pixel_ctrl + BACKBUFFER_REG);
        iounmap(sdram_buffer);
        iounmap(onchip_buffer);
        iounmap(lw_virtual);
        return ret;
    }
    pr_info("video: %dx%d double-buffered (on-chip + SDRAM)\n",
            resolution_x, resolution_y);
    return 0;
}

static void __exit stop_video(void)
{
    misc_deregister(&video_dev);

    /* Leave the display using the original FPGA on-chip buffer. */
    iowrite32(FPGA_ONCHIP_BASE,
              (u8 __iomem *)pixel_ctrl + BACKBUFFER_REG);
    if (!swap_buffers())
        iowrite32(FPGA_ONCHIP_BASE,
                  (u8 __iomem *)pixel_ctrl + BACKBUFFER_REG);
    iounmap(sdram_buffer);
    iounmap(onchip_buffer);
    iounmap(lw_virtual);
    pr_info("video: driver removed\n");
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ECE1718 Lab 6 Part IV VGA driver with double buffering");
module_init(start_video);
module_exit(stop_video);
