#include <linux/fs.h>               // struct file, struct file_operations
#include <linux/module.h>           // for module init and exit macros
#include <linux/miscdevice.h>       // for misc_device_register and struct miscdev
#include <linux/uaccess.h>          // for copy_to_user, see code
#include <asm/io.h>                 // for mmap
#include "../include/address_map_arm.h"

/* This kernel module provides a character device driver that controls a video display.
 * The user-interface to the driver is through the file /dev/video. The following commands
 * can be written to the driver through this file: "clear" and "pixel x,y color".
 * Reading from the /dev/video produces the string "screen_x screen_y", where these are
 * the number of columns and rows on the screen, respectively. */
        
/* Prototypes for functions used for video display */
void get_screen_specs (volatile int *);
void clear_screen (void);
void plot_pixel(int, int, short int);
void usage(void);

// START of declarations needed for a character device
static int video_open (struct inode *, struct file *);
static int video_release (struct inode *, struct file *);
static ssize_t video_read (struct file *, char *, size_t, loff_t *);
static ssize_t video_write(struct file *, const char *, size_t, loff_t *);

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .read = video_read,
    .write = video_write,
    .open = video_open,
    .release = video_release
};

#define SUCCESS 0
#define DEV_NAME "video"
#define VIDEO_BYTES 8           // number of bytes to return when read (includes a \n)
#define VIDEO_SIZE 18           // max number of bytes accepted for a write 

static struct miscdevice video_dev = { 
    .minor = MISC_DYNAMIC_MINOR, 
    .name = DEV_NAME,
    .fops = &fops,
    .mode = 0666
};

static int video_registered = 0;

// END of declarations needed for a character device

// START of declarations for the video
void *LW_virtual;               // used to map physical addresses for the light-weight bridge
volatile int * pixel_ctrl_ptr;  // virtual address of pixel buffer DMA controller
int pixel_buffer;               // current location of pixel buffer memory
int resolution_x, resolution_y; // VGA screen size
// END of declarations

/* Code to initialize the video driver */
static int __init start_video(void) {
    int err = misc_register (&video_dev);
    if (err < 0) {
        printk (KERN_ERR "/dev/%s: misc_register() failed\n", DEV_NAME);
    }
    else {
        printk (KERN_INFO "/dev/%s driver registered\n", DEV_NAME);
        video_registered = 1;
    }
    // end of special code for char. dev. driver
   
    // generate a virtual address for the FPGA lightweight bridge
    LW_virtual = ioremap_nocache (LW_BRIDGE_BASE, LW_BRIDGE_SPAN);
    if (LW_virtual == 0)
        printk (KERN_ERR "/dev/%s: ioremap_nocache returned NULL\n", DEV_NAME);

    // Create virtual memory access to the pixel buffer controller
    pixel_ctrl_ptr = (unsigned int *) (LW_virtual + PIXEL_BUF_CTRL_BASE);
    get_screen_specs (pixel_ctrl_ptr);  // determine X, Y screen size

    // Create virtual memory access to the pixel buffer
    pixel_buffer = (int) ioremap_nocache (FPGA_ONCHIP_BASE, FPGA_ONCHIP_SPAN); 
    if (pixel_buffer == 0)
        printk (KERN_ERR "/dev/%s: ioremap_nocache returned NULL\n", DEV_NAME);

    /* Erase the pixel buffer */
    clear_screen ( );
    return 0;
}

/* Function to find the VGA screen specifications */
void get_screen_specs(volatile int * pixel_ctrl_ptr) {
    int resolution_reg;

    resolution_reg = *(pixel_ctrl_ptr + 2);
    resolution_x = resolution_reg & 0xFFFF;
    resolution_y = resolution_reg >> 16;
}

/* Function to clear the pixel buffer */
void clear_screen( ) {
    int y, x;

    for (x = 0; x < resolution_x; x++)
        for (y = 0; y < resolution_y; y++)
            plot_pixel (x, y, 0);
}

void plot_pixel(int x, int y, short int color) {
    *(short int *)(pixel_buffer + (y << 10) + (x << 1)) = color;
}

/* Remove the character device driver */
static void __exit stop_video(void) {
    /* unmap the physical-to-virtual mappings */
    iounmap (LW_virtual);
    iounmap ((void *) pixel_buffer);

    /* Remove the device from the kernel */
    if (video_registered) {
        misc_deregister (&video_dev);
        printk (KERN_INFO "/dev/%s driver de-registered\n", DEV_NAME);
    }
}

/* Called when a process opens the video */
static int video_open(struct inode *inode, struct file *file) {
    return SUCCESS;
}

/* Called when a process closes the video */
static int video_release(struct inode *inode, struct file *file) {
    return 0;
}

/* Called when a process reads from the video. If *offset = 0, the current value of
 * the video is copied back to the user, and VIDEO_BYTES is returned. But if 
 * *offset = VIDEO_BYTES, nothing is copied back to the user and 0 (EOF) is returned. */
static ssize_t video_read(struct file *filp, char *buffer, size_t length, loff_t *offset) {
    char themessage[VIDEO_BYTES + 1];   // extra byte is for a NULL terminator
    size_t bytes;

    bytes = VIDEO_BYTES - (*offset);    // has the video data been sent yet?
    if (bytes) {
        sprintf(themessage, "%03d %03d\n", resolution_x, resolution_y);
        if (copy_to_user (buffer, themessage, bytes) != 0)
            printk (KERN_ERR "/dev/%s: copy_to_user unsuccessful\n", DEV_NAME);
    }
    (*offset) = bytes;                  // keep track of number of bytes sent to the user
    return bytes;                       // returns VIDEO_BYTES, or EOF
}

void usage( ) {
    printk (KERN_ERR "Usage: --\n");
    printk (KERN_ERR "Usage: clear\n");
    printk (KERN_ERR "       pixel X,Y color\n");
    printk (KERN_ERR "Notes: X,Y are integers, color is 4-digit hex value\n");
}

/* Called when a process writes to the video */
static ssize_t video_write(struct file *filp, const char *buffer, size_t length, loff_t *offset) {
    char video_string[VIDEO_SIZE + 1];  // extra byte is for a NULL terminator
    char command[8];
    int bytes;
    int x, y, color;

    bytes = length;
    if (bytes > VIDEO_SIZE)             // can copy all at once, or not?
        bytes = VIDEO_SIZE;
    if (copy_from_user (video_string, buffer, bytes) != 0)
        printk (KERN_ERR "/dev/%s: copy_from_user unsuccessful\n", DEV_NAME);

    video_string[bytes] = '\0';         // NULL terminate
    if (video_string[0] == '-' && video_string[1] == '-') {
        usage ( );
    }
    else if (video_string[0] == 'c' && video_string[1] == 'l') {
        clear_screen ( );
    }
    else {
        if (sscanf (video_string, "%s %d,%d %X", command, &x, &y, &color) != 4) {
            printk (KERN_ERR "/dev/%s: can't parse user input: %s\n", DEV_NAME, video_string);
        }
        if (strcmp (command, "pixel") == 0) {
            if ((x < 0) || (x > resolution_x - 1) || (y < 0) || (y > resolution_y - 1)) {
                printk (KERN_ERR "/dev/%s: bad coordinates: %d,%d\n", DEV_NAME, x, y);
                usage ( );
            }
            else {
                // printk (KERN_ERR "/dev/%s: coordinates: %d,%d %d\n", DEV_NAME, x, y, color);
                plot_pixel (x, y, color);
            }
        }
        else {
            printk (KERN_ERR "/dev/%s: bad argument: %s\n", DEV_NAME, command);
            usage ( );
        }
    }
    return length;
}

MODULE_LICENSE("GPL");
module_init (start_video);
module_exit (stop_video);
