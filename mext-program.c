/* On 328PB, RX is PD0 and TX and PD1 */
/* This is a test that stores data (chars) in a buffer of limited size (a ring buffer),
reads each byte and sends it back to the computer over uart. 
them one by one*/

#include <avr/io.h> // for registers
#include <avr/interrupt.h>
/* contains the macro ATOMIC_BLOCK, which wraps a block of code so that no
no interrupt can fire while that code executes */
#include <util/atomic.h> 
#include <stdint.h>


#ifndef F_CPU
#define F_CPU 16000000UL
#endif 

#define DEBUG 0
#define RX_BUFFER_SIZE 8

#define BAUD 115200UL
/* UBBR = F_CPU / (8 * BAUD) - 1 */
#define UBBR_VAL ((F_CPU / (8UL * BAUD)) - 1)

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

static void uart_send_string(uint8_t* string) {
    while(*string) {
        uart_send(*string);
        string++;
    }
}

static uint8_t usart_receive(void) {
    /* Wait for data to be received */
    while ( !(UCSR0A & (1 << RXC0)) );

    /* Get and return received data from buffer */
    return UDR0;
}

/* Change the data type to whatever you need, both here and in the read/write functions*/

typedef struct {
    uint8_t data[RX_BUFFER_SIZE];
    uint8_t read; 
    uint8_t write; 
    uint8_t count;
} ring_buffer;

/* This is being read by the ISR, meanning it can change behind the compiler's back. 
 Volatile says to reread it everything time. */
static volatile ring_buffer rx_buff;

static void write_to_ring_buffer(volatile ring_buffer* buff, uint8_t value) {
    if(buff->count < RX_BUFFER_SIZE) { // if the buffer isn't full (all 8 slots taken) (it needs to be 1-index because 0 data means the count is actually 0)
        buff->data[buff->write] = value;
        buff->write = (buff->write + 1) % RX_BUFFER_SIZE;
        buff->count++;
    } else {
        #if DEBUG

        #endif
    }
}

static uint8_t read_from__ring_buffer(volatile ring_buffer* buff) {
    uint8_t value;

    value = buff->data[buff->read];
    buff->read = (buff->read + 1) % RX_BUFFER_SIZE;
    /* count is also changed by the RX ISR; keep its read-modify-write from being split */
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        buff->count--;
    }
    return value;
}

/* UART RX interrupt service routine */
ISR(USART_RX_vect) {
    write_to_ring_buffer(&rx_buff, UDR0);
}



int main(void) {
    uart_init(); // enable uart
    sei(); // enable global interrupts. cli() disables them
    
    while(1) {
        if(rx_buff.count > 0) { // only loop through the following things if there's anything in the rx_buff
            uint8_t serialosc_query = read_from_ring_buffer(&rx_buff); //read once from the ring buffer

            switch(serialosc_query) {
            case 0x00: //serialosc asking "what do you have?"
                /* answer */
                uart_send(0x00); // op code to answer this query
                uart_send(0x01); // index 1: "led-grid"
                uart_send(0x01); // 1 of them
                break;
            case 0x01: //serial osc asking "what's your name?"
                const char *name = "zacks monome grid";

                uart_send(0x01); // opcode to respond to this query
                for (uint8_t i = 0; i < 32; i++) {
                    if(*name) {
                        uart_send(*name);
                        name++;
                    } else {
                        uart_send('0'); //pad the name with zeros as it must be 32 bytes
                    }
                }
            case 0x05: // how big is your grid? 
                uart_send(0x03); // opcode to reply
                uart_send(0x10); // num of columns (16 in hex)
                uart_send(0x08); // num of rows (8)
            }
        }
    }
}
