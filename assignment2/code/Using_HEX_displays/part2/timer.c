#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include "../address_map_arm.h"
#include "../interrupt_ID.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Intel FPGA University Program");
MODULE_DESCRIPTION("Embedded Linux Exercise");

/* Kernel module that displays a real-time timer on the HEX displays. The time is
 * incremented by an interrupt service routine for a timer that expires every 1/100 second */

void *LW_virtual;             // used to map physical addresses for the light-weight bridge
volatile int * timer0_ptr;		// virtual pointer to FPGA timer
volatile int * HEX3_HEX0_ptr; // virtual pointer to HEX displays
volatile int * HEX5_HEX4_ptr; // virtual pointer to HEX displays
int time = 0;						// stop watch time
/* seven segment display codes for decimal digits */
char seg7[10] =	{0b00111111, 0b00000110, 0b01011011, 0b01001111, 0b01100110, 
						 0b01101101, 0b01111101, 0b00000111, 0b01111111, 0b01100111};

/* The FPGA timer interrupt handler. It increments the time and then displays it */
irq_handler_t irq_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	int hundredths, min, sec;
	// Clear the timer control register (clears current interrupt)
	*(timer0_ptr) = 0; 										// clear the interrupt
   
	time += 1;
	if (time == 360000) time = 0; 						// 59:59:99 rolls back to 0

	/* find the 7-segment display pattern */
	min = time / 6000;										// time is in hundredths of a second
	*HEX5_HEX4_ptr = seg7[min / 10] << 8;				// most significant minute digit
	*HEX5_HEX4_ptr |= seg7[min % 10];					// least significant minute digit

	hundredths = time % 6000;								// hundredths = seconds x 100
	sec = hundredths / 100;
	*HEX3_HEX0_ptr = seg7[sec / 10] << 24;				// most sig seconds digit
	*HEX3_HEX0_ptr |= seg7[sec % 10] << 16;			// least sig seconds digit

	hundredths = hundredths % 100;
	*HEX3_HEX0_ptr |= seg7[hundredths / 10] << 8;	// most sig hundredths digit
	*HEX3_HEX0_ptr |= seg7[hundredths % 10];			// least sig hundredths digit

	return (irq_handler_t) IRQ_HANDLED;
}

/* Device driver initialization function */
static int __init intitialize_timer_handler(void)
{
	int counter, ret_val;
	
	// register the interrupt handler
	ret_val = (int) request_irq (TIMER0_IRQ, (irq_handler_t) irq_handler, IRQF_SHARED,
		"irq_handler", (void *) (irq_handler));

	// generate a virtual address for the FPGA lightweight bridge
	LW_virtual = ioremap_nocache (LW_BRIDGE_BASE, LW_BRIDGE_SPAN);

	/* Set up the FPGA timer */
	timer0_ptr = LW_virtual + TIMER0_BASE;				// FPGA timer base address

	/* Set the timer period */
	counter = 1000000;				// period = 1/(100 MHz) x (1 x 10^6) = 0.01 sec
	*(timer0_ptr + 0x2) = (counter & 0xFFFF);
	*(timer0_ptr + 0x3) = (counter >> 16) & 0xFFFF;

	/* Start the timer, enable its interrupts */
	*(timer0_ptr + 1) = 0x7;	// STOP = 0, START = 1, CONT = 1, ITO = 1 

	HEX3_HEX0_ptr = LW_virtual + HEX3_HEX0_BASE;   // virtual address for HEX port
	HEX5_HEX4_ptr = LW_virtual + HEX5_HEX4_BASE;   // virtual address for HEX port
	*HEX3_HEX0_ptr = 0; 	// clear the display
	*HEX5_HEX4_ptr = 0; 	// clear the display
	
	return ret_val;
}

/* Device driver exit function */
static void __exit cleanup_timer_handler(void)
{
	/* Stop the timer */
	*(timer0_ptr + 1) = 0x8;		// STOP = 1, START = 0, CONT = 0, ITO = 0 
	*HEX3_HEX0_ptr = 0; 				// clear the display
	*HEX5_HEX4_ptr = 0; 				// clear the display
   iounmap (LW_virtual);
	free_irq (TIMER0_IRQ, (void*) irq_handler);
}

module_init (intitialize_timer_handler);
module_exit (cleanup_timer_handler);
