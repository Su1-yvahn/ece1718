#include <linux/fs.h>         // struct file, struct file_operations
#include <linux/init.h>       // for __init, see code
#include <linux/module.h>     // for module init and exit macros
#include <linux/miscdevice.h> // for misc_device_register and struct miscdev
#include <linux/uaccess.h>    // for copy_from_user
#include <asm/io.h>           // for mmap
#include "../include/address_map_arm_vm.h"

/* This kernel module provides two character device drivers. One driver controls
 * the LEDR red LEDs through /dev/LEDR. The other driver controls the six
 * seven-segment displays through /dev/HEX. */

// START of declarations needed for the character device drivers
static int LEDR_device_open(struct inode *, struct file *);
static int LEDR_device_release(struct inode *, struct file *);
static ssize_t LEDR_device_write(struct file *, const char *, size_t, loff_t *);

static int HEX_device_open(struct inode *, struct file *);
static int HEX_device_release(struct inode *, struct file *);
static ssize_t HEX_device_write(struct file *, const char *, size_t, loff_t *);

#define SUCCESS 0
#define LEDR_DEV "LEDR"
#define HEX_DEV "HEX"
#define LEDR_SIZE 4 // three hexadecimal digits plus '\n'
#define HEX_SIZE 17 // enough for "-r HHHH HHHHHHHH\n"

static struct file_operations LEDR_fops = {
    .owner = THIS_MODULE,
    .write = LEDR_device_write,
    .open = LEDR_device_open,
    .release = LEDR_device_release};

static struct file_operations HEX_fops = {
    .owner = THIS_MODULE,
    .write = HEX_device_write,
    .open = HEX_device_open,
    .release = HEX_device_release};

static struct miscdevice LEDR = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = LEDR_DEV,
    .fops = &LEDR_fops,
    .mode = 0666};

static struct miscdevice HEX = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = HEX_DEV,
    .fops = &HEX_fops,
    .mode = 0666};

static int LEDR_registered = 0;
static int HEX_registered = 0;
// END of declarations needed for the character device drivers

// START of declarations for the LEDR and HEX ports
void *LW_virtual;            // virtual address for the light-weight bridge
volatile int *LEDR_ptr;      // virtual pointer to LEDR port
volatile int *HEX3_HEX0_ptr; // virtual pointer to HEX3-HEX0 port
volatile int *HEX5_HEX4_ptr; // virtual pointer to HEX5-HEX4 port

/* Segment codes for decimal digits 0-9. Bit 0 controls segment 0, bit 1
 * controls segment 1, ..., and bit 6 controls segment 6. */
static const unsigned char seven_seg_decode_table[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66,
    0x6D, 0x7D, 0x07, 0x7F, 0x67};
// END of declarations for the ports

/* Code to initialize the drivers */
static int __init init_drivers(void)
{
    int err = 0;

    /* Make the LEDR character device driver */
    err = misc_register(&LEDR);
    if (err < 0)
    {
        printk(KERN_ERR "/dev/%s: misc_register() failed\n", LEDR_DEV);
    }
    else
    {
        printk(KERN_INFO "/dev/%s driver registered\n", LEDR_DEV);
        LEDR_registered = 1;
    }

    /* Make the HEX character device driver */
    err = misc_register(&HEX);
    if (err < 0)
    {
        printk(KERN_ERR "/dev/%s: misc_register() failed\n", HEX_DEV);
    }
    else
    {
        printk(KERN_INFO "/dev/%s driver registered\n", HEX_DEV);
        HEX_registered = 1;
    }

    /* Set up virtual port addresses */
    LW_virtual = ioremap_nocache(LW_BRIDGE_BASE, LW_BRIDGE_SPAN);

    LEDR_ptr = LW_virtual + LEDR_BASE;
    HEX3_HEX0_ptr = LW_virtual + HEX3_HEX0_BASE;
    HEX5_HEX4_ptr = LW_virtual + HEX5_HEX4_BASE;

    /* Start with LEDs and seven-segment displays turned off */
    *LEDR_ptr = 0;
    *HEX3_HEX0_ptr = 0;
    *HEX5_HEX4_ptr = 0;

    return err;
}

/* Remove the character device drivers */
static void __exit stop_drivers(void)
{
    /* Turn off outputs before unmapping the bridge */
    *LEDR_ptr = 0;
    *HEX3_HEX0_ptr = 0;
    *HEX5_HEX4_ptr = 0;

    iounmap(LW_virtual);

    if (LEDR_registered)
    {
        misc_deregister(&LEDR);
        printk(KERN_INFO "/dev/%s driver de-registered\n", LEDR_DEV);
    }
    if (HEX_registered)
    {
        misc_deregister(&HEX);
        printk(KERN_INFO "/dev/%s driver de-registered\n", HEX_DEV);
    }
}

/* Called when a process opens the LEDR driver */
static int LEDR_device_open(struct inode *inode, struct file *file)
{
    return SUCCESS;
}

/* Called when a process closes the LEDR driver */
static int LEDR_device_release(struct inode *inode, struct file *file)
{
    return 0;
}

/* Called when a process writes to the LEDR driver. The expected data is a
 * three-digit hexadecimal value, such as "3FF\n". */
static ssize_t LEDR_device_write(struct file *filp, const char *buffer,
                                 size_t length, loff_t *offset)
{
    char LEDR_string[LEDR_SIZE + 1];
    unsigned int LEDR_data;
    size_t bytes;

    bytes = length;
    if (bytes > LEDR_SIZE)
        bytes = LEDR_SIZE;

    if (copy_from_user(LEDR_string, buffer, bytes) != 0)
        printk(KERN_ERR "/dev/%s: copy_from_user unsuccessful\n", LEDR_DEV);

    LEDR_string[bytes] = '\0';
    *offset += bytes;

    /* Print usage information when the user writes -- to the driver. */
    if ((LEDR_string[0] == '-') && (LEDR_string[1] == '-'))
    {
        printk(KERN_ERR "Usage info for LEDR\n");
        printk(KERN_ERR "Usage: HHH, where 0 <= H <= F\n");
        return length;
    }

    /* Convert the ASCII hexadecimal string into an integer. */
    if (sscanf(LEDR_string, "%x", &LEDR_data) == 1)
        *LEDR_ptr = LEDR_data & 0x3FF; // only LEDR9-LEDR0 are used

    return length;
}

/* Called when a process opens the HEX driver */
static int HEX_device_open(struct inode *inode, struct file *file)
{
    return SUCCESS;
}

/* Called when a process closes the HEX driver */
static int HEX_device_release(struct inode *inode, struct file *file)
{
    return 0;
}

/* Called when a process writes to the HEX driver.
 *
 * Normal mode:
 *     DDDDDD
 * displays a decimal value on HEX5-HEX0.
 *
 * Raw mode:
 *     -r HHHH HHHHHHHH
 * writes the hexadecimal bit patterns directly to the HEX5-HEX4 and
 * HEX3-HEX0 hardware registers. */
static ssize_t HEX_device_write(struct file *filp, const char *buffer,
                                size_t length, loff_t *offset)
{
    char HEX_string[HEX_SIZE + 1];
    int HEX_data;
    unsigned int HEX3_HEX0_data = 0;
    unsigned int HEX5_HEX4_data = 0;
    unsigned int digit;
    unsigned int code;
    size_t bytes;
    int i;

    bytes = length;
    if (bytes > HEX_SIZE)
        bytes = HEX_SIZE;

    if (copy_from_user(HEX_string, buffer, bytes) != 0)
        printk(KERN_ERR "/dev/%s: copy_from_user unsuccessful\n", HEX_DEV);

    HEX_string[bytes] = '\0';
    *offset += bytes;

    /* Print usage information when the user writes -- to the driver. */
    if ((HEX_string[0] == '-') && (HEX_string[1] == '-'))
    {
        printk(KERN_ERR "Usage info for HEX\n");
        printk(KERN_ERR "Usage: --\n");
        printk(KERN_ERR "       DDDDDD, where 0 <= D <= 9\n");
        printk(KERN_ERR "       -r HHHH HHHHHHHH, where 0 <= H <= F\n");
        return length;
    }

    /* Raw mode: directly write segment bit patterns to the two HEX registers. */
    if ((HEX_string[0] == '-') && (HEX_string[1] == 'r'))
    {
        if (sscanf(HEX_string, "-r %x %x", &HEX5_HEX4_data,
                   &HEX3_HEX0_data) == 2)
        {
            *HEX5_HEX4_ptr = HEX5_HEX4_data & 0xFFFF;
            *HEX3_HEX0_ptr = HEX3_HEX0_data;
        }
        return length;
    }

    /* Normal mode: convert the ASCII decimal string into an integer. */
    if (sscanf(HEX_string, "%d", &HEX_data) != 1)
        return length;

    if (HEX_data < 0)
        HEX_data = 0;
    if (HEX_data > 999999)
        HEX_data %= 1000000;

    /* Decode one decimal digit at a time, starting with HEX0. */
    for (i = 0; i < 6; ++i)
    {
        digit = HEX_data % 10;
        code = seven_seg_decode_table[digit];

        if (i < 4)
            HEX3_HEX0_data |= code << (i * 8);
        else
            HEX5_HEX4_data |= code << ((i - 4) * 8);

        HEX_data /= 10;
    }

    *HEX3_HEX0_ptr = HEX3_HEX0_data;
    *HEX5_HEX4_ptr = HEX5_HEX4_data;

    return length;
}

MODULE_LICENSE("GPL");
module_init(init_drivers);
module_exit(stop_drivers);
