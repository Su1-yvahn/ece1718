#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/io.h>

#include "address_map_arm.h"
#include "interrupt_ID.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Intel FPGA University Program");
MODULE_DESCRIPTION("Embedded Linux Stopwatch - VT100 Terminal Version");


/* ============================================================
 * FPGA device pointers
 * ============================================================
 */

void *LW_virtual;

volatile int *timer0_ptr;
volatile int *KEY_ptr;
volatile int *SW_ptr;


/* ============================================================
 * Stopwatch state
 * ============================================================
 */

/*
 * Time stored in hundredths of a second.
 *
 * 59:59:99 = 359999
 */
int time = 359999;


/*
 * running = 1 -> running
 * running = 0 -> paused / setting
 */
int running = 1;


/*
 * Selected digit:
 *
 * 0 -> DD ones
 * 1 -> DD tens
 * 2 -> SS ones
 * 3 -> SS tens
 * 4 -> MM ones
 * 5 -> MM tens
 *
 * Display:
 *
 * MM : SS : DD
 * 54   32   10
 */
int set_digit = 0;


/* ============================================================
 * VT100 colors
 * ============================================================
 */

#define VT100_RED       "\033[31m"
#define VT100_GREEN     "\033[32m"
#define VT100_YELLOW    "\033[33m"
#define VT100_RESET     "\033[0m"


/* ============================================================
 * Print current stopwatch time
 * ============================================================
 */

void print_time(void)
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


    /* --------------------------------------------------------
     * Extract MM:SS:DD
     * --------------------------------------------------------
     */

    min = time / 6000;
    sec = (time % 6000) / 100;
    hundredths = time % 100;


    /* --------------------------------------------------------
     * Split into six digits
     * --------------------------------------------------------
     */

    min_tens = min / 10;
    min_ones = min % 10;

    sec_tens = sec / 10;
    sec_ones = sec % 10;

    dd_tens = hundredths / 10;
    dd_ones = hundredths % 10;


    /* --------------------------------------------------------
     * Running:
     *
     * Display entire time in GREEN.
     * --------------------------------------------------------
     */

    if (running)
    {
        printk(KERN_ALERT
               VT100_GREEN
               "%d%d:%d%d:%d%d"
               VT100_RESET
               "\n",
               min_tens,
               min_ones,
               sec_tens,
               sec_ones,
               dd_tens,
               dd_ones);

        return;
    }


    /* --------------------------------------------------------
     * Paused / setting:
     *
     * Normal digits  -> RED
     * Selected digit -> YELLOW
     *
     * Digit positions:
     *
     *      5 4   3 2   1 0
     *      M M : S S : D D
     * --------------------------------------------------------
     */

    printk(KERN_ALERT

           /* MM tens */
           "%s%d"

           /* MM ones */
           "%s%d"

           /* first ':' */
           VT100_RED ":"

           /* SS tens */
           "%s%d"

           /* SS ones */
           "%s%d"

           /* second ':' */
           VT100_RED ":"

           /* DD tens */
           "%s%d"

           /* DD ones */
           "%s%d"

           VT100_RESET
           "\n",

           /*
            * MM tens -> position 5
            */
           (set_digit == 5)
               ? VT100_YELLOW
               : VT100_RED,
           min_tens,

           /*
            * MM ones -> position 4
            */
           (set_digit == 4)
               ? VT100_YELLOW
               : VT100_RED,
           min_ones,

           /*
            * SS tens -> position 3
            */
           (set_digit == 3)
               ? VT100_YELLOW
               : VT100_RED,
           sec_tens,

           /*
            * SS ones -> position 2
            */
           (set_digit == 2)
               ? VT100_YELLOW
               : VT100_RED,
           sec_ones,

           /*
            * DD tens -> position 1
            */
           (set_digit == 1)
               ? VT100_YELLOW
               : VT100_RED,
           dd_tens,

           /*
            * DD ones -> position 0
            */
           (set_digit == 0)
               ? VT100_YELLOW
               : VT100_RED,
           dd_ones);
}


/* ============================================================
 * Set one stopwatch digit
 * ============================================================
 */

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


    /* --------------------------------------------------------
     * Extract MM:SS:DD
     * --------------------------------------------------------
     */

    min = time / 6000;
    sec = (time % 6000) / 100;
    hundredths = time % 100;


    /* --------------------------------------------------------
     * Split into six digits
     * --------------------------------------------------------
     */

    min_tens = min / 10;
    min_ones = min % 10;

    sec_tens = sec / 10;
    sec_ones = sec % 10;

    dd_tens = hundredths / 10;
    dd_ones = hundredths % 10;


    /* --------------------------------------------------------
     * Change exactly one digit
     * --------------------------------------------------------
     */

    switch (position)
    {
        case 0:
            /*
             * DD ones
             *
             * range: 0-9
             */
            digit = switch_value;

            if (digit > 9)
                digit = 9;

            dd_ones = digit;

            break;


        case 1:
            /*
             * DD tens
             *
             * range: 0-9
             */
            digit = switch_value;

            if (digit > 9)
                digit = 9;

            dd_tens = digit;

            break;


        case 2:
            /*
             * SS ones
             *
             * range: 0-9
             */
            digit = switch_value;

            if (digit > 9)
                digit = 9;

            sec_ones = digit;

            break;


        case 3:
            /*
             * SS tens
             *
             * range: 0-5
             */
            digit = switch_value;

            if (digit > 5)
                digit = 5;

            sec_tens = digit;

            break;


        case 4:
            /*
             * MM ones
             *
             * range: 0-9
             */
            digit = switch_value;

            if (digit > 9)
                digit = 9;

            min_ones = digit;

            break;


        case 5:
            /*
             * MM tens
             *
             * range: 0-5
             */
            digit = switch_value;

            if (digit > 5)
                digit = 5;

            min_tens = digit;

            break;
    }


    /* --------------------------------------------------------
     * Reconstruct MM, SS, DD
     * --------------------------------------------------------
     */

    min =
        min_tens * 10
        + min_ones;


    sec =
        sec_tens * 10
        + sec_ones;


    hundredths =
        dd_tens * 10
        + dd_ones;


    /* --------------------------------------------------------
     * Reconstruct total time
     * --------------------------------------------------------
     */

    time =
        min * 6000
        + sec * 100
        + hundredths;
}


/* ============================================================
 * Timer interrupt handler
 * ============================================================
 */

irq_handler_t timer_irq_handler(
    int irq,
    void *dev_id,
    struct pt_regs *regs)
{
    /*
     * Clear Timer0 interrupt.
     */
    *(timer0_ptr) = 0;


    /*
     * Count down only while running.
     */
    if (running && time > 0)
    {
        time--;
    }


    /*
     * Do not print here.
     *
     * Timer interrupt occurs every 0.01 sec.
     */

    return (irq_handler_t) IRQ_HANDLED;
}


/* ============================================================
 * KEY interrupt handler
 * ============================================================
 *
 * KEY0:
 *      toggle run / pause
 *      no printing
 *
 * KEY1:
 *      running -> print current time
 *      paused  -> set currently selected digit
 *
 * KEY2:
 *      paused -> move selection RIGHT
 *
 * KEY3:
 *      paused -> move selection LEFT
 *
 * ============================================================
 */

irq_handler_t key_irq_handler(
    int irq,
    void *dev_id,
    struct pt_regs *regs)
{
    int press;
    int switch_value;


    /*
     * Read KEY Edgecapture register.
     */
    press = *(KEY_ptr + 3);


    /* ========================================================
     * KEY0
     *
     * Toggle run / pause.
     *
     * KEY0 does NOT print.
     * ========================================================
     */

    if (press & 0x1)
    {
        /*
         * Running -> paused
         */
        if (running)
        {
            running = 0;


            /*
             * Begin selection at the
             * rightmost digit.
             */
            set_digit = 0;
        }


        /*
         * Paused -> running
         */
        else
        {
            running = 1;
        }
    }


    /* ========================================================
     * KEY1
     *
     * Running:
     *      print current time
     *
     * Paused:
     *      set currently selected digit
     * ========================================================
     */

    if (press & 0x2)
    {
        /*
         * ----------------------------------------------------
         * If running:
         *
         * only display current time.
         * ----------------------------------------------------
         */
        if (running)
        {
            print_time();
        }


        /*
         * ----------------------------------------------------
         * If paused:
         *
         * set exactly the currently selected digit.
         * ----------------------------------------------------
         */
        else
        {
            /*
             * Read SW9-SW0 as one 10-bit
             * binary number.
             *
             * 0x3FF =
             * 0b1111111111
             */
            switch_value =
                *SW_ptr & 0x3FF;


            /*
             * Modify only current selected digit.
             *
             * The function automatically clamps:
             *
             * normal digit -> max 9
             * tens of MM/SS -> max 5
             */
            set_stopwatch_digit(
                set_digit,
                switch_value
            );


            /*
             * Print updated time.
             *
             * Selected digit remains selected
             * and is displayed in YELLOW.
             */
            print_time();
        }
    }


    /* ========================================================
     * KEY2
     *
     * Move selected digit RIGHT.
     *
     * Display:
     *
     *      5 4   3 2   1 0
     *      M M : S S : D D
     *
     * Moving RIGHT means:
     *
     *      set_digit--
     *
     * Only active while paused.
     * ========================================================
     */

    if (press & 0x4)
    {
        if (!running)
        {
            /*
             * Do not move beyond
             * the rightmost digit.
             */
            if (set_digit > 0)
            {
                set_digit--;
            }


            /*
             * Display current selection.
             */
            print_time();
        }
    }


    /* ========================================================
     * KEY3
     *
     * Move selected digit LEFT.
     *
     * Display:
     *
     *      5 4   3 2   1 0
     *      M M : S S : D D
     *
     * Moving LEFT means:
     *
     *      set_digit++
     *
     * Only active while paused.
     * ========================================================
     */

    if (press & 0x8)
    {
        if (!running)
        {
            /*
             * Do not move beyond
             * the leftmost digit.
             */
            if (set_digit < 5)
            {
                set_digit++;
            }


            /*
             * Display current selection.
             */
            print_time();
        }
    }


    /*
     * Clear KEY Edgecapture bits.
     *
     * Writing 1 clears the captured bit.
     */
    *(KEY_ptr + 3) = press;


    return (irq_handler_t) IRQ_HANDLED;
}


/* ============================================================
 * Module initialization
 * ============================================================
 */

static int __init initialize_stopwatch_handler(void)
{
    int counter;
    int ret_val;


    /* --------------------------------------------------------
     * Map FPGA lightweight bridge
     * --------------------------------------------------------
     */

    LW_virtual =
        ioremap_nocache(
            LW_BRIDGE_BASE,
            LW_BRIDGE_SPAN
        );


    if (LW_virtual == NULL)
    {
        printk(KERN_ERR
               "Could not map FPGA lightweight bridge\n");

        return -ENOMEM;
    }


    /* --------------------------------------------------------
     * FPGA pointers
     * --------------------------------------------------------
     */

    timer0_ptr =
        LW_virtual + TIMER0_BASE;


    KEY_ptr =
        LW_virtual + KEY_BASE;


    SW_ptr =
        LW_virtual + SW_BASE;


    /* --------------------------------------------------------
     * Clear old KEY events
     * --------------------------------------------------------
     */

    *(KEY_ptr + 3) = 0xF;


    /* --------------------------------------------------------
     * Register Timer0 interrupt
     * --------------------------------------------------------
     */

    ret_val =
        request_irq(
            TIMER0_IRQ,
            (irq_handler_t) timer_irq_handler,
            IRQF_SHARED,
            "timer_irq_handler",
            (void *) timer_irq_handler
        );


    if (ret_val)
    {
        printk(KERN_ERR
               "Could not register TIMER0 interrupt\n");

        iounmap(LW_virtual);

        return ret_val;
    }


    /* --------------------------------------------------------
     * Register KEY interrupt
     * --------------------------------------------------------
     */

    ret_val =
        request_irq(
            KEY_IRQ,
            (irq_handler_t) key_irq_handler,
            IRQF_SHARED,
            "key_irq_handler",
            (void *) key_irq_handler
        );


    if (ret_val)
    {
        printk(KERN_ERR
               "Could not register KEY interrupt\n");


        free_irq(
            TIMER0_IRQ,
            (void *) timer_irq_handler
        );


        iounmap(LW_virtual);

        return ret_val;
    }


    /* --------------------------------------------------------
     * Enable KEY0, KEY1, KEY2, KEY3
     * --------------------------------------------------------
     *
     * bit 0 -> KEY0
     * bit 1 -> KEY1
     * bit 2 -> KEY2
     * bit 3 -> KEY3
     *
     * 0b1111 = 0xF
     */

    *(KEY_ptr + 2) = 0xF;


    /* --------------------------------------------------------
     * Configure Timer0
     * --------------------------------------------------------
     *
     * Timer clock = 100 MHz
     *
     * desired interrupt period = 0.01 sec
     *
     * 100,000,000 * 0.01
     * = 1,000,000
     */

    counter = 1000000;


    /*
     * Low 16 bits.
     */
    *(timer0_ptr + 2) =
        counter & 0xFFFF;


    /*
     * High 16 bits.
     */
    *(timer0_ptr + 3) =
        (counter >> 16) & 0xFFFF;


    /* --------------------------------------------------------
     * Start timer
     * --------------------------------------------------------
     *
     * START = 1
     * CONT  = 1
     * ITO   = 1
     *
     * 0x7
     */

    *(timer0_ptr + 1) = 0x7;


    /* --------------------------------------------------------
     * Initial stopwatch state
     * --------------------------------------------------------
     */

    time = 359999;

    running = 1;

    set_digit = 0;


    /*
     * Do NOT print here.
     *
     * insmod stopwatch.ko
     *
     * should not print the stopwatch time.
     */


    return 0;
}


/* ============================================================
 * Module cleanup
 * ============================================================
 */

static void __exit cleanup_stopwatch_handler(void)
{
    /*
     * Disable KEY interrupts.
     */
    *(KEY_ptr + 2) = 0;


    /*
     * Stop Timer0.
     */
    *(timer0_ptr + 1) = 0x8;


    /*
     * Clear pending KEY events.
     */
    *(KEY_ptr + 3) = 0xF;


    /*
     * Free Timer0 IRQ.
     */
    free_irq(
        TIMER0_IRQ,
        (void *) timer_irq_handler
    );


    /*
     * Free KEY IRQ.
     */
    free_irq(
        KEY_IRQ,
        (void *) key_irq_handler
    );


    /*
     * Release FPGA memory mapping.
     */
    iounmap(LW_virtual);
}


/* ============================================================
 * Register module
 * ============================================================
 */

module_init(initialize_stopwatch_handler);
module_exit(cleanup_stopwatch_handler);