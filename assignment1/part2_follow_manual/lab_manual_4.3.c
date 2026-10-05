#include <stdio.h>
#include <unistd.h> // for using close()
#include <fcntl.h>
#include <sys/mman.h>
#include "address_map_arm.h"

// prototypes for functions used to access physical memory addresses
int open_physical(int);                              // open /dev/mem device file
void close_physical(int);                            // close /dev/mem device file
void *map_physical(int, unsigned int, unsigned int); // calls the mmap kernel function to create a physical-to-virtual address mapping for I/O device
int unmap_physical(void *, unsigned int);            // close the mapping

// increments the contents of the red LED parallel port
int main(void)
{
    volatile int *LEDR_ptr; // virtual address pointer to red LEDs
    int fd = -1;            // used to open /dev/mem
    void *LW_virtual;       // physical addresses for light-weight bridge

    // create virtual memory access to the FPGA light-weight bridge
    if ((fd = open_physical(fd)) == -1) // open /dev/mem file
        return (-1);

    // LW_virtual variable will be set to an address that maps to the requested physical address space (LW_BRIDGE_BASE)
    // Now an access to LW_virtual + offset will access the physical address 0xFF200000 + offset
    if ((LW_virtual = map_physical(fd, LW_BRIDGE_BASE, LW_BRIDGE_SPAN)) == NULL)
        return (-1);

    LEDR_ptr = (int *)(LW_virtual + LEDR_BASE); // calculates the virtual address that maps to LED port (done by adding the address offset of the port LEDR_BASE to LW_virtual)
    *LEDR_ptr = *LEDR_ptr + 1;                  // reads data register, increment 1 every time the program execute and writes the incremented value back to data register

    // unmap and close the /dev/mem file
    unmap_physical(LW_virtual, LW_BRIDGE_SPAN);
    close_physical(fd);

    return 0;
}

// open /dev/mem to give access to physical addresses
int open_physical(int fd)
{
    if (fd == -1) // check if already open
    {
        if ((fd = open("/dev/mem", (O_RDWR | O_SYNC))) == -1)
        {
            printf("ERROR: could not open \"/dev/mem\"...\n");
            return (-1);
        }
    }
    return fd;
}

// close /dev/mem to give access to physical address
void close_physical(int fd)
{
    close(fd);
}

void *map_physical(int fd, unsigned int base, unsigned int span)
{
    void *virtual_base;
    // get a mapping from physical addresses to virtual addresses
    virtual_base = mmap(NULL, span, (PROT_READ | PROT_WRITE), MAP_SHARED, fd, base);
    if (virtual_base == MAP_FAILED)
    {
        printf("ERROR:mmap() failed... \n");
        close(fd);
        return (NULL);
    }
    return virtual_base;
}

// close the previously-opened virtual address mapping
int unmap_physical(void *virtual_base, unsigned int span)
{
    if (munmap(virtual_base, span) != 0)
    {
        printf("ERROR: munmap() failed...\n");
        return (-1);
    }
    return 0;
}