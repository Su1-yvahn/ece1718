#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/mutex.h> // supports a mutex used to protect accelerometer operations
#include <linux/string.h>
#include <asm/io.h>

#include "../include/address_map_arm.h"
#include "../include/ADXL345.h"

#define DEV_NAME "accel"
#define CMD_BYTES 40

static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t device_read(struct file *, char *, size_t, loff_t *);
static ssize_t device_write(struct file *, const char *, size_t, loff_t *);

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .read = device_read,
    .write = device_write,
    .open = device_open,
    .release = device_release};

static struct miscdevice accel_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = DEV_NAME,
    .fops = &fops,
    .mode = 0666}; // allow reading and writing by all users

volatile unsigned int *I2C0_ptr;   // points to the memory-mapped I²C controller registers
volatile unsigned int *SYSMGR_ptr; // points to the System Manager registers used for pin configuration
static int16_t XYZ[3];             // XYZ[3] stores the latest raw acceleration readings
// Since both read and write can happen to the accelerometer's configuration
// one process could be reading acceleration while another process sends a configuration command, The mutex prevents these driver operations from overlapping
static DEFINE_MUTEX(accel_lock);

static int __init start_accel(void)
{
    int err;

    // map the physical hardware register regions into kernel virtual addresses
    I2C0_ptr = ioremap_nocache(I2C0_BASE, I2C0_SPAN);
    SYSMGR_ptr = ioremap_nocache(SYSMGR_BASE, SYSMGR_SPAN);

    if (!I2C0_ptr || !SYSMGR_ptr)
    {
        printk(KERN_ERR "/dev/%s: ioremap_nocache failed\n", DEV_NAME);
        err = -ENOMEM;
        goto error;
    }

    pass_addrs((unsigned int *)SYSMGR_ptr, (unsigned int *)I2C0_ptr);
    Pinmux_Config();
    I2C0_Init();
    ADXL345_Init();

    err = misc_register(&accel_dev);
    if (err)
    {
        printk(KERN_ERR "/dev/%s: misc_register failed\n", DEV_NAME);
        goto error;
    }
    printk(KERN_INFO "/dev/%s driver registered\n", DEV_NAME);
    return 0;

error:
    if (I2C0_ptr)
        iounmap(I2C0_ptr);
    if (SYSMGR_ptr)
        iounmap(SYSMGR_ptr);
    return err;
}

static void __exit stop_accel(void)
{
    misc_deregister(&accel_dev);
    iounmap(I2C0_ptr);
    iounmap(SYSMGR_ptr);
    printk(KERN_INFO "/dev/%s driver de-registered\n", DEV_NAME);
}

static int device_open(struct inode *inode, struct file *file)
{
    return 0;
}

static int device_release(struct inode *inode, struct file *file)
{
    return 0;
}

/* Called when a process reads from the accel. If *offset = 0, the current value of
 * the accel is copied back to the user, and ACCEL_BYTES is returned. But if
 * *offset = ACCEL_BYTES, nothing is copied back to the user and 0 (EOF) is returned. */
static ssize_t device_read(struct file *filp, char *buffer, size_t length, loff_t *offset)
{
    char message[48];
    int bytes, ready;

    if (*offset != 0)
    {
        *offset = 0; // next accel_read() can obtain another sample
        return 0;
    }
    if (!length)
        return 0;

    mutex_lock(&accel_lock);
    ready = ADXL345_IsDataReady(); // checking whether new data is available
    if (ready)
        ADXL345_XYZ_Read(XYZ);                                                                                  // reading the three acceleration axes
    bytes = scnprintf(message, sizeof(message), "%d %d %d %d %d\n", ready, XYZ[0], XYZ[1], XYZ[2], mg_per_lsb); // formatting the data for user: R XXXX YYYY ZZZZ SS e.g., 1 0 -3 32 31
    mutex_unlock(&accel_lock);

    if (length < bytes)
        return -EINVAL;                       // wrappers read with a 32-byte buffer
    if (copy_to_user(buffer, message, bytes)) // copies the text into the user program's memory
        return -EFAULT;
    *offset = bytes;
    return bytes;
}

// Write commands: device, init, calibrate, format F G, rate R
// receives a text command from user and decides which accelerometer function to execute
static ssize_t device_write(struct file *filp, const char *buffer, size_t length, loff_t *offset)
{
    char command[CMD_BYTES];
    int full, range, range_bits;
    unsigned int devid;
    char rate_text[20];
    uint8_t id;
    uint8_t rate_code;
    int valid = 1;

    // copies the command from user memory
    if (!length || length >= CMD_BYTES)
        return -EINVAL;
    if (copy_from_user(command, buffer, length))
        return -EFAULT;
    command[length] = '\0';
    strim(command); // removes trailing whitespace

    mutex_lock(&accel_lock);
    if (!strcmp(command, "device")) // Write command: 'device'
    {
        ADXL345_IdRead(&id); // 0xE5
        devid = id;
        printk(KERN_ALERT "ADXL345 device ID: 0x%02X\n", devid);
    }
    else if (!strcmp(command, "init")) // Write command: 'init'
    {
        ADXL345_Init();               // reinitializes the accelerometer
        XYZ[0] = XYZ[1] = XYZ[2] = 0; // resets our cached acceleration values to zero
    }
    else if (!strcmp(command, "calibrate")) // Write command: 'calibrate'
    {
        ADXL345_Calibrate();
    }
    // F is the resolution format (0: fixed 10-bit mode; 1: full-resolution mode)
    // G is the measurement range: (2: ±2 g; 4: ±4 g; 8: ±8 g; 16: ±16 g)
    else if (sscanf(command, "format %d %d", &full, &range) == 2) // Write command: 'format F G'
    {
        if ((full != 0 && full != 1) || (range != 2 && range != 4 && range != 8 && range != 16))
        {
            valid = 0;
        }
        else
        {
            if (range == 2)
                range_bits = XL345_RANGE_2G;
            else if (range == 4)
                range_bits = XL345_RANGE_4G;
            else if (range == 8)
                range_bits = XL345_RANGE_8G;
            else
                range_bits = XL345_RANGE_16G;
            ADXL345_REG_WRITE(ADXL345_REG_DATA_FORMAT, range_bits | (full ? XL345_FULL_RESOLUTION : 0));
            mg_per_lsb = full ? 4 : ROUNDED_DIVISION(range * 1000, 512);
        }
    }
    else if (sscanf(command, "rate %19s", rate_text) == 1) // Write command: 'rate R': parses a requested sampling frequency
    {
        if (!strcmp(rate_text, "25") || !strcmp(rate_text, "25.00"))
            rate_code = XL345_RATE_25;
        else if (!strcmp(rate_text, "12.5") || !strcmp(rate_text, "12.50"))
            rate_code = XL345_RATE_12_5;
        else if (!strcmp(rate_text, "6.25"))
            rate_code = XL345_RATE_6_25;
        else if (!strcmp(rate_text, "1.56"))
            rate_code = XL345_RATE_1_56;
        else if (!strcmp(rate_text, "0.78"))
            rate_code = XL345_RATE_0_78;
        else if (!strcmp(rate_text, "50") || !strcmp(rate_text, "50.00"))
            rate_code = XL345_RATE_50;
        else if (!strcmp(rate_text, "100") || !strcmp(rate_text, "100.00"))
            rate_code = XL345_RATE_100;
        else
            valid = 0;
        if (valid)
            ADXL345_REG_WRITE(ADXL345_REG_BW_RATE, rate_code);
    }
    else
    {
        valid = 0;
    }
    mutex_unlock(&accel_lock);

    if (!valid)
    {
        printk(KERN_WARNING "/dev/%s: invalid command: %s\n", DEV_NAME, command);
        return -EINVAL;
    }
    return length;
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DE1-SoC ADXL345 Part 3 driver");
module_init(start_accel);
module_exit(stop_accel);
