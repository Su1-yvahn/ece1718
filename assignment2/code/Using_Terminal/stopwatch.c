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
 * Time is stored in hundredths of a second.
 *
 * 59:59:99
 *
 * = 59 * 60 * 100
 * + 59 * 100
 * + 99
 *
 * = 359999
 */
int time = 359999;


/*
 * running = 1 : stopwatch is running
 * running = 0 : stopwatch is paused / setting
 */
int running = 1;


/*
 * Which digit will be changed next.
 *
 * 0 -> DD ones
 * 1 -> DD tens
 * 2 -> SS ones
 * 3 -> SS tens
 * 4 -> MM ones
 * 5 -> MM tens
 */
int set_digit = 0;


/* ============================================================
 * VT100 color codes
 * ============================================================
 */

#define VT100_RED       "\033[31m"
#define VT100_GREEN     "\033[32m"
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

    min = time / 6000;

    sec = (time % 6000) / 100;

    hundredths = time % 100;


    /*
     * Running -> GREEN
     *
     * Paused  -> RED
     */
    if (running)
    {
        printk(KERN_INFO
               VT100_GREEN
               "%02d:%02d:%02d"
               VT100_RESET
               "\n",
               min,
               sec,
               hundredths);
    }
    else
    {
        printk(KERN_INFO
               VT100_RED
               "%02d:%02d:%02d"
               VT100_RESET
               "\n",
               min,
               sec,
               hundredths);
    }
}


/* ============================================================
 * Set one stopwatch digit
 * ============================================================
 */

void set_stopwatch_digit(int position, int value)
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
     * Extract current MM:SS:DD
     * --------------------------------------------------------
     */

    min = time / 6000;

    sec = (time % 6000) / 100;

    hundredths = time % 100;


    /* --------------------------------------------------------
     * Split into six individual decimal digits
     * --------------------------------------------------------
     */

    min_tens = min / 10;
    min_ones = min % 10;

    sec_tens = sec / 10;
    sec_ones = sec % 10;

    dd_tens = hundredths / 10;
    dd_ones = hundredths % 10;


    /* --------------------------------------------------------
     * Modify exactly ONE digit
     * --------------------------------------------------------
     */

    switch (position)
    {
        /*
         * MM:SS:D[D]
         */
        case 0:

            dd_ones = value;

            break;


        /*
         * MM:SS:[D]D
         */
        case 1:

            dd_tens = value;

            break;


        /*
         * MM:S[S]:DD
         */
        case 2:

            sec_ones = value;

            break;


        /*
         * MM:[S]S:DD
         */
        case 3:

            sec_tens = value;

            break;


        /*
         * M[M]:SS:DD
         */
        case 4:

            min_ones = value;

            break;


        /*
         * [M]M:SS:DD
         */
        case 5:

            min_tens = value;

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
 *
 * Timer0 generates an interrupt every 0.01 second.
 * ============================================================
 */

irq_handler_t timer_irq_handler(
    int irq,
    void *dev_id,
    struct pt_regs *regs)
{
    /*
     * Clear current Timer0 interrupt.
     */
    *(timer0_ptr) = 0;


    /*
     * Count down only when running.
     *
     * Do not go below 00:00:00.
     */
    if (running && time > 0)
    {
        time--;
    }


    /*
     * Do NOT print here.
     *
     * Timer interrupt happens 100 times/sec.
     *
     * Time is printed only when the user
     * presses KEY0 or KEY1.
     */


    return (irq_handler_t) IRQ_HANDLED;
}


/* ============================================================
 * KEY interrupt handler
 *
 * KEY0:
 *      running -> pause
 *      paused  -> run
 *
 * KEY1:
 *      running -> print current time
 *      paused  -> set one digit using switches
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
     *
     * KEY_ptr + 3
     *
     * because:
     *
     * KEY base + 0x0C
     */
    press = *(KEY_ptr + 3);


    /* ========================================================
     * KEY0
     * ========================================================
     */

    if (press & 0x1)
    {
        /*
         * If running:
         *
         * pause stopwatch and begin setting
         * from the rightmost digit.
         */
        if (running)
        {
            running = 0;

            /*
             * Start setting sequence again
             * from DD ones.
             */
            set_digit = 0;


            /*
             * Since running == 0,
             * print_time() displays RED.
             */
            print_time();
        }


        /*
         * If paused:
         *
         * KEY0 concludes setting procedure
         * and resumes stopwatch.
         */
        else
        {
            running = 1;


            /*
             * Since running == 1,
             * print_time() displays GREEN.
             */
            print_time();
        }
    }


    /* ========================================================
     * KEY1
     * ========================================================
     */

    if (press & 0x2)
    {
        /*
         * ----------------------------------------------------
         * Stopwatch is RUNNING
         *
         * KEY1 only prints the current time.
         * ----------------------------------------------------
         */
        if (running)
        {
            print_time();
        }


        /*
         * ----------------------------------------------------
         * Stopwatch is PAUSED
         *
         * KEY1 sets one digit.
         * ----------------------------------------------------
         */
        else
        {
            /*
             * Read SW switches as a binary value.
             *
             * 0x3FF =
             *
             * 0b1111111111
             *
             * so only SW9-SW0 are kept.
             */
            switch_value =
                *SW_ptr & 0x3FF;


            /*
             * From your test of the professor's
             * sample program:
             *
             * one digit can only be 0-9.
             */
            if (switch_value <= 9)
            {
                /*
                 * Modify exactly one digit.
                 */
                set_stopwatch_digit(
                    set_digit,
                    switch_value
                );


                /*
                 * Print updated time.
                 *
                 * Since stopwatch is paused,
                 * this appears RED.
                 */
                print_time();


                /*
                 * Move to next digit:
                 *
                 * 0 -> DD ones
                 * 1 -> DD tens
                 * 2 -> SS ones
                 * 3 -> SS tens
                 * 4 -> MM ones
                 * 5 -> MM tens
                 */
                set_digit++;


                /*
                 * After sixth digit,
                 * return to rightmost digit.
                 */
                if (set_digit == 6)
                {
                    set_digit = 0;
                }
            }


            /*
             * Invalid value:
             *
             * Do not modify the stopwatch.
             * Do not advance to the next digit.
             */
            else
            {
                printk(KERN_INFO
                       VT100_RED
                       "Invalid SW value: %d (use 0-9)"
                       VT100_RESET
                       "\n",
                       switch_value);
            }
        }
    }


    /*
     * Clear captured KEY interrupt bits.
     *
     * Writing a 1 clears the corresponding
     * Edgecapture bit.
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
     * Set virtual pointers to FPGA devices
     * --------------------------------------------------------
     */

    timer0_ptr =
        LW_virtual + TIMER0_BASE;


    KEY_ptr =
        LW_virtual + KEY_BASE;


    SW_ptr =
        LW_virtual + SW_BASE;


    /* --------------------------------------------------------
     * Clear old KEY Edgecapture values
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
     * Enable ONLY KEY0 and KEY1 interrupts
     * --------------------------------------------------------
     *
     * KEY1 KEY0
     *   1    1
     *
     * binary:
     *
     * 0011
     *
     * = 0x3
     */

    *(KEY_ptr + 2) = 0x3;


    /* --------------------------------------------------------
     * Configure FPGA Timer0
     * --------------------------------------------------------
     *
     * Timer frequency:
     *
     * 100 MHz
     *
     * Desired period:
     *
     * 0.01 sec
     *
     * Therefore:
     *
     * 100,000,000 * 0.01
     *
     * = 1,000,000
     */

    counter = 1000000;


    /*
     * Counter start value:
     *
     * low 16 bits
     */
    *(timer0_ptr + 2) =
        counter & 0xFFFF;


    /*
     * Counter start value:
     *
     * high 16 bits
     */
    *(timer0_ptr + 3) =
        (counter >> 16) & 0xFFFF;


    /* --------------------------------------------------------
     * Start timer
     * --------------------------------------------------------
     *
     * Control register:
     *
     * STOP  = 0
     * START = 1
     * CONT  = 1
     * ITO   = 1
     *
     * = 0x7
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
     * Initial value appears GREEN.
     */
    print_time();


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
     * Stop FPGA Timer0.
     *
     * STOP = 1
     */
    *(timer0_ptr + 1) = 0x8;


    /*
     * Clear remaining KEY edge events.
     */
    *(KEY_ptr + 3) = 0xF;


    /*
     * Unregister Timer0 interrupt.
     */
    free_irq(
        TIMER0_IRQ,
        (void *) timer_irq_handler
    );


    /*
     * Unregister KEY interrupt.
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
 * Register kernel module
 * ============================================================
 */

module_init(initialize_stopwatch_handler);
module_exit(cleanup_stopwatch_handler);