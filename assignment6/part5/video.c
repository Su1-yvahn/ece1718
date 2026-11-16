#include <linux/fs.h>         // struct file, struct file_operations
#include <linux/module.h>     // for module init and exit macros
#include <linux/miscdevice.h> // for misc_device_register and struct miscdev
#include <linux/uaccess.h>    // for copy_to_user, see code
#include <asm/io.h>           // for mmap
#include "../include/address_map_arm.h"

/* /dev/video: "clear", "pixel x,y color", and "line x1,y1 x2,y2 color", "box x1,y1 x2,y2 color", and "sync".
 * Reading from the /dev/video produces the string "screen_x screen_y", where these are
 * the number of columns and rows on the screen, respectively. */

/* Prototypes for functions used for video display */
void get_screen_specs(volatile int *);
void clear_screen(void);
void draw_line(int, int, int, int, short int);
void draw_box(int, int, int, int, short int);
void plot_pixel(int, int, short int);
void wait_for_vsync(void);

// START of declarations needed for a character device
static int video_open(struct inode *, struct file *);
static int video_release(struct inode *, struct file *);
static ssize_t video_read(struct file *, char *, size_t, loff_t *);
static ssize_t video_write(struct file *, const char *, size_t, loff_t *);

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .read = video_read,
    .write = video_write,
    .open = video_open,
    .release = video_release};

#define ABS(x) (((x) > 0) ? (x) : -(x))

#define SUCCESS 0
#define DEV_NAME "video"
#define VIDEO_BYTES 8 // number of bytes to return when read (includes a \n)
#define VIDEO_SIZE 25 // max number of bytes accepted for a write

static struct miscdevice video_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = DEV_NAME,
    .fops = &fops,
    .mode = 0666};

static int video_registered = 0;
// END of declarations needed for a character device

// START of declarations for the video
void *LW_virtual;               // used to map physical addresses for the light-weight bridge
volatile int *pixel_ctrl_ptr;   // virtual address of pixel buffer DMA controller
int pixel_buffer;               // current drawing location (back buffer)
int fpga_pixel_buffer;          // virtual address of FPGA on-chip pixel buffer
int sdram_pixel_buffer;         // virtual address of SDRAM pixel buffer
int resolution_x, resolution_y; // VGA screen size
// The VGA character plane is independent of the VGA pixel plane.
// Clear stale Part 6 text when this video driver starts.
static void __iomem *char_buffer_virtual;
static int clear_character_plane(void)
{
    int x, y;
    char_buffer_virtual = ioremap_nocache(FPGA_CHAR_BASE, FPGA_CHAR_SPAN);
    if (!char_buffer_virtual)
        return -ENOMEM;
    for (y = 0; y < 60; ++y)
        for (x = 0; x < 80; ++x)
            iowrite8(' ', (char __iomem *)char_buffer_virtual + (y << 7) + x);
    return 0;
}
// END of declarations

/* Code to initialize the video driver */
static int __init start_video(void)
{
    int err = misc_register(&video_dev);
    if (err < 0)
    {
        printk(KERN_ERR "/dev/%s: misc_register() failed\n", DEV_NAME);
    }
    else
    {
        printk(KERN_INFO "/dev/%s driver registered\n", DEV_NAME);
        video_registered = 1;
    }
    // end of special code for char. dev. driver

    // generate a virtual address for the FPGA lightweight bridge
    LW_virtual = ioremap_nocache(LW_BRIDGE_BASE, LW_BRIDGE_SPAN);
    if (LW_virtual == 0)
        printk(KERN_ERR "/dev/%s: ioremap_nocache returned NULL\n", DEV_NAME);

    // Create virtual memory access to the pixel buffer controller
    pixel_ctrl_ptr = (unsigned int *)(LW_virtual + PIXEL_BUF_CTRL_BASE);
    get_screen_specs(pixel_ctrl_ptr); // determine X, Y screen size

    // Map both physical frame buffers for double-buffered animation
    fpga_pixel_buffer = (int)ioremap_nocache(FPGA_ONCHIP_BASE, FPGA_ONCHIP_SPAN);
    sdram_pixel_buffer = (int)ioremap_nocache(SDRAM_BASE, FPGA_ONCHIP_SPAN);
    if (fpga_pixel_buffer == 0 || sdram_pixel_buffer == 0)
    {
        printk(KERN_ERR "/dev/%s: pixel buffer mapping failed\n", DEV_NAME);
        return -ENOMEM;
    }

    // Restore the on-chip buffer as the front buffer if a prior run swapped it
    if (*pixel_ctrl_ptr != FPGA_ONCHIP_BASE)
    {
        *(pixel_ctrl_ptr + 1) = FPGA_ONCHIP_BASE;
        wait_for_vsync();
    }

    // The displayed front buffer is FPGA; draw into SDRAM (the back buffer)
    *(pixel_ctrl_ptr + 1) = SDRAM_BASE;
    pixel_buffer = fpga_pixel_buffer;
    clear_screen();
    pixel_buffer = sdram_pixel_buffer;
    clear_screen();
    if (clear_character_plane() != 0)
    {
        printk(KERN_ERR "/dev/%s: failed to clear VGA character buffer\n", DEV_NAME);
        return -ENOMEM;
    }
    return 0;
}

/* Function to find the VGA screen specifications */
void get_screen_specs(volatile int *pixel_ctrl_ptr)
{
    int resolution_reg;

    resolution_reg = *(pixel_ctrl_ptr + 2);
    resolution_x = resolution_reg & 0xFFFF;
    resolution_y = resolution_reg >> 16;
}

/* Function to clear the pixel buffer */
void clear_screen()
{
    int y, x;

    for (x = 0; x < resolution_x; x++)
        for (y = 0; y < resolution_y; y++)
            plot_pixel(x, y, 0);
}

void plot_pixel(int x, int y, short int line_color)
{
    *(short int *)(pixel_buffer + (y << 10) + (x << 1)) = line_color;
}

/* Bresenham's line drawing algorithm. */
void draw_line(int x0, int y0, int x1, int y1, short int color)
{
    /* This function draws a line between points (x0, y0) and (x1, y1). The function assumes
     * that the coordinates are valid */
    register int x_0 = x0;
    register int y_0 = y0;
    register int x_1 = x1;
    register int y_1 = y1;
    register char steep = (ABS(y_1 - y_0) > ABS(x_1 - x_0)) ? 1 : 0;
    register int deltax, deltay, tmp, error, ystep, x, y;

    /* Preprocessing inputs */
    if (steep > 0)
    {
        // Swap x_0 and y_0
        tmp = x_0;
        x_0 = y_0;
        y_0 = tmp;
        // Swap x_1 and y_1
        tmp = x_1;
        x_1 = y_1;
        y_1 = tmp;
    }
    if (x_0 > x_1)
    {
        // Swap x_0 and x_1
        tmp = x_0;
        x_0 = x_1;
        x_1 = tmp;
        // Swap y_0 and y_1
        tmp = y_0;
        y_0 = y_1;
        y_1 = tmp;
    }

    /* Setup local variables */
    deltax = x_1 - x_0;
    deltay = ABS(y_1 - y_0);
    error = -(deltax / 2);
    y = y_0;
    if (y_0 < y_1)
        ystep = 1;
    else
        ystep = -1;

    /* Draw a line - either go along the x-axis (steep = 0) or along the y-axis (steep = 1) */
    for (x = x_0; x <= x_1; x++)
    {
        if (steep == 1)
            plot_pixel(y, x, color);
        else
            plot_pixel(x, y, color);
        error = error + deltay;
        if (error > 0)
        {
            y = y + ystep;
            error = error - deltax;
        }
    }
}

/* Swap the front/back buffers, then draw in the newly available back buffer. */
void wait_for_vsync(void)
{
    *pixel_ctrl_ptr = 1; // request pixel buffer swap
    while ((*(pixel_ctrl_ptr + 3) & 0x1) != 0)
        ; // wait until status S bit clears

    // After swapping, the back buffer is whichever physical buffer is in register 1
    if (*(pixel_ctrl_ptr + 1) == FPGA_ONCHIP_BASE)
        pixel_buffer = fpga_pixel_buffer;
    else
        pixel_buffer = sdram_pixel_buffer;
}

/* Draw a filled rectangle, including both endpoint coordinates. */
void draw_box(int x1, int y1, int x2, int y2, short int color)
{
    int x, y, tmp;
    if (x1 > x2)
    {
        tmp = x1;
        x1 = x2;
        x2 = tmp;
    }
    if (y1 > y2)
    {
        tmp = y1;
        y1 = y2;
        y2 = tmp;
    }
    for (y = y1; y <= y2; y++)
        for (x = x1; x <= x2; x++)
            plot_pixel(x, y, color);
}

/* Remove the character device driver */
static void __exit stop_video(void)
{
    /* unmap the physical-to-virtual mappings */
    if (char_buffer_virtual)
        iounmap(char_buffer_virtual);
    iounmap(LW_virtual);
    if (fpga_pixel_buffer)
        iounmap((void *)fpga_pixel_buffer);
    if (sdram_pixel_buffer)
        iounmap((void *)sdram_pixel_buffer);

    /* Remove the device from the kernel */
    if (video_registered)
    {
        misc_deregister(&video_dev);
        printk(KERN_INFO "/dev/%s driver de-registered\n", DEV_NAME);
    }
}

/* Called when a process opens the video */
static int video_open(struct inode *inode, struct file *file)
{
    return SUCCESS;
}

/* Called when a process closes the video */
static int video_release(struct inode *inode, struct file *file)
{
    return 0;
}

/* Called when a process reads from the video. If *offset = 0, the current value of
 * the video is copied back to the user, and VIDEO_BYTES is returned. But if
 * *offset = VIDEO_BYTES, nothing is copied back to the user and 0 (EOF) is returned. */
static ssize_t video_read(struct file *filp, char *buffer, size_t length, loff_t *offset)
{
    char themessage[VIDEO_BYTES + 1]; // extra byte is for a NULL terminator
    size_t bytes;

    bytes = VIDEO_BYTES - (*offset); // has the video data been sent yet?
    if (bytes)
    {
        sprintf(themessage, "%03d %03d\n", resolution_x, resolution_y);
        if (copy_to_user(buffer, themessage, bytes) != 0)
            printk(KERN_ERR "/dev/%s: copy_to_user unsuccessful\n", DEV_NAME);
    }
    (*offset) = bytes; // keep track of number of bytes sent to the user
    return bytes;      // returns VIDEO_BYTES, or EOF
}

/* Called when a process writes to the video */
static ssize_t video_write(struct file *filp, const char *buffer, size_t length, loff_t *offset)
{
    char video_string[VIDEO_SIZE + 1]; // extra byte is for a NULL terminator
    int bytes;
    char command[8];
    int x1, y1, x2, y2, color;

    bytes = length;
    if (bytes > VIDEO_SIZE) // can copy all at once, or not?
        bytes = VIDEO_SIZE;
    if (copy_from_user(video_string, buffer, bytes) != 0)
        printk(KERN_ERR "/dev/%s: copy_from_user in /dev/video unsuccessful\n", DEV_NAME);

    video_string[bytes] = '\0';
    if (video_string[0] == '-' && video_string[1] == '-')
    {
        printk(KERN_ERR "       clear\n");
        printk(KERN_ERR "       pixel X,Y color\n");
        printk(KERN_ERR "       line X1,Y1 X2,Y2 color\n");
        printk(KERN_ERR "       box X1,Y1 X2,Y2 color\n");
        printk(KERN_ERR "       sync\n");
        printk(KERN_ERR "Notes: X,Y are integers, color is 16-bit hex\n");
    }
    else if (video_string[0] == 'c' && video_string[1] == 'l')
    {
        clear_screen();
    }
    else if (strcmp(video_string, "sync") == 0 || strcmp(video_string, "sync\n") == 0)
    {
        wait_for_vsync();
    }
    else if (video_string[0] == 'p' && video_string[1] == 'i')
    {
        if (sscanf(video_string, "%s %d,%d %X", command, &x1, &y1, &color) != 4)
        {
            printk(KERN_ERR "/dev/%s: can't parse user input: %s\n", DEV_NAME, video_string);
        }
        if (x1 >= 0 && x1 < resolution_x && y1 >= 0 && y1 < resolution_y)
            plot_pixel(x1, y1, color);
    }
    else
    {
        if (sscanf(video_string, "%s %d,%d %d,%d %X", command, &x1, &y1, &x2, &y2, &color) != 6)
        {
            printk(KERN_ERR "/dev/%s: can't parse user input: %s\n", DEV_NAME, video_string);
        }
        else if (strcmp(command, "line") == 0 || strcmp(command, "box") == 0)
        {
            if ((x1 < 0) || (x1 > resolution_x - 1) || (x2 < 0) || (x2 > resolution_x - 1) ||
                (y1 < 0) || (y1 > resolution_y - 1) || (y2 < 0) || (y2 > resolution_y - 1))
                printk(KERN_ERR "/dev/%s: bad coordinates: %d,%d %d,%d\n", DEV_NAME, x1, y1, x2, y2);
            else if (strcmp(command, "box") == 0)
                draw_box(x1, y1, x2, y2, color);
            else
                draw_line(x1, y1, x2, y2, color);
        }
        else
            printk(KERN_ERR "/dev/%s: bad argument: %s\n", DEV_NAME, command);
    }
    return length;
}

MODULE_LICENSE("GPL");
module_init(start_video);
module_exit(stop_video);
