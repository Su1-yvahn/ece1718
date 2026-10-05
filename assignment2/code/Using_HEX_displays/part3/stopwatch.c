#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include "../address_map_arm.h"
#include "../interrupt_ID.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Intel FPGA University Program");
MODULE_DESCRIPTION("Embedded Linux Stopwatch");

void *LW_virtual; // virtual address for FPGA lightweight bridge

volatile int *timer0_ptr;
volatile int *HEX3_HEX0_ptr;
volatile int *HEX5_HEX4_ptr;
volatile int *KEY_ptr;
volatile int *SW_ptr;

int time = 359999; // Stopwatch time is stored in hundredths of a second. 59:59:99 = 59 * 60 * 100 + 59 * 100 + 99 = 359999
int running = 1;   // 1 = running, 0 = paused

/* seven-segment display codes for digits 0-9 */
char seg7[10] = {
    0b00111111,
    0b00000110,
    0b01011011,
    0b01001111,
    0b01100110,
    0b01101101,
    0b01111101,
    0b00000111,
    0b01111111,
    0b01100111};

// display current stopwatch time on HEX displays
void display_time(void)
{
    int min;
    int sec;
    int hundredths;

    min = time / 6000; // time is measured in hundredths of a second; 1 minute = 60 * 100 = 6000 hundredths

    // display MM on HEX5 and HEX4
    *HEX5_HEX4_ptr = seg7[min / 10] << 8;
    *HEX5_HEX4_ptr |= seg7[min % 10];

    // Remove the minute part. The remainder contains SS:DD
    hundredths = time % 6000;
    sec = hundredths / 100;

    // display SS on HEX3 and HEX2
    *HEX3_HEX0_ptr = seg7[sec / 10] << 24;
    *HEX3_HEX0_ptr |= seg7[sec % 10] << 16;

    // get DD
    hundredths = hundredths % 100;

    // display DD on HEX1 and HEX0
    *HEX3_HEX0_ptr |= seg7[hundredths / 10] << 8;
    *HEX3_HEX0_ptr |= seg7[hundredths % 10];
}

// FPGA Timer0 interrupt handler. Timer clock = 100 MHz; Timer0 interrupts every 0.01 seconds. decrement it by one hundredth of a second.
irq_handler_t timer_irq_handler(int irq, void *dev_id, struct pt_regs *regs)
{
    // clear Timer0 interrupt
    *(timer0_ptr) = 0;

    if (running && time > 0)
    {
        time--;
    }
    display_time();

    return (irq_handler_t)IRQ_HANDLED;
}

/*
KEY interrupt handler
KEY0: toggle run/pause
KEY1: set DD using switches
KEY2: set SS using switches
KEY3: set MM using switches
*/
irq_handler_t key_irq_handler(int irq, void *dev_id, struct pt_regs *regs)
{
    int press;
    int value;
    int min;
    int sec;
    int hundredths;

    press = *(KEY_ptr + 3); // KEY_ptr + 3: Edgecapture register

    if (press & 0x1) // capture KEY0
    {
        running ^= 1; // toggle running / paused
    }

    if (press & 0x2) // capture KEY1
    {
        value = *SW_ptr & 0x3FF; // Read SW9-SW0. extract lower 10 bits

        if (value > 99) // set DD maximum: 99
            value = 99;

        time = (time / 100) * 100 + value; // set DD
    }

    if (press & 0x4) // capture KEY2
    {
        value = *SW_ptr & 0x3FF; // Read SW9-SW0. extract lower 10 bits

        if (value > 59) // set SS maximum: 59
            value = 59;

        // Preserve MM and DD, set SS
        min = time / 6000;
        hundredths = time % 100;

        time = min * 6000 + value * 100 + hundredths;
    }

    if (press & 0x8) // capture KEY3
    {
        value = *SW_ptr & 0x3FF; // Read SW9-SW0. extract lower 10 bits

        if (value > 59) // set MM maximum: 59
            value = 59;

        // Preserve SS and DD, set MM.
        sec = (time % 6000) / 100;
        hundredths = time % 100;

        time = value * 6000 + sec * 100 + hundredths;
    }

    // Clear the Edgecapture bits
    *(KEY_ptr + 3) = press;
    display_time(); // show any changed value

    return (irq_handler_t)IRQ_HANDLED;
}

// Device driver initialization for insmod
static int __init initialize_stopwatch_handler(void)
{
    int counter;
    int ret_val;

    // Map FPGA lightweight bridge into kernel virtual memory.
    LW_virtual = ioremap_nocache(LW_BRIDGE_BASE, LW_BRIDGE_SPAN);

    // pointers Initialization
    timer0_ptr = LW_virtual + TIMER0_BASE;
    HEX3_HEX0_ptr = LW_virtual + HEX3_HEX0_BASE;
    HEX5_HEX4_ptr = LW_virtual + HEX5_HEX4_BASE;
    KEY_ptr = LW_virtual + KEY_BASE;
    SW_ptr = LW_virtual + SW_BASE;

    // Clear HEX displays initially
    *HEX3_HEX0_ptr = 0;
    *HEX5_HEX4_ptr = 0;

    *(KEY_ptr + 3) = 0xF; // Clear any old KEY edge-capture values before enabling interrupts.

    // Register Timer0 interrupt handler. TIMER0_IRQ = 72
    ret_val = request_irq(TIMER0_IRQ, (irq_handler_t)timer_irq_handler, IRQF_SHARED, "timer_irq_handler", (void *)timer_irq_handler);
    if (ret_val)
    {
        return ret_val;
    }

    // Register KEY interrupt handler. KEY_IRQ = 73
    ret_val = request_irq(KEY_IRQ, (irq_handler_t)key_irq_handler, IRQF_SHARED, "key_irq_handler", (void *)key_irq_handler);
    if (ret_val)
    {
        free_irq(TIMER0_IRQ, (void *)timer_irq_handler);
        return ret_val;
    }

    // Enable interrupts from KEY0-KEY3
    *(KEY_ptr + 2) = 0xF; // KEY_ptr is an int pointer

    // Timer clock = 100 MHz. want an interrupt every 0.01 s; 100,000,000 * 0.01 = 1,000,000
    counter = 1000000;
    // Set Counter start value (low)
    *(timer0_ptr + 2) = counter & 0xFFFF;
    // Set Counter start value (high)
    *(timer0_ptr + 3) = (counter >> 16) & 0xFFFF;
    // Set Control register of Timer0.
    *(timer0_ptr + 1) = 0x7; // START = 1; CONT  = 1; ITO   = 1

    // Stopwatch begins at 59:59:99 and immediately starts counting down
    time = 359999;
    running = 1;

    display_time();
    return 0;
}

// Device driver cleanup for rmmod
static void __exit cleanup_stopwatch_handler(void)
{
    // Disable KEY interrupts
    *(KEY_ptr + 2) = 0;

    // Stop Timer0
    *(timer0_ptr + 1) = 0x8; // STOP = 1

    // Clear KEY edge capture register
    *(KEY_ptr + 3) = 0xF;

    // Clear seven-segment displays
    *HEX3_HEX0_ptr = 0;
    *HEX5_HEX4_ptr = 0;

    // Unregister both interrupt handlers
    free_irq(TIMER0_IRQ, (void *)timer_irq_handler);
    free_irq(KEY_IRQ, (void *)key_irq_handler);

    // Release virtual-memory mapping
    iounmap(LW_virtual);
}

module_init(initialize_stopwatch_handler);
module_exit(cleanup_stopwatch_handler);