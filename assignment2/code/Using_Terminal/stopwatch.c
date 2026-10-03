#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/io.h>

#include "address_map_arm.h"
#include "interrupt_ID.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Intel FPGA University Program");
MODULE_DESCRIPTION("Embedded Linux Stopwatch - Terminal Version");


/* virtual address for FPGA lightweight bridge */
void *LW_virtual;


/* FPGA device pointers */
volatile int *timer0_ptr;
volatile int *KEY_ptr;
volatile int *SW_ptr;


/*
 * Stopwatch time in hundredths of a second.
 *
 * 59:59:99 = 359999 hundredths
 */
int time = 359999;


/* 1 = running, 0 = paused */
int running = 1;


/*
 * Print stopwatch time to the kernel terminal/log.
 */
void print_time(void)
{
    int min;
    int sec;
    int hundredths;

    min = time / 6000;

    sec = (time % 6000) / 100;

    hundredths = time % 100;

    printk(KERN_INFO "Stopwatch: %02d:%02d:%02d\n",
           min, sec, hundredths);
}


/*
 * FPGA Timer0 interrupt handler.
 *
 * Timer fires every 0.01 second.
 */
irq_handler_t timer_irq_handler(
    int irq,
    void *dev_id,
    struct pt_regs *regs)
{
    /*
     * Clear timer interrupt.
     */
    *(timer0_ptr) = 0;


    /*
     * Decrement only if:
     *
     * 1. stopwatch is running
     * 2. time has not reached 00:00:00
     */
    if (running && time > 0)
        time--;


    /*
     * IMPORTANT:
     *
     * Do NOT print here.
     *
     * This ISR runs 100 times per second.
     * Printing here would flood the terminal/kernel log.
     */

    return (irq_handler_t) IRQ_HANDLED;
}


/*
 * KEY interrupt handler
 *
 * KEY0 -> run/pause
 * KEY1 -> set DD
 * KEY2 -> set SS
 * KEY3 -> set MM
 */
irq_handler_t key_irq_handler(
    int irq,
    void *dev_id,
    struct pt_regs *regs)
{
    int press;
    int value;

    int min;
    int sec;
    int hundredths;


    /*
     * Read KEY Edgecapture register.
     *
     * KEY_ptr + 3 corresponds to offset 0x0C.
     */
    press = *(KEY_ptr + 3);


    /*
     * ==============================
     * KEY0
     * Toggle running / paused
     * ==============================
     */
    if (press & 0x1)
    {
        running ^= 1;

        if (running)
            printk(KERN_INFO "Stopwatch running\n");
        else
            printk(KERN_INFO "Stopwatch paused\n");

        print_time();
    }


    /*
     * ==============================
     * KEY1
     * Set DD using SW switches
     * ==============================
     */
    if (press & 0x2)
    {
        /*
         * Read SW9-SW0.
         */
        value = *SW_ptr & 0x3FF;


        /*
         * DD maximum = 99
         */
        if (value > 99)
            value = 99;


        /*
         * Preserve MM:SS and replace DD.
         */
        time = (time / 100) * 100 + value;


        /*
         * Print current stopwatch time.
         */
        print_time();
    }


    /*
     * ==============================
     * KEY2
     * Set SS using SW switches
     * ==============================
     */
    if (press & 0x4)
    {
        value = *SW_ptr & 0x3FF;


        /*
         * SS maximum = 59
         */
        if (value > 59)
            value = 59;


        /*
         * Preserve MM and DD.
         */
        min = time / 6000;

        hundredths = time % 100;


        /*
         * Replace SS.
         */
        time =
            min * 6000 +
            value * 100 +
            hundredths;


        print_time();
    }


    /*
     * ==============================
     * KEY3
     * Set MM using SW switches
     * ==============================
     */
    if (press & 0x8)
    {
        value = *SW_ptr & 0x3FF;


        /*
         * MM maximum = 59
         */
        if (value > 59)
            value = 59;


        /*
         * Preserve SS and DD.
         */
        sec = (time % 6000) / 100;

        hundredths = time % 100;


        /*
         * Replace MM.
         */
        time =
            value * 6000 +
            sec * 100 +
            hundredths;


        print_time();
    }


    /*
     * Clear captured KEY interrupts.
     *
     * Write 1 to the captured bits to clear them.
     */
    *(KEY_ptr + 3) = press;


    return (irq_handler_t) IRQ_HANDLED;
}


/*
 * Module initialization
 */
static int __init initialize_stopwatch_handler(void)
{
    int counter;
    int ret_val;


    /*
     * Map FPGA lightweight bridge into
     * kernel virtual address space.
     */
    LW_virtual = ioremap_nocache(
        LW_BRIDGE_BASE,
        LW_BRIDGE_SPAN
    );


    if (LW_virtual == NULL)
    {
        printk(KERN_ERR "Could not map FPGA lightweight bridge\n");
        return -ENOMEM;
    }


    /*
     * Device pointers.
     */
    timer0_ptr = LW_virtual + TIMER0_BASE;

    KEY_ptr = LW_virtual + KEY_BASE;

    SW_ptr = LW_virtual + SW_BASE;


    /*
     * Clear old KEY edge-capture bits.
     */
    *(KEY_ptr + 3) = 0xF;


    /*
     * Register Timer0 interrupt.
     */
    ret_val = request_irq(
        TIMER0_IRQ,
        (irq_handler_t) timer_irq_handler,
        IRQF_SHARED,
        "timer_irq_handler",
        (void *) timer_irq_handler
    );


    if (ret_val)
    {
        printk(KERN_ERR "Could not register Timer0 IRQ\n");

        iounmap(LW_virtual);

        return ret_val;
    }


    /*
     * Register KEY interrupt.
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
        printk(KERN_ERR "Could not register KEY IRQ\n");

        free_irq(
            TIMER0_IRQ,
            (void *) timer_irq_handler
        );

        iounmap(LW_virtual);

        return ret_val;
    }


    /*
     * Enable interrupts for:
     *
     * KEY0
     * KEY1
     * KEY2
     * KEY3
     */
    *(KEY_ptr + 2) = 0xF;


    /*
     * Timer clock = 100 MHz.
     *
     * Want:
     *
     * interrupt every 0.01 s
     *
     * therefore:
     *
     * 100,000,000 * 0.01
     * = 1,000,000
     */
    counter = 1000000;


    /*
     * Counter start value low 16 bits.
     */
    *(timer0_ptr + 2) =
        counter & 0xFFFF;


    /*
     * Counter start value high 16 bits.
     */
    *(timer0_ptr + 3) =
        (counter >> 16) & 0xFFFF;


    /*
     * Start timer:
     *
     * START = 1
     * CONT  = 1
     * ITO   = 1
     */
    *(timer0_ptr + 1) = 0x7;


    /*
     * Initial stopwatch value.
     */
    time = 359999;

    running = 1;


    printk(KERN_INFO "Stopwatch module loaded\n");

    print_time();


    return 0;
}


/*
 * Module cleanup
 */
static void __exit cleanup_stopwatch_handler(void)
{
    /*
     * Disable KEY interrupts.
     */
    *(KEY_ptr + 2) = 0;


    /*
     * Stop timer.
     */
    *(timer0_ptr + 1) = 0x8;


    /*
     * Clear pending KEY edges.
     */
    *(KEY_ptr + 3) = 0xF;


    /*
     * Free IRQs.
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
     * Release FPGA mapping.
     */
    iounmap(LW_virtual);


    printk(KERN_INFO "Stopwatch module removed\n");
}


module_init(initialize_stopwatch_handler);
module_exit(cleanup_stopwatch_handler);