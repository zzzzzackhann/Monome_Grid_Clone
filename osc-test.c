/* On 328PB, RX is PD0 and TX and PD1 */


#include <avr/io.h> // for registers
#include <avr/interrupt.h>
/* contains the macro ATOMIC_BLOCK, which wraps a block of code so that no
no interrupt can fire while that code executes */
#include <util/atomic.h> 
#include <stdint.h>

#ifndef F_CPU
#define F_CPU 16000000UL
#endif 

#define BAUD 115200UL
/* UBBR = F_CPU / (16 * BAUD) - 1 */
#define UBBR_VAL ((F_CPU / (16UL * BAUD)) - 1)

/* _____________________________________________________ */

/* the static on this isn't stricly necessary but is a good practice for saving memory. 
it means these functions are only callable from this module and can't be linked to other modules. */
static void uart_init(void) {
    UBRR0H = UBBR_VAL >> 8;
    UBRR0L = UBBR_VAL;
    UCSR0A = (1 << U2X0); // enable double speed
    UCSR0B = (1 << RXEN0)| (1 << TXEN0) | (1 << RXCIE0); // enable TX, RX, and interrupt on RX    
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00); // frame configuration: 8 data bits, no parity, 1 stop bit
}

static void uart_send (uint8_t c) {
    while(!(UCSR0A & (1 << UDRE0)));
    UDR0 = c;
}

static void usart_receive(void) {
    /* Wait for data to be received */
    while ( !(UCSR0A & (1 << RXC0)) );

    /* Get and return received data from buffer */
    return UDR0;
}

/* Change the data type to whatever you need, both here and in the read/write functions*/
typedef struct {
    int data[8];
    int read;
    int write;
    int count;
} ring_buffer;

static void write_to_ring_buffer(ring_buffer* buff, float value) {
    buff->data[buff->write] = value;
    buff->write = (buff->write + 1) % 8;
    buff->count++;
}

static int read_from__ring_buffer(ring_buffer* buff) {
    int value;
    value = buff->data[buff->read];
    buff->read = (buff->read + 1) % 8;
    buff->count--;

    return value;
}

/* UART RX interrupt service routine */
ISR(USART0_RX) {
    write_to_ring_buffer(&buff, (char) value);
}




int main(void) {
    sei(); // enable global interrupts. cli() disables them
    uart_init();



}
