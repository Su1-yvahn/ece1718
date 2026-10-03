#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include "address_map_arm.h"
#include "interrupt_ID.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Intel FPGA University Program");
MODULE_DESCRIPTION("Embedded Linux Stopwatch");


/* virtual address for FPGA lightweight bridge */
void *LW_virtual;

/* pointers to FPGA devices */
volatile int *timer0_ptr;
volatile int *HEX3_HEX0_ptr;
volatile int *HEX5_HEX4_ptr;
volatile int *KEY_ptr;
volatile int *SW_ptr;


/*
 * Stopwatch time is stored in hundredths of a second.
 *
 * 59:59:99 =
 * 59 * 60 * 100 + 59 * 100 + 99
 * = 359999
 */
int time = 359999;

/* 1 = running, 0 = paused */
int running = 1;


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
    0b01100111
};


/* display current stopwatch time on HEX displays */
void display_time(void)
{
    int min;
    int sec;
    int hundredths;

    /*
     * time is measured in hundredths of a second
     *
     * 1 minute = 60 * 100 = 6000 hundredths
     */

    min = time / 6000;

    /* display MM on HEX5 and HEX4 */
    *HEX5_HEX4_ptr = seg7[min / 10] << 8;
    *HEX5_HEX4_ptr |= seg7[min % 10];


    /*
     * Remove the minute part.
     * The remainder contains SS:DD.
     */
    hundredths = time % 6000;

    sec = hundredths / 100;

    /* display SS on HEX3 and HEX2 */
    *HEX3_HEX0_ptr = seg7[sec / 10] << 24;
    *HEX3_HEX0_ptr |= seg7[sec % 10] << 16;


    /* get DD */
    hundredths = hundredths % 100;

    /* display DD on HEX1 and HEX0 */
    *HEX3_HEX0_ptr |= seg7[hundredths / 10] << 8;
    *HEX3_HEX0_ptr |= seg7[hundredths % 10];
}


/*
 * FPGA Timer0 interrupt handler.
 *
 * Timer0 interrupts every 0.01 seconds.
 * If the stopwatch is running and is not already zero,
 * decrement it by one hundredth of a second.
 */
irq_handler_t timer_irq_handler(int irq, void *dev_id, struct pt_regs *regs)
{
    /* clear Timer0 interrupt */
    *(timer0_ptr) = 0;

    if (running && time > 0)
        time--;

    display_time();

    return (irq_handler_t) IRQ_HANDLED;
}


/*
 * KEY interrupt handler
 *
 * KEY0 -> toggle run/pause
 * KEY1 -> set DD using switches
 * KEY2 -> set SS using switches
 * KEY3 -> set MM using switches
 */
irq_handler_t key_irq_handler(int irq, void *dev_id, struct pt_regs *regs)
{
    int press;
    int value;
    int min;
    int sec;
    int hundredths;

    /*
     * KEY_ptr + 3 corresponds to the Edgecapture register.
     *
     * KEY0 -> bit 0
     * KEY1 -> bit 1
     * KEY2 -> bit 2
     * KEY3 -> bit 3
     */
    press = *(KEY_ptr + 3);


    /* ---------- KEY0: toggle running / paused ---------- */

    if (press & 0x1)
    {
        running ^= 1;
    }


    /* ---------- KEY1: set DD ---------- */

    if (press & 0x2)
    {
        /*
         * Read SW9-SW0.
         * Only lower 10 bits correspond to switches.
         */
        value = *SW_ptr & 0x3FF;

        /* DD maximum is 99 */
        if (value > 99)
            value = 99;

        /*
         * Preserve MM:SS and replace DD.
         *
         * Example:
         *
         * 12:34:56
         *
         * time / 100 removes 56
         * multiply by 100 gives XX:XX:00
         * then add new DD.
         */
        time = (time / 100) * 100 + value;
    }


    /* ---------- KEY2: set SS ---------- */

    if (press & 0x4)
    {
        value = *SW_ptr & 0x3FF;

        /* SS maximum is 59 */
        if (value > 59)
            value = 59;

        /*
         * Preserve MM and DD,
         * replace SS.
         */
        min = time / 6000;
        hundredths = time % 100;

        time = min * 6000
             + value * 100
             + hundredths;
    }


    /* ---------- KEY3: set MM ---------- */

    if (press & 0x8)
    {
        value = *SW_ptr & 0x3FF;

        /* MM maximum is 59 */
        if (value > 59)
            value = 59;

        /*
         * Preserve SS and DD,
         * replace MM.
         */
        sec = (time % 6000) / 100;
        hundredths = time % 100;

        time = value * 6000
             + sec * 100
             + hundredths;
    }


    /*
     * Clear the Edgecapture bits.
     *
     * Writing a 1 clears the corresponding captured edge.
     */
    *(KEY_ptr + 3) = press;


    /* immediately show any changed value */
    display_time();

    return (irq_handler_t) IRQ_HANDLED;
}


/* Device driver initialization */
static int __init initialize_stopwatch_handler(void)
{
    int counter;
    int ret_val;


    /*
     * Map FPGA lightweight bridge into kernel virtual memory.
     */
    LW_virtual = ioremap_nocache(
        LW_BRIDGE_BASE,
        LW_BRIDGE_SPAN
    );


    /* create pointers to FPGA devices */

    timer0_ptr = LW_virtual + TIMER0_BASE;

    HEX3_HEX0_ptr = LW_virtual + HEX3_HEX0_BASE;
    HEX5_HEX4_ptr = LW_virtual + HEX5_HEX4_BASE;

    KEY_ptr = LW_virtual + KEY_BASE;
    SW_ptr = LW_virtual + SW_BASE;


    /*
     * Clear HEX displays initially.
     */
    *HEX3_HEX0_ptr = 0;
    *HEX5_HEX4_ptr = 0;


    /*
     * Clear any old KEY edge-capture values
     * before enabling interrupts.
     */
    *(KEY_ptr + 3) = 0xF;


    /*
     * Register Timer0 interrupt handler.
     *
     * TIMER0_IRQ = 72
     */
    ret_val = request_irq(
        TIMER0_IRQ,
        (irq_handler_t) timer_irq_handler,
        IRQF_SHARED,
        "timer_irq_handler",
        (void *) timer_irq_handler
    );

    if (ret_val)
        return ret_val;


    /*
     * Register KEY interrupt handler.
     *
     * KEY_IRQ = 73
     */
    ret_val = request_irq(
        KEY_IRQ,
        (irq_handler_t) key_irq_handler,
        IRQF_SHARED,
        "key_irq_handler",
        (void *) key_irq_handler
    );

    if (ret_val)
    {
        free_irq(
            TIMER0_IRQ,
            (void *) timer_irq_handler
        );

        return ret_val;
    }


    /*
     * Enable interrupts from KEY0-KEY3.
     *
     * KEY interrupt-mask register is at:
     *
     * KEY_BASE + 0x08
     *
     * Since KEY_ptr is an int pointer:
     *
     * KEY_ptr + 2
     */
    *(KEY_ptr + 2) = 0xF;


    /*
     * Timer clock = 100 MHz.
     *
     * We want an interrupt every 0.01 s:
     *
     * 100,000,000 * 0.01 = 1,000,000
     */
    counter = 1000000;


    /*
     * Timer period low 16 bits.
     *
     * timer0_ptr + 2
     * -> offset 0x08
     */
    *(timer0_ptr + 2) = counter & 0xFFFF;


    /*
     * Timer period high 16 bits.
     *
     * timer0_ptr + 3
     * -> offset 0x0C
     */
    *(timer0_ptr + 3) =
        (counter >> 16) & 0xFFFF;


    /*
     * Start Timer0.
     *
     * 0x7 =
     *
     * START = 1
     * CONT  = 1
     * ITO   = 1
     */
    *(timer0_ptr + 1) = 0x7;


    /*
     * Stopwatch begins at 59:59:99
     * and immediately starts counting down.
     */
    time = 359999;
    running = 1;

    display_time();


    return 0;
}


/* Device driver cleanup */
static void __exit cleanup_stopwatch_handler(void)
{
    /*
     * Disable KEY interrupts.
     */
    *(KEY_ptr + 2) = 0;


    /*
     * Stop Timer0.
     *
     * STOP = 1
     */
    *(timer0_ptr + 1) = 0x8;


    /*
     * Clear any pending KEY edges.
     */
    *(KEY_ptr + 3) = 0xF;


    /*
     * Clear seven-segment displays.
     */
    *HEX3_HEX0_ptr = 0;
    *HEX5_HEX4_ptr = 0;


    /*
     * Unregister both interrupt handlers.
     */
    free_irq(
        TIMER0_IRQ,
        (void *) timer_irq_handler
    );

    free_irq(
        KEY_IRQ,
        (void *) key_irq_handler
    );


    /*
     * Release virtual-memory mapping.
     */
    iounmap(LW_virtual);
}


module_init(initialize_stopwatch_handler);
module_exit(cleanup_stopwatch_handler);