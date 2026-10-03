#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <stdbool.h>

#include "physical.h"
#include "address_map_arm.h"

/*s
 * KEY register layout:
 *
 * KEY data register        : KEY_BASE + 0x00
 * Interrupt mask register  : KEY_BASE + 0x08
 * Edgecapture register     : KEY_BASE + 0x0C
 */
#define KEY_EDGE_OFFSET 0x0C

static uint8_t encode_char(char c)
{
    uint8_t pattern;

    switch (c) {
        case 'I':
            pattern = 0x04;
            break;

        case 'n':
            pattern = 0x54;
            break;

        case 't':
            pattern = 0x78;
            break;

        case 'E':
            pattern = 0x79;
            break;

        case 'L':
            pattern = 0x38;
            break;

        case 'S':
            pattern = 0x6D;
            break;

        case 'o':
            pattern = 0x5C;
            break;

        case 'C':
            pattern = 0x39;
            break;

        case 'F':
            pattern = 0x71;
            break;

        case 'P':
            pattern = 0x73;
            break;

        case 'G':
            pattern = 0x3D;
            break;

        case 'A':
            pattern = 0x77;
            break;

        case ' ':
            pattern = 0x00;
            break;

        default:
            pattern = 0x00;
            break;
    }

    /*
     * Convert active-high -> active-low.
     * Only lower 7 bits belong to the seven segments.
     */
    return (~pattern) & 0x7F;
}


/*
 * Display six characters.
 *
 * text[0] = leftmost character  -> HEX5
 * text[1]                       -> HEX4
 * text[2]                       -> HEX3
 * text[3]                       -> HEX2
 * text[4]                       -> HEX1
 * text[5] = rightmost character -> HEX0
 */
static void display_six(
    volatile uint32_t *hex3_hex0,
    volatile uint32_t *hex5_hex4,
    const char text[6])
{
    uint8_t h5 = encode_char(text[0]);
    uint8_t h4 = encode_char(text[1]);
    uint8_t h3 = encode_char(text[2]);
    uint8_t h2 = encode_char(text[3]);
    uint8_t h1 = encode_char(text[4]);
    uint8_t h0 = encode_char(text[5]);

    /*
     * Register at 0xFF200020:
     *
     * bits  6:0   -> HEX0
     * bits 14:8   -> HEX1
     * bits 22:16  -> HEX2
     * bits 30:24  -> HEX3
     */
    *hex3_hex0 =
          ((uint32_t)h0)
        | ((uint32_t)h1 << 8)
        | ((uint32_t)h2 << 16)
        | ((uint32_t)h3 << 24);

    /*
     * Register at 0xFF200030:
     *
     * bits  6:0   -> HEX4
     * bits 14:8   -> HEX5
     */
    *hex5_hex4 =
          ((uint32_t)h4)
        | ((uint32_t)h5 << 8);
}


int main(void)
{
    int fd = -1;
    bool stop = false;
    void *virtual_base;


    volatile uint32_t *hex3_hex0_ptr;
    volatile uint32_t *hex5_hex4_ptr;
    volatile uint32_t *key_edge_ptr;

    /*
     * Six spaces before and after the message.
     *
     * This makes the message enter from the right side,
     * travel across all six displays, and disappear on
     * the left side.
     */
    const char message[] = "      Intel SoC FPGA      ";

    const int message_length = sizeof(message) - 1;

    int running = 1;
    struct timespec delay;
    delay.tv_sec = 0;
    delay.tv_nsec = 300000000;   // 300 ms


    /* ------------------------------------
     * 1. Open physical memory
     * ------------------------------------ */

    fd = open_physical(fd);

    if (fd == -1) {
        return 1;
    }


    /* ------------------------------------
     * 2. Map lightweight FPGA bridge
     * ------------------------------------ */

    virtual_base =
        map_physical(
            fd,
            LW_BRIDGE_BASE,
            LW_BRIDGE_SPAN
        );

    if (virtual_base == NULL) {
        close_physical(fd);
        return 1;
    }


    /* ------------------------------------
     * 3. Obtain pointers to MMIO registers
     * ------------------------------------ */

    hex3_hex0_ptr =
        (volatile uint32_t *)
        ((char *)virtual_base + HEX3_HEX0_BASE);

    hex5_hex4_ptr =
        (volatile uint32_t *)
        ((char *)virtual_base + HEX5_HEX4_BASE);

    key_edge_ptr =
        (volatile uint32_t *)
        ((char *)virtual_base
         + KEY_BASE
         + KEY_EDGE_OFFSET);


    /*
     * Clear any old KEY edge event.
     */
    *key_edge_ptr = 0xF;


    /* ------------------------------------
     * 4. Scrolling loop
     * ------------------------------------ */

    while (!stop) {

        for (int i = 0;
             i <= message_length - 6;
             i++) {

            /*
             * Keep waiting here if scrolling
             * has been paused.
             */
            while (!running) {

                uint32_t edge =
                    *key_edge_ptr;

                if (edge & 0x1) {

                    /*
                     * Clear KEY0 edgecapture bit
                     * by writing 1 back to it.
                     */
                    *key_edge_ptr = 0x1;

                    running = 1;
                }

                nanosleep(&delay, NULL);
            }


            /*
             * Check whether KEY0 was pressed.
             */
            uint32_t edge =
                *key_edge_ptr;

            if (edge & 0x1) {

                /*
                 * Clear KEY0 edge event.
                 */
                *key_edge_ptr = 0x1;

                running = 0;
                continue;
            }


            /*
             * Current six-character window.
             */
            char window[6];

            window[0] = message[i];
            window[1] = message[i + 1];
            window[2] = message[i + 2];
            window[3] = message[i + 3];
            window[4] = message[i + 4];
            window[5] = message[i + 5];


            /*
             * Send these six characters
             * to the FPGA HEX registers.
             */
            display_six(
                hex3_hex0_ptr,
                hex5_hex4_ptr,
                window
            );


            /*
             * Slow the scrolling speed.
             */
            nanosleep(&delay, NULL);
        }
    }


    /*
     * Normally unreachable because the program
     * runs forever, but included for completeness.
     */
    unmap_physical(
        virtual_base,
        LW_BRIDGE_SPAN
    );

    close_physical(fd);

    return 0;
}