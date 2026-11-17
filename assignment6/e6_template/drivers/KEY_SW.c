#include <linux/fs.h>         // struct file, struct file_operations
#include <linux/module.h>     // for module init and exit macros
#include <linux/miscdevice.h> // for misc_device_register and struct miscdev
#include <linux/uaccess.h>    // for copy_to_user, see code
#include <asm/io.h>           // for mmap
#include "../include/address_map_arm.h"

/* This kernel module provides two character device drivers. One driver provides the state
 * of the KEY pushbutton switches in the DE1-SoC computer, via the file /dev/KEY. The data is
 * provided as a single digit that represents the hexadecimal valuation of the four keys. The
 * other driver provides the state of the SW slider switches, via the file /dev/SW. The data is
 * provided as three digits that represent the hexadecimal valuation of the ten switches. */

// START of declarations needed for the character device drivers
static int KEY_device_open(struct inode *, struct file *);
static int KEY_device_release(struct inode *, struct file *);
static ssize_t KEY_device_read(struct file *, char *, size_t, loff_t *);
static ssize_t KEY_device_write(struct file *, const char *, size_t, loff_t *);

static int SW_device_open(struct inode *, struct file *);
static int SW_device_release(struct inode *, struct file *);
static ssize_t SW_device_read(struct file *, char *, size_t, loff_t *);
static ssize_t SW_device_write(struct file *, const char *, size_t, loff_t *);

#define SUCCESS 0
#define KEY_DEV "KEY"
#define SW_DEV "SW"
#define KEY_BYTES 2 // number of bytes returned when read (e.g., pressing KEY0: 1\n) => 2 bytes
#define SW_BYTES 4  // number of bytes returned when read (e.g., 10 switches: 3FF\n) => 4 bytes
#define KEY_SIZE 3  // used for writing -- (usage) command (e.g., In "echo -- > /dev/KEY", --\n is 3 bytes)
#define SW_SIZE 3   // used for writing -- (usage) command (e.g., In "echo -- > /dev/SW", --\n is 3 bytes)

static struct file_operations KEY_fops = {
    .owner = THIS_MODULE,
    .read = KEY_device_read,
    .write = KEY_device_write,
    .open = KEY_device_open,
    .release = KEY_device_release};

static struct file_operations SW_fops = {
    .owner = THIS_MODULE,
    .read = SW_device_read,
    .write = SW_device_write,
    .open = SW_device_open,
    .release = SW_device_release};

static struct miscdevice KEY = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = KEY_DEV,
    .fops = &KEY_fops,
    .mode = 0666};

static struct miscdevice SW = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = SW_DEV,
    .fops = &SW_fops,
    .mode = 0666};

static int KEY_registered = 0;
static int SW_registered = 0;
// END of declarations needed for the character device drivers

// START of declarations for the KEY and SW ports
void *LW_virtual;      // used to map physical addresses for the light-weight bridge
volatile int *KEY_ptr; // virtual pointer to KEY port
volatile int *SW_ptr;  // virtual pointer to SW port
// END of declarations for the ports

/* Code to initialize the drivers */
static int __init init_drivers(void)
{
    int err = 0;

    /* START of code to make KEY port character device driver */
    err = misc_register(&KEY);
    if (err < 0)
    {
        printk(KERN_ERR "/dev/%s: misc_register() failed\n", KEY_DEV);
    }
    else
    {
        printk(KERN_INFO "/dev/%s driver registered\n", KEY_DEV);
        KEY_registered = 1;
    }

    /* START of code to make SW port character device driver */
    err = misc_register(&SW);
    if (err < 0)
    {
        printk(KERN_ERR "/dev/%s: misc_register() failed\n", SW_DEV);
    }
    else
    {
        printk(KERN_INFO "/dev/%s driver registered\n", SW_DEV);
        SW_registered = 1;
    }
    /* END of code to make a character device driver */

    /* START of code to set up virtual port addresses */
    // generate a virtual address for the FPGA lightweight bridge
    LW_virtual = ioremap_nocache(LW_BRIDGE_BASE, LW_BRIDGE_SPAN);

    // Set up the KEY and SW ports
    KEY_ptr = LW_virtual + KEY_BASE; // KEY port base address
    *(KEY_ptr + 0x3) = 0xF;          // clear the Edgecapture register
    SW_ptr = LW_virtual + SW_BASE;   // SW port base address

    return 0;
}

/* Remove the character device drivers */
static void __exit stop_drivers(void)
{
    iounmap(LW_virtual);
    /* Remove the devices from the kernel */
    if (KEY_registered)
    {
        misc_deregister(&KEY);
        printk(KERN_INFO "/dev/%s driver de-registered\n", KEY_DEV);
    }
    if (SW_registered)
    {
        misc_deregister(&SW);
        printk(KERN_INFO "/dev/%s driver de-registered\n", SW_DEV);
    }
}

/* Called when a process opens the KEY driver */
static int KEY_device_open(struct inode *inode, struct file *file)
{
    return SUCCESS;
}

/* Called when a process closes the KEY driver */
static int KEY_device_release(struct inode *inode, struct file *file)
{
    return 0;
}

/* Called when a process reads from the KEY driver. If *offset = 0, the contents of the KEY
 * Edgecapture register are copied back to the user, and KEY_BYTES is returned. But if
 * *offset = KEY_BYTES, nothing is copied back to the user and 0 (EOF) is returned. */
// when perform the read operation initially, the *offset is 0, the "bytes" is set to the length of the character string that is returned when the device is read
static ssize_t KEY_device_read(struct file *filp, char *buffer, size_t length, loff_t *offset)
{
    int KEY_data;
    char themessage[KEY_BYTES + 1]; // extra byte is for a NULL terminator
    size_t bytes;

    // determine how much data can be sent back to the kernel
    bytes = KEY_BYTES - (*offset); // has the KEY data been sent yet?
    if (bytes)
    {
        KEY_data = *(KEY_ptr + 0x3);                      // read the KEY port Edgecapture register
        *(KEY_ptr + 0x3) = KEY_data;                      // clear the Edgecapture register
        sprintf(themessage, "%1X\n", KEY_data);           // put data into the message
        if (copy_to_user(buffer, themessage, bytes) != 0) // copies character string from the kernel back to user-level program via buffer
            printk(KERN_ERR "/dev/%s: copy_to_user unsuccessful\n", KEY_DEV);
    }

    (*offset) = bytes; // keep track of number of bytes sent
    return bytes;      // returns KEY_BYTES, or EOF
}

/* Called when a process writes to the KEY driver. Only the -- usage command is supported */
// length is the maximum number of bytes that user is trying to write
static ssize_t KEY_device_write(struct file *filp, const char *buffer, size_t length, loff_t *offset)
{
    char KEY_string[KEY_SIZE + 1]; // extra byte is for a NULL terminator (the size is only KEY_SIZE + 1)
    size_t bytes;
    bytes = length; // (how much did user try to write) if length is 10 bytes, then "bytes" is also 10 bytes, but too big to send

    // check overflow
    if (bytes > KEY_SIZE) // can copy all at once, or not?
        bytes = KEY_SIZE;
    if (copy_from_user(KEY_string, buffer, bytes) != 0) // copy character string "KEY_string" from user-level to kernel through buffer
        printk(KERN_ERR "/dev/%s: copy_from_user unsuccessful\n", KEY_DEV);
    KEY_string[bytes] = '\0'; // NULL terminate (required for C string)
    *offset += bytes;         // keep track of how much was copied (where you currently are in the file)

    if (KEY_string[0] == '-')
    {
        printk(KERN_ERR "Intel FPGA sample device driver\n");
        printk(KERN_ERR "Returns: H, where 0 <= H <= F\n");
    }
    return length;
}

/* Called when a process opens the SW driver */
static int SW_device_open(struct inode *inode, struct file *file)
{
    return SUCCESS;
}

/* Called when a process closes the SW driver */
static int SW_device_release(struct inode *inode, struct file *file)
{
    return 0;
}

/* Called when a process reads from the SW driver. If *offset = 0, the contents of the SW
 * data register are copied back to the user, and SW_BYTES is returned. But if
 * *offset = SW_BYTES, nothing is copied back to the user and 0 (EOF) is returned. */
static ssize_t SW_device_read(struct file *filp, char *buffer, size_t length, loff_t *offset)
{
    int SW_data;
    char themessage[SW_BYTES + 1]; // extra byte is for a NULL terminator
    size_t bytes;

    bytes = SW_BYTES - (*offset); // has the SW data been sent yet?
    if (bytes)
    {
        SW_data = *(SW_ptr);                    // read the SW port data register
        sprintf(themessage, "%03X\n", SW_data); // put data into the message
        if (copy_to_user(buffer, themessage, bytes) != 0)
            printk(KERN_ERR "/dev/%s: copy_to_user unsuccessful\n", SW_DEV);
    }
    (*offset) = bytes; // keep track of number of bytes sent to the user
    return bytes;      // returns SW_BYTES, or EOF
}

/* Called when a process writes to the SW driver. Only the -- usage command is supported */
static ssize_t SW_device_write(struct file *filp, const char *buffer, size_t length,
                               loff_t *offset)
{
    char SW_string[SW_SIZE + 1]; // extra byte is for a NULL terminator
    size_t bytes;
    bytes = length;

    if (bytes > SW_SIZE) // can copy all at once, or not?
        bytes = SW_SIZE;
    if (copy_from_user(SW_string, buffer, bytes) != 0)
        printk(KERN_ERR "/dev/%s: copy_from_user unsuccessful\n", SW_DEV);
    SW_string[bytes] = '\0'; // NULL terminate
    *offset += bytes;        // keep track of how much was copied

    if (SW_string[0] == '-')
    {
        printk(KERN_ERR "Intel FPGA sample device driver\n");
        printk(KERN_ERR "Returns: HHH, where 0 <= H <= F\n");
    }
    return length;
}

MODULE_LICENSE("GPL");
module_init(init_drivers);
module_exit(stop_drivers);
