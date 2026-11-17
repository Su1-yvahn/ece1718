
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <asm/io.h>
#include "../include/address_map_arm.h"

/* Video display function prototypes */
void get_screen_specs(void);
void clear_screen(void);
void plot_pixel(int x, int y, short int color);
void draw_line(int x0, int y0, int x1, int y1,
               short int color);
void sync_video(void);

/* Character device function prototypes */
static int video_open(struct inode *, struct file *);
static int video_release(struct inode *, struct file *);
static ssize_t video_read(struct file *, char *,
                          size_t, loff_t *);
static ssize_t video_write(struct file *, const char *,
                           size_t, loff_t *);

#define SUCCESS 0
#define DEV_NAME "video"
#define VIDEO_SIZE 64

#define ABS(x) (((x) > 0) ? (x) : -(x))

/* File operations */
static struct file_operations fops = {
    .owner   = THIS_MODULE,
    .read    = video_read,
    .write   = video_write,
    .open    = video_open,
    .release = video_release
};

/* Miscellaneous character device */
static struct miscdevice video_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = DEV_NAME,
    .fops  = &fops,
    .mode  = 0666
};

/* FPGA memory mappings */
void __iomem *LW_virtual;
void __iomem *pixel_buffer;

int resolution_x, resolution_y;

#define PIXEL_CTRL_OFFSET 0
#define BACKBUFFER_OFFSET 4
#define RESOLUTION_OFFSET 8
#define STATUS_OFFSET 12

void __iomem *pixel_ctrl_ptr;

/* Initialize video driver */
static int __init start_video(void)
{
    int err;

    // Map FPGA lightweight bridge
    LW_virtual = ioremap_nocache(LW_BRIDGE_BASE,
                                 LW_BRIDGE_SPAN);

    if (LW_virtual == NULL) {
        printk(KERN_ERR "Failed to map LW bridge\n");
        return -ENOMEM;
    }

    // Access pixel buffer controller
    pixel_ctrl_ptr =
        (char __iomem *)LW_virtual + PIXEL_BUF_CTRL_BASE;

    // Read screen resolution
    get_screen_specs();

    // Map FPGA pixel buffer
    pixel_buffer = ioremap_nocache(FPGA_ONCHIP_BASE,
                                   FPGA_ONCHIP_SPAN);

    if (pixel_buffer == NULL) {
        printk(KERN_ERR "Failed to map pixel buffer\n");
        iounmap(LW_virtual);
        return -ENOMEM;
    }

    /*
     * Part III uses a single pixel buffer.
     *
     * Make the front and back buffer point to
     * the same FPGA on-chip memory address.
     */

    iowrite32(FPGA_ONCHIP_BASE,
              (char __iomem *)pixel_ctrl_ptr +
              BACKBUFFER_OFFSET);

    // Swap to make the front buffer use FPGA memory
    sync_video();

    // Set the back buffer to the same address
    iowrite32(FPGA_ONCHIP_BASE,
              (char __iomem *)pixel_ctrl_ptr +
              BACKBUFFER_OFFSET);

    // Clear screen
    clear_screen();

    // Register character device
    err = misc_register(&video_dev);

    if (err < 0) {
        printk(KERN_ERR "Failed to register /dev/video\n");
        iounmap(pixel_buffer);
        iounmap(LW_virtual);
        return err;
    }

    printk(KERN_INFO "/dev/video driver registered\n");

    return 0;
}

/* Read VGA screen resolution */
void get_screen_specs(void)
{
    unsigned int resolution_reg;

    resolution_reg = ioread32(
        (char __iomem *)pixel_ctrl_ptr +
        RESOLUTION_OFFSET);

    resolution_x = resolution_reg & 0xFFFF;
    resolution_y = (resolution_reg >> 16) & 0xFFFF;
}

/* Plot one pixel */
void plot_pixel(int x, int y, short int color)
{
    unsigned int offset;

    offset = (y << 10) + (x << 1);

    iowrite16((unsigned short)color,
              (char __iomem *)pixel_buffer + offset);
}

/* Clear the entire screen */
void clear_screen(void)
{
    int x, y;

    for (x = 0; x < resolution_x; x++) {
        for (y = 0; y < resolution_y; y++) {
            plot_pixel(x, y, 0);
        }
    }
}

/* Bresenham's line drawing algorithm */
void draw_line(int x0, int y0, int x1, int y1,
               short int color)
{
    int steep;
    int tmp;
    int deltax, deltay;
    int error;
    int ystep;
    int x, y;

    // Check whether the line is steep
    steep = ABS(y1 - y0) > ABS(x1 - x0);

    // Swap x and y if the line is steep
    if (steep) {
        tmp = x0;
        x0 = y0;
        y0 = tmp;

        tmp = x1;
        x1 = y1;
        y1 = tmp;
    }

    // Ensure x0 <= x1
    if (x0 > x1) {
        tmp = x0;
        x0 = x1;
        x1 = tmp;

        tmp = y0;
        y0 = y1;
        y1 = tmp;
    }

    // Calculate differences
    deltax = x1 - x0;
    deltay = ABS(y1 - y0);

    error = -(deltax / 2);

    y = y0;

    if (y0 < y1)
        ystep = 1;
    else
        ystep = -1;

    // Draw the line
    for (x = x0; x <= x1; x++) {

        if (steep)
            plot_pixel(y, x, color);
        else
            plot_pixel(x, y, color);

        error += deltay;

        if (error >= 0) {
            y += ystep;
            error -= deltax;
        }
    }
}

/*
 * Part III: VGA synchronization
 *
 * Write 1 to Buffer register to request a swap.
 * Wait until S bit in Status register becomes 0.
 */
void sync_video(void)
{
    // Request pixel buffer swap
    iowrite32(1,
              (char __iomem *)pixel_ctrl_ptr +
              PIXEL_CTRL_OFFSET);

    // Wait for synchronization to finish
    while (ioread32(
        (char __iomem *)pixel_ctrl_ptr +
        STATUS_OFFSET) & 0x1) {
        cpu_relax();
    }
}

/* Remove video driver */
static void __exit stop_video(void)
{
    misc_deregister(&video_dev);

    iounmap(pixel_buffer);
    iounmap(LW_virtual);

    printk(KERN_INFO "/dev/video driver removed\n");
}

/* Open device */
static int video_open(struct inode *inode,
                      struct file *file)
{
    return SUCCESS;
}

/* Close device */
static int video_release(struct inode *inode,
                         struct file *file)
{
    return SUCCESS;
}

/* Read screen resolution */
static ssize_t video_read(struct file *filp,
                          char *buffer,
                          size_t length,
                          loff_t *offset)
{
    char message[16];
    int message_length;
    size_t bytes;

    message_length = snprintf(message, sizeof(message),
                              "%03d %03d\n",
                              resolution_x, resolution_y);

    if (*offset >= message_length)
        return 0;

    bytes = message_length - *offset;

    if (bytes > length)
        bytes = length;

    if (copy_to_user(buffer,
                     message + *offset,
                     bytes) != 0)
        return -EFAULT;

    *offset += bytes;

    return bytes;
}

/* Write commands to video driver */
static ssize_t video_write(struct file *filp,
                           const char *buffer,
                           size_t length,
                           loff_t *offset)
{
    char video_string[VIDEO_SIZE + 1];
    char command[8];

    int x1, y1, x2, y2;
    unsigned int color;

    if (length == 0)
        return 0;

    if (length > VIDEO_SIZE)
        return -EINVAL;

    // Copy command from user space
    if (copy_from_user(video_string,
                       buffer, length) != 0)
        return -EFAULT;

    video_string[length] = '\0';

    // Read command name
    if (sscanf(video_string, "%7s", command) != 1)
        return -EINVAL;

    // Clear screen
    if (strcmp(command, "clear") == 0) {
        clear_screen();
    }

    // Part III: VGA synchronization
    else if (strcmp(command, "sync") == 0) {
        sync_video();
    }

    // Plot one pixel
    else if (strcmp(command, "pixel") == 0) {

        if (sscanf(video_string,
                   "%7s %d,%d %X",
                   command, &x1, &y1, &color) != 4)
            return -EINVAL;

        // Check coordinates
        if (x1 < 0 || x1 >= resolution_x ||
            y1 < 0 || y1 >= resolution_y)
            return -EINVAL;

        plot_pixel(x1, y1, (short int)color);
    }

    // Draw a line
    else if (strcmp(command, "line") == 0) {

        if (sscanf(video_string,
                   "%7s %d,%d %d,%d %X",
                   command,
                   &x1, &y1, &x2, &y2,
                   &color) != 6)
            return -EINVAL;

        // Check coordinates
        if (x1 < 0 || x1 >= resolution_x ||
            x2 < 0 || x2 >= resolution_x ||
            y1 < 0 || y1 >= resolution_y ||
            y2 < 0 || y2 >= resolution_y)
            return -EINVAL;

        draw_line(x1, y1, x2, y2,
                  (short int)color);
    }

    else {
        printk(KERN_ERR
               "/dev/video: Unknown command: %s\n",
               command);
        return -EINVAL;
    }

    return length;
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("VGA video driver - Lab 6 Part III");

module_init(start_video);
module_exit(stop_video);
