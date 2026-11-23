#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
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
    .mode = 0666};

volatile unsigned int *I2C0_ptr;
volatile unsigned int *SYSMGR_ptr;
static int16_t XYZ[3];
static DEFINE_MUTEX(accel_lock);

/* Part 4 tap settings (ADXL345 datasheet register scale factors).
 * THRESH_TAP: 48 x 62.5 mg = 3 g
 * DUR:        32 x 625 us = 20 ms
 * LATENT:     16 x 1.25 ms = 20 ms
 * WINDOW:    240 x 1.25 ms = 300 ms
 */
static void configure_taps(void)
{
    ADXL345_REG_WRITE(ADXL345_REG_INT_ENABLE, 0);
    ADXL345_REG_WRITE(ADXL345_REG_THRESH_TAP, 48); // Tap Threshold
    ADXL345_REG_WRITE(ADXL345_REG_DURATION, 32);   // max tap duration
    ADXL345_REG_WRITE(ADXL345_REG_LATENCY, 16);    // tap latency
    ADXL345_REG_WRITE(ADXL345_REG_WINDOW, 240);    // double-tap window
    ADXL345_REG_WRITE(ADXL345_REG_TAP_AXES, 0x07); // enable X, Y, Z
    ADXL345_REG_WRITE(ADXL345_REG_INT_ENABLE, XL345_DATAREADY | XL345_SINGLETAP | XL345_DOUBLETAP);
    // Clear any old event bits after configuring the detector
    {
        uint8_t unused;
        ADXL345_REG_READ(ADXL345_REG_INT_SOURCE, &unused);
    }
}

static int __init start_accel(void)
{
    int err;
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
    configure_taps();

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

// One interrupt-source/XYZ sample followed by EOF for supplied wrapper
static ssize_t device_read(struct file *filp, char *buffer, size_t length, loff_t *offset)
{
    char message[48];
    int bytes;
    uint8_t interrupt_source;

    if (*offset != 0)
    {
        *offset = 0; // next accel_read() can obtain another sample
        return 0;
    }
    if (!length)
        return 0;

    mutex_lock(&accel_lock);
    // Read INT_SOURCE exactly once; reading it clears latched tap bits
    ADXL345_REG_READ(ADXL345_REG_INT_SOURCE, &interrupt_source);
    if (interrupt_source & XL345_DATAREADY) // examine individual interrupt bits using bitwise AND Instead of calling ADXL345_IsDataReady()
        ADXL345_XYZ_Read(XYZ);
    bytes = scnprintf(message, sizeof(message), "%02X %d %d %d %d\n", interrupt_source, XYZ[0], XYZ[1], XYZ[2], mg_per_lsb); // interrupt_source is hexadecimal interrupt-source value; 80: new acceleration data is ready; 40: a single tap was detected; 20: a double tap was detected
    mutex_unlock(&accel_lock);

    if (length < bytes)
        return -EINVAL; /* wrappers read with a 32-byte buffer */
    if (copy_to_user(buffer, message, bytes))
        return -EFAULT;
    *offset = bytes;
    return bytes;
}

// Write commands: device, init, calibrate, format F G, rate R
static ssize_t device_write(struct file *filp, const char *buffer, size_t length, loff_t *offset)
{
    char command[CMD_BYTES];
    int full, range, range_bits;
    unsigned int devid;
    char rate_text[20];
    uint8_t id;
    uint8_t rate_code;
    int valid = 1;

    if (!length || length >= CMD_BYTES)
        return -EINVAL;
    if (copy_from_user(command, buffer, length))
        return -EFAULT;
    command[length] = '\0';
    strim(command);

    mutex_lock(&accel_lock);
    if (!strcmp(command, "device"))
    {
        ADXL345_IdRead(&id);
        devid = id;
        printk(KERN_ALERT "ADXL345 device ID: 0x%02X\n", devid);
    }
    else if (!strcmp(command, "init"))
    {
        ADXL345_Init();
        configure_taps();
        XYZ[0] = XYZ[1] = XYZ[2] = 0;
    }
    else if (!strcmp(command, "calibrate"))
    {
        ADXL345_Calibrate();
    }
    else if (sscanf(command, "format %d %d", &full, &range) == 2)
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
    else if (sscanf(command, "rate %19s", rate_text) == 1)
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
MODULE_DESCRIPTION("DE1-SoC ADXL345 Part 4 driver");
module_init(start_accel);
module_exit(stop_accel);
