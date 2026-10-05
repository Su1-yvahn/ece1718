#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <sys/mman.h>
#include "physical.h"
#include "../address_map_arm.h"

char message[] = "intEL SoC FPGA     intEL"; 
// used to exit the program cleanly
volatile sig_atomic_t stop;
void catchSIGINT(int);
char display_char(char);

/* Program that scrolls the message intEL SoC FPGA across the 7-segment displays */
int main(void)
{
	char *pmessage;
	volatile unsigned int * HEX3_HEX0_ptr;
	volatile unsigned int * HEX5_HEX4_ptr;
	volatile unsigned int * KEY_ptr;
	int fd = -1;				// used to open /dev/mem for access to physical addresses
	void *LW_virtual;			// used to map physical addresses for the light-weight bridge
	struct timespec ts;
	time_t start_time;
	int scroll;					// used to pause/run scrolling

	// catch SIGINT from ^C, instead of having it abruptly close this program
   signal(SIGINT, catchSIGINT);
	start_time = time (NULL);

	// Create access to the FPGA light-weight bridge
	if ((fd = open_physical (fd)) == -1)
		return (-1);
	else if ((LW_virtual = map_physical (fd, LW_BRIDGE_BASE, LW_BRIDGE_SPAN)) == NULL)
		return (-1);

	// Set virtual address pointers to the I/O ports
	HEX3_HEX0_ptr = (unsigned int *) (LW_virtual + HEX3_HEX0_BASE);
	HEX5_HEX4_ptr = (unsigned int *) (LW_virtual + HEX5_HEX4_BASE);
	KEY_ptr = (unsigned int *) (LW_virtual + KEY_BASE);

	*HEX3_HEX0_ptr = 0;								// clear the display
	*HEX5_HEX4_ptr = 0;								// clear the display

	pmessage = message;								// point to start of message
	ts.tv_sec = 0;										// used to delay
	ts.tv_nsec = 300000000;							// 3 x 10^8 ns = 0.3 sec
	scroll = 1;
	while (!stop)						
	{
		/* display scrolling message */
		*HEX5_HEX4_ptr =  display_char(*pmessage) << 8;
		*HEX5_HEX4_ptr |= display_char(*(pmessage+1));
		*HEX3_HEX0_ptr =  display_char(*(pmessage+2)) << 24;
		*HEX3_HEX0_ptr |= display_char(*(pmessage+3)) << 16;
		*HEX3_HEX0_ptr |= display_char(*(pmessage+4)) << 8;
		*HEX3_HEX0_ptr |= display_char(*(pmessage+5));

		if (pmessage == message + 18)			// check when message has "wrapped around"
			pmessage = message;
		else
			if (scroll) ++pmessage;

		if (*(KEY_ptr + 3))						// check for KEY press
		{
			scroll ^= 1;
  			*(KEY_ptr + 3) = 0xF; 				// clear KEY
		}

		/* wait for timer */
		nanosleep (&ts, NULL);
		if (time (NULL) - start_time > 12) stop = 1;
	}
	*HEX3_HEX0_ptr = 0;								// clear the display
	*HEX5_HEX4_ptr = 0;								// clear the display
	unmap_physical (LW_virtual, LW_BRIDGE_SPAN);	// release the physical-memory mapping
	close_physical (fd);	// close /dev/mem
	printf ("\nExiting sample solution program\n");
	return 0;
}

/* Function to allow clean exit of the program */
void catchSIGINT(int signum)
{
	stop = 1;
}

char display_char(char c)
{
	char seg7_code;
	switch (c)
	{
		case 'i': seg7_code = 0b00000100; break;
		case 'n': seg7_code = 0b01010100; break;
		case 't': seg7_code = 0b01111000; break;
		case 'E': seg7_code = 0b01111001; break;
		case 'L': seg7_code = 0b00111000; break;
		case 'S': seg7_code = 0b01101101; break;
		case 'o': seg7_code = 0b01011100; break;
		case 'C': seg7_code = 0b00111001; break;
		case 'F': seg7_code = 0b01110001; break;
		case 'P': seg7_code = 0b01110011; break;
		case 'G': seg7_code = 0b01111101; break;
		case 'A': seg7_code = 0b01110111; break;
		case ' ': seg7_code = 0b00000000; break;
	}
	return seg7_code;
}
