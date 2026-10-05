#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/io.h>

#include "../address_map_arm.h"
#include "../interrupt_ID.h"

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
 * running = 0 -> paused & setting
 */
int running = 1;


/*
 *
 * 0 -> MM:SS:D_ 
 * 1 -> MM:SS:_D
 * 2 -> MM:S_:DD
 * 3 -> MM:_S:DD
 * 4 -> M_:SS:DD
 * 5 -> _M:SS:DD

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

    min = time / 6000;
    sec = (time % 6000) / 100;
    hundredths = time % 100;

    min_tens = min / 10;
    min_ones = min % 10;

    sec_tens = sec / 10;
    sec_ones = sec % 10;

    dd_tens = hundredths / 10;
    dd_ones = hundredths % 10;

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

    printk(KERN_ALERT
           "%s%d"
           "%s%d"
           VT100_RED ":"
           "%s%d"
           "%s%d"
           VT100_RED ":"
           "%s%d"
           "%s%d"
           VT100_RESET
           "\n",
           (set_digit == 5)
               ? VT100_YELLOW
               : VT100_RED,
           min_tens,
           (set_digit == 4)
               ? VT100_YELLOW
               : VT100_RED,
           min_ones,
           (set_digit == 3)
               ? VT100_YELLOW
               : VT100_RED,
           sec_tens,
           (set_digit == 2)
               ? VT100_YELLOW
               : VT100_RED,
           sec_ones,
           (set_digit == 1)
               ? VT100_YELLOW
               : VT100_RED,
           dd_tens,

           (set_digit == 0)
               ? VT100_YELLOW
               : VT100_RED,
           dd_ones);
}

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

    min = time / 6000;
    sec = (time % 6000) / 100;
    hundredths = time % 100;

    min_tens = min / 10;
    min_ones = min % 10;

    sec_tens = sec / 10;
    sec_ones = sec % 10;

    dd_tens = hundredths / 10;
    dd_ones = hundredths % 10;

    switch (position)
    {
        case 0:
            digit = switch_value;
            if (digit > 9)
                digit = 9;
            dd_ones = digit;
            break;
        case 1:
            digit = switch_value;
            if (digit > 9)
                digit = 9;
            dd_tens = digit;
            break;
        case 2:
            digit = switch_value;
            if (digit > 9)
                digit = 9;
            sec_ones = digit;
            break;
        case 3:
            digit = switch_value;
            if (digit > 5)
                digit = 5;
            sec_tens = digit;
            break;
        case 4:
            digit = switch_value;
            if (digit > 9)
                digit = 9;
            min_ones = digit;
            break;
        case 5:
            digit = switch_value;
            if (digit > 5)
                digit = 5;
            min_tens = digit;
            break;
    }

    min = min_tens * 10 + min_ones;

    sec = sec_tens * 10 + sec_ones;

    hundredths = dd_tens * 10 + dd_ones;

    time = min * 6000 + sec * 100 + hundredths;
}

irq_handler_t timer_irq_handler(int irq, void *dev_id, struct pt_regs *regs){
    *(timer0_ptr) = 0;
    if (running && time > 0){
        time--;
    }
    return (irq_handler_t) IRQ_HANDLED;
}

irq_handler_t key_irq_handler(int irq, void *dev_id, struct pt_regs *regs){
    int press;
    int switch_value;

    press = *(KEY_ptr + 3);

    if (press & 0x1){
        if (running){
            running = 0;
            set_digit = 0;
        }else{
            running = 1;
        }
    }

    if (press & 0x2)
    {
        if (running){
            print_time();
        }else{
            switch_value = *SW_ptr & 0x3FF;
            set_stopwatch_digit(set_digit,switch_value);
            print_time();
        }
    }

    if (press & 0x4){
        if (!running)
        {
            if (set_digit > 0)
            {
                set_digit--;
            }
            print_time();
        }
    }

    if (press & 0x8){
        if (!running){
            if (set_digit < 5)
            {
                set_digit++;
            }
            print_time();
        }
    }
    *(KEY_ptr + 3) = press;

    return (irq_handler_t) IRQ_HANDLED;
}

static int __init initialize_stopwatch_handler(void){
    int counter;
    int ret_val;
    LW_virtual =ioremap_nocache(LW_BRIDGE_BASE, LW_BRIDGE_SPAN);

    if (LW_virtual == NULL){
        printk(KERN_ERR "Could not map FPGA lightweight bridge\n");
        return -ENOMEM;
    }

    timer0_ptr = LW_virtual + TIMER0_BASE;


    KEY_ptr = LW_virtual + KEY_BASE;


    SW_ptr = LW_virtual + SW_BASE;

    *(KEY_ptr + 3) = 0xF;

    ret_val =request_irq(TIMER0_IRQ, (irq_handler_t) timer_irq_handler, IRQF_SHARED, "timer_irq_handler", (void *) timer_irq_handler);


    if (ret_val){
        printk(KERN_ERR "Could not register TIMER0 interrupt\n");
        iounmap(LW_virtual);
        return ret_val;
    }

    ret_val =request_irq(KEY_IRQ, (irq_handler_t) key_irq_handler, IRQF_SHARED, "key_irq_handler", (void *) key_irq_handler);

    if (ret_val){
        printk(KERN_ERR "Could not register KEY interrupt\n");
        free_irq(TIMER0_IRQ, (void *) timer_irq_handler);
        iounmap(LW_virtual);
        return ret_val;
    }

    *(KEY_ptr + 2) = 0xF;

    counter = 1000000;

    *(timer0_ptr + 2) = counter & 0xFFFF;

    *(timer0_ptr + 3) = (counter >> 16) & 0xFFFF;

    *(timer0_ptr + 1) = 0x7;

    time = 359999;
    running = 1;
    set_digit = 0;

    return 0;
}

static void __exit cleanup_stopwatch_handler(void){
    *(KEY_ptr + 2) = 0;

    *(timer0_ptr + 1) = 0x8;

    *(KEY_ptr + 3) = 0xF;

    free_irq(TIMER0_IRQ, (void *) timer_irq_handler);

    free_irq(KEY_IRQ, (void *) key_irq_handler);

    iounmap(LW_virtual);
}

module_init(initialize_stopwatch_handler);
module_exit(cleanup_stopwatch_handler);