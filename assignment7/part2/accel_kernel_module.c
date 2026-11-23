#include <linux/kernel.h>
#include <linux/fs.h>         // struct file, struct file_operations
#include <linux/init.h>       // for __init, see code
#include <linux/module.h>     // for module init and exit macros
#include <linux/miscdevice.h> // for misc_device_register and struct miscdev
#include <linux/uaccess.h>    // for copy_to_user, see code
#include <asm/io.h>           // for mmap

#include "../include/address_map_arm.h"
#include "../include/ADXL345.h"

/* This kernel module provides a character device driver that communicates with the
 * accelerometer in the DE1-SoC Computer. The user-interface to the driver is through the
 * file /dev/accel.
 * Reading from /dev/accel produces the string "R XXXX YYYY ZZZZ SS", where R is 1 if new
 * data is being provided, else 0. The fields XXXX, YYYY, and ZZZZ are four-digit decimal
 * numbers that represent acceleration in the x, y, and z axes, and SS is a two-digit decimal
 * number that gives the scale factor for converting XXXX, YYYY, and ZZZZ into units of mg.
 * The ADXL345 functions (Pinmux_Config, I2C0_Init, ADXL345_Init, ...) are in ../ADXL345.c */

// START of declarations needed for a character device
static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t device_read(struct file *, char *, size_t, loff_t *);

#define SUCCESS 0
#define DEV_NAME "accel"
#define ACCEL_BYTES 20 // number of bytes to return when read (includes a \n)

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .read = device_read,
    .open = device_open,
    .release = device_release};

static struct miscdevice accel_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = DEV_NAME,
    .fops = &fops,
    .mode = 0666};

static int accel_registered = 0;

// END of declarations needed for a character device

// Declare global variables needed to use the accelerometer
volatile unsigned int *I2C0_ptr;   // virtual address for I2C communication
volatile unsigned int *SYSMGR_ptr; // virtual address for System Manager communication
static int16_t XYZ[3];             // used for acceleration data

/* Code to initialize the accel driver */
static int __init start_accel(void)
{
    int err = misc_register(&accel_dev);
    if (err < 0)
    {
        printk(KERN_ERR "/dev/%s: misc_register() failed\n", DEV_NAME);
    }
    else
    {
        printk(KERN_INFO "/dev/%s driver registered\n", DEV_NAME);
        accel_registered = 1;
    }
    /* END of code to make a character device driver */

    // generate virtual addresses for the I2C controller and System Manager
    I2C0_ptr = ioremap_nocache(I2C0_BASE, I2C0_SPAN);
    SYSMGR_ptr = ioremap_nocache(SYSMGR_BASE, SYSMGR_SPAN);

    if ((I2C0_ptr == NULL) || (SYSMGR_ptr == NULL))
        printk(KERN_ERR "Error: ioremap_nocache returned NULL!\n");

    pass_addrs((unsigned int *)SYSMGR_ptr, (unsigned int *)I2C0_ptr);

    Pinmux_Config();
    I2C0_Init();
    ADXL345_Init();
    return 0;
}

/* Remove the character device driver */
static void __exit stop_accel(void)
{
    /* unmap the physical-to-virtual mappings */
    iounmap(I2C0_ptr);
    iounmap(SYSMGR_ptr);

    /* Remove the device from the kernel */
    if (accel_registered)
    {
        misc_deregister(&accel_dev);
        printk(KERN_INFO "/dev/%s driver de-registered\n", DEV_NAME);
    }
}

/* Called when a process opens the accel */
static int device_open(struct inode *inode, struct file *file)
{
    return SUCCESS;
}

/* Called when a process closes the accel */
static int device_release(struct inode *inode, struct file *file)
{
    return 0;
}

/* Called when a process reads from the accel. If *offset = 0, the current value of
 * the accel is copied back to the user, and ACCEL_BYTES is returned. But if
 * *offset = ACCEL_BYTES, nothing is copied back to the user and 0 (EOF) is returned. */
static ssize_t device_read(struct file *filp, char *buffer, size_t length, loff_t *offset)
{
    char themessage[ACCEL_BYTES + 1]; // extra byte is for a NULL terminator
    size_t bytes;
    int NewData;

    bytes = ACCEL_BYTES - (*offset); // has the acceleration data been sent yet?
    if (bytes)
    {
        NewData = 0; // assume no new data is available
        NewData = ADXL345_IsDataReady();
        if (NewData)
            ADXL345_XYZ_Read(XYZ);

        // return new-data flag and X, Y, Z data
        sprintf(themessage, "%1d %04d %04d %04d %02d\n", NewData, XYZ[0], XYZ[1], XYZ[2], mg_per_lsb); // R XXXX YYYY ZZZZ SS

        if (copy_to_user(buffer, themessage, bytes) != 0)
            printk(KERN_ERR "/dev/%s: copy_to_user unsuccessful\n", DEV_NAME);
    }
    (*offset) = bytes; // keep track of number of bytes sent to the user
    return bytes;      // returns ACCEL_BYTES, or EOF
}

MODULE_LICENSE("GPL");
module_init(start_accel);
module_exit(stop_accel);
