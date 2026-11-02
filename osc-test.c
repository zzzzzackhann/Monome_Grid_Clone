#include <avr/io.h>
#include <avr/interrupt.h>
/* contains the macro ATOMIC_BLOCK, which wraps a block of code so that no
no interrupt can fire while that code executes */
#include <util/atomic.h> 
#include <stdint.h>

#ifndef F_CPU
#define F_CPU 16000000UL
#endif 

#define BAUD 9600UL
/* UBBR = F_CPU / (16 * BAUD) - 1 */
#define UBBR_VAL ((F_CPU / (16UL * BAUD)) - 1)

/* _____________________________________________________ */

/* the static on this isn't stricly necessary but is a good practice for saving memory. 
it means these functions are only callable from this module and can't be linked to other modules. (I think*/
static void uart_init(void) {
    UBRR0H = UBBR_VAL >> 8;
    UBRR0L = UBBR_VAL;
    UCSR0B = 1 << TXEN0; // enable TX (on PD1)
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00); // 8 data bits, no parity, 1 stop 
}

static void 
