#include <linux/fs.h>               // struct file, struct file_operations
#include <linux/module.h>           // for module init and exit macros
#include <linux/miscdevice.h>       // for misc_device_register and struct miscdev
#include <linux/uaccess.h>          // for copy_to_user, see code
#include <linux/interrupt.h>
#include <asm/io.h>                 // for mmap
#include "../include/address_map_arm.h"
#include "../include/interrupt_ID.h"

/* This kernel module provides a character device driver that implements a real-time stopwatch.
 * The stopwatch time is written to the file /dev/stopwatch and is decremented every 1/100 
 * seconds by a timer interrupt. The time is provided in the format MM:SS:DD, where MM are
 * minutes, SS are seconds, and DD are hundredths of a second. The stopwatch stops when it 
 * reaches 00:00:00  */

// START of declarations needed for a character device
static int device_open (struct inode *, struct file *);
static int device_release (struct inode *, struct file *);
static ssize_t device_read (struct file *, char *, size_t, loff_t *);

#define SUCCESS 0
#define stopwatch_DEV "stopwatch"
#define stopwatch_BYTES 9                // number of bytes to return when read (includes a \n)

static struct file_operations stopwatch_fops = {
    .owner = THIS_MODULE,
    .read = device_read,
    .open = device_open,
    .release = device_release
};

static struct miscdevice stopwatch = { 
    .minor = MISC_DYNAMIC_MINOR, 
    .name = stopwatch_DEV,
    .fops = &stopwatch_fops,
    .mode = 0666
};

static int stopwatch_registered = 0;
// END of declarations needed for a character device

// START of declarations for the timer that makes interrupts every 1/100 second
void *LW_virtual;             // used to map physical addresses for the light-weight bridge
volatile int * timer0_ptr;    // virtual pointer to FPGA timer
static int time;              // stopwatch time
static int runtime = 1;       // run/stop the stopwatch
// END of declarations for the timer that makes interrupts every 1/100 second

/* The FPGA timer interrupt handler. It decrements the time */
irq_handler_t irq_handler(int irq, void *dev_id, struct pt_regs *regs) {
    // Clear the timer control register (clears current interrupt)
    *(timer0_ptr) = 0;           // clear the interrupt
   
    if (time != 0 && runtime)    // stop when time reaches 0 or timer is stopped
        time -= 1;

    return (irq_handler_t) IRQ_HANDLED;
}

static int __init start_stopwatch(void) {
    int err = 0;
    int counter;
    int ret_val;

    /* START of code to make a character device driver */
    err = misc_register (&stopwatch);
    if (err < 0) {
        printk (KERN_ERR "/dev/%s: misc_register() failed\n", stopwatch_DEV);
    }
    else {
        printk (KERN_INFO "/dev/%s driver registered\n", stopwatch_DEV);
        stopwatch_registered = 1;
    }

    /* END of code to make a character device driver */

    /* START of code to set up the interrupt timer */
    // register the interrupt handler
    ret_val = (int) request_irq (TIMER0_IRQ, (irq_handler_t) irq_handler, IRQF_SHARED,
        "timer_irq_handler", (void *) (irq_handler));

    time = 359999;        // initial time is 59:59:99
    // generate a virtual address for the FPGA lightweight bridge
    LW_virtual = ioremap_nocache (LW_BRIDGE_BASE, LW_BRIDGE_SPAN);

    // Set up the FPGA timer
    timer0_ptr = LW_virtual + TIMER0_BASE;    // FPGA timer base address

    // Set the timer period
    counter = 1000000;    // period = 1/(100 MHz) x (1 x 10^6) = 0.01 sec
    *(timer0_ptr + 0x2) = (counter & 0xFFFF);
    *(timer0_ptr + 0x3) = (counter >> 16) & 0xFFFF;

    // Start the timer, enable its interrupts
    *(timer0_ptr + 1) = 0x7;    // STOP = 0, START = 1, CONT = 1, ITO = 1 
    /* END of code to set up the interrupt timer */

    return 0;
}

/* Remove the character device driver, and stop the interrupt timer */
static void __exit stop_stopwatch(void) {
    /* Stop the timer */
    *(timer0_ptr + 1) = 0x8;    // STOP = 1, START = 0, CONT = 0, ITO = 0 
    iounmap (LW_virtual);
    free_irq (TIMER0_IRQ, (void*) irq_handler);
    /* Remove the device from the kernel */
    if (stopwatch_registered) {
        misc_deregister (&stopwatch);
        printk (KERN_INFO "/dev/%s driver de-registered\n", stopwatch_DEV);
    }
}

/* Called when a process opens the stopwatch */
static int device_open(struct inode *inode, struct file *file) {
    return SUCCESS;
}

/* Called when a process closes the stopwatch */
static int device_release(struct inode *inode, struct file *file) {
    return 0;
}

/* Called when a process reads from the stopwatch. If *offset = 0, the current value of
 * the stopwatch is copied back to the user, and stopwatch_BYTES is returned. But if 
 * *offset = stopwatch_BYTES, nothing is copied back to the user and 0 (EOF) is returned. */
static ssize_t device_read(struct file *filp, char *buffer, size_t length, loff_t *offset) {
    int DD, MM, SS;
    char themessage[stopwatch_BYTES + 1];   // extra byte is for a NULL terminator
    size_t bytes;

    bytes = stopwatch_BYTES - (*offset);    // has the time data been sent yet?
    if (bytes) {
        // print the time to themessage in format MM:SS:DD
        MM = time / 6000;   // MM = minutes (time is in hundredths of a second)
        DD = time % 6000;   // DD = seconds x 100
        SS = DD / 100;      // SS = seconds
        DD = DD % 100;      // DD = hundredths of a second
        sprintf(themessage, "%02d:%02d:%02d\n", MM, SS, DD);
        if (copy_to_user (buffer, themessage, bytes) != 0)
            printk (KERN_ERR "Error: copy_to_user unsuccessful\n");
    }
    (*offset) = bytes;      // keep track of number of bytes sent to the user
    return bytes;           // returns stopwatch_BYTES, or EOF
}

MODULE_LICENSE("GPL");
module_init (start_stopwatch);
module_exit (stop_stopwatch);
