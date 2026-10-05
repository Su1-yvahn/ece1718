#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/io.h>

#include "../address_map_arm.h"
#include "../interrupt_ID.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Intel FPGA University Program");
MODULE_DESCRIPTION("Embedded Linux Stopwatch - VT100 Terminal Version");

void *LW_virtual;
volatile int *timer0_ptr;
volatile int *KEY_ptr;
volatile int *SW_ptr;

int time = 359999; // Time stored in hundredths of a second; 59:59:99 = 359999
int running = 1;   // 1 -> running; 0 -> paused / setting

/*  Digit-setting order:
    0: DD ones
    1: DD tens
    2: SS ones
    3: SS tens
    4: MM ones
    5: MM tens
*/
int set_digit = 0;

// VT100 colors
#define VT100_RED "\033[31m"
#define VT100_GREEN "\033[32m"
#define VT100_RESET "\033[0m"

// Print current stopwatch time
void print_time(void)
{
    int min;
    int sec;
    int hundredths;

    min = time / 6000;
    sec = (time % 6000) / 100;
    hundredths = time % 100;

    // Running -> green; Paused  -> red
    if (running)
    {
        printk(KERN_ALERT VT100_GREEN "%02d:%02d:%02d" VT100_RESET "\n", min, sec, hundredths);
    }
    else
    {
        printk(KERN_ALERT VT100_RED "%02d:%02d:%02d" VT100_RESET "\n", min, sec, hundredths);
    }
}

// Set one stopwatch digit
void set_stopwatch_digit(int position, int switch_value)
{
    int min;
    int sec;
    int hundredths;

    int min_tens;
    int min_ones;

    int sec_tens;
    int sec_ones;

    int dd_tens;
    int dd_ones;

    int digit;

    // Extract MM:SS:DD
    min = time / 6000;
    sec = (time % 6000) / 100; // sec = time - (min × 6000) / 100
    hundredths = time % 100;

    // Split into six digits
    min_tens = min / 10;
    min_ones = min % 10;

    sec_tens = sec / 10;
    sec_ones = sec % 10;

    dd_tens = hundredths / 10;
    dd_ones = hundredths % 10;

    // Change exactly one digit
    switch (position)
    {
    case 0:
        // DD ones: range: 0-9
        digit = switch_value;

        if (digit > 9)
            digit = 9;

        dd_ones = digit;
        break;

    case 1:
        // DD tens: range: 0-9
        digit = switch_value;

        if (digit > 9)
            digit = 9;

        dd_tens = digit;
        break;

    case 2:
        // SS ones: range: 0-9
        digit = switch_value;

        if (digit > 9)
            digit = 9;

        sec_ones = digit;
        break;

    case 3:
        // SS tens: range: 0-5; keeps seconds <= 59
        digit = switch_value;

        if (digit > 5)
            digit = 5;

        sec_tens = digit;
        break;

    case 4:
        // MM ones: range: 0-9
        digit = switch_value;

        if (digit > 9)
            digit = 9;

        min_ones = digit;
        break;

    case 5:
        // MM tens: range: 0-5; keeps minutes <= 59.
        digit = switch_value;

        if (digit > 5)
            digit = 5;

        min_tens = digit;
        break;
    }

    // Reconstruct MM, SS, DD
    min = min_tens * 10 + min_ones;
    sec = sec_tens * 10 + sec_ones;
    hundredths = dd_tens * 10 + dd_ones;

    // Reconstruct total time
    time = min * 6000 + sec * 100 + hundredths;
}

// Timer interrupt handler
irq_handler_t timer_irq_handler(int irq, void *dev_id, struct pt_regs *regs)
{
    // Clear Timer0 interrupt.
    *(timer0_ptr) = 0;

    // Count down only while running.
    if (running && time > 0)
    {
        time--;
    }

    // no print
    // Timer interrupt occurs every 0.01 sec
    return (irq_handler_t)IRQ_HANDLED;
}

// KEY interrupt handler
// KEY0: toggle run/pause; no printing
// KEY1: running -> print current time; paused  -> set one digit, then print

irq_handler_t key_irq_handler(int irq, void *dev_id, struct pt_regs *regs)
{
    int press;
    int switch_value;

    // Read KEY Edgecapture register
    press = *(KEY_ptr + 3);

    if (press & 0x1) // Extract Edgecapture register of KEY0
    {
        if (running) // pause if running
        {
            running = 0;
            set_digit = 0; // Begin setting from the rightmost digit again
        }

        else // run if currently pause
        {
            running = 1;
        }
    }

    if (press & 0x2) // Extract Edgecapture register of KEY1
    {
        if (running) // only display current time if currently running
        {
            print_time();
        }

        else // set exactly one digit if paused
        {
            switch_value = *SW_ptr & 0x3FF;               // extract SW value(lower 10-bits) 0x3FF = 0b1111111111
            set_stopwatch_digit(set_digit, switch_value); // Set current digit

            print_time(); // color is red because paused

            set_digit++; // Move to next digit

            if (set_digit == 6) // After six digits, return to the rightmost digit.
            {
                set_digit = 0;
            }
        }
    }

    // Clear KEY Edgecapture bits.
    *(KEY_ptr + 3) = press;

    return (irq_handler_t)IRQ_HANDLED;
}

// Module initialization for insmod
static int __init initialize_stopwatch_handler(void)
{
    int counter;
    int ret_val;

    // Map FPGA lightweight bridge
    LW_virtual = ioremap_nocache(LW_BRIDGE_BASE, LW_BRIDGE_SPAN);

    if (LW_virtual == NULL)
    {
        printk(KERN_ERR "Could not map FPGA lightweight bridge\n");
        return -ENOMEM;
    }

    timer0_ptr = LW_virtual + TIMER0_BASE;
    KEY_ptr = LW_virtual + KEY_BASE;
    SW_ptr = LW_virtual + SW_BASE;

    // Clear Edge Capture Register of KEYs
    *(KEY_ptr + 3) = 0xF;

    // Register Timer0 interrupt
    ret_val = request_irq(TIMER0_IRQ, (irq_handler_t)timer_irq_handler, IRQF_SHARED, "timer_irq_handler", (void *)timer_irq_handler);

    if (ret_val)
    {
        printk(KERN_ERR "Could not register TIMER0 interrupt\n");
        iounmap(LW_virtual);
        return ret_val;
    }

    // Register KEY interrupt
    ret_val = request_irq(KEY_IRQ, (irq_handler_t)key_irq_handler, IRQF_SHARED, "key_irq_handler", (void *)key_irq_handler);

    if (ret_val)
    {
        printk(KERN_ERR "Could not register KEY interrupt\n");
        free_irq(TIMER0_IRQ, (void *)timer_irq_handler);
        iounmap(LW_virtual);
        return ret_val;
    }

    *(KEY_ptr + 2) = 0x3; // Enable only KEY0 and KEY1 interrupts

    // Configure Timer0
    // one unit of time represents one hundredth of a second (1 unit of time = 1/100 second = 10ms)
    counter = 1000000;                            // Timer clock = 100 MHz; interrupt period = 0.01 sec; 100,000,000 * 0.01 = 1,000,000
    *(timer0_ptr + 2) = counter & 0xFFFF;         // Set Counter start value (low) Low 16 bits
    *(timer0_ptr + 3) = (counter >> 16) & 0xFFFF; // Set Counter start value (high) high 16 bits
    *(timer0_ptr + 1) = 0x7;                      // set control register: START = 1; CONT = 1; ITO = 1

    // Initial stopwatch state
    time = 359999;
    running = 1;
    set_digit = 0;

    // no print here
    return 0;
}

// Module cleanup for rmmod
static void __exit cleanup_stopwatch_handler(void)
{
    *(KEY_ptr + 2) = 0;                              // Disable KEY interrupts
    *(timer0_ptr + 1) = 0x8;                         // Stop Timer0
    *(KEY_ptr + 3) = 0xF;                            // clear ECR
    free_irq(TIMER0_IRQ, (void *)timer_irq_handler); // Free Timer0 IRQ
    free_irq(KEY_IRQ, (void *)key_irq_handler);      // Free KEY IRQ
    iounmap(LW_virtual);                             // Release FPGA memory mapping
}

// Register module
module_init(initialize_stopwatch_handler);
module_exit(cleanup_stopwatch_handler);