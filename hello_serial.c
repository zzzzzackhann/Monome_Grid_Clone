/* hello.c
 *
 * Sends "hello" over UART0 every 500 ms at 9600 baud, 8N1.
 * Purpose: confirm the USB-serial bridge works:
 *     328PB TX (PD1) -> 16U2 (running LUFA USBtoSerial) -> USB -> Mac
 *
 * On the Mac:
 *     ls /dev/tty.usbmodem*
 *     screen /dev/tty.usbmodemXXXX 9600      (quit: Ctrl-A, then K, then y)
 *
 * Build with -mmcu=atmega328p (the installed headers don't know the 328PB);
 * flash with avrdude -p m328pb.
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include <stdint.h>

#ifndef F_CPU
#define F_CPU 16000000UL            /* normally passed in by the Makefile */
#endif

#define BAUD 9600UL
/* UBRR = F_CPU / (16 * BAUD) - 1  ->  16 MHz / 153600 - 1 = 103 (0.2% error) */
#define UBRR_VALUE ((F_CPU / (16UL * BAUD)) - 1)

#define HELLO_INTERVAL_MS 500UL


/* ---------- millisecond clock: Timer0 in CTC mode, 1 kHz tick ---------- */

static volatile uint32_t ms_ticks = 0;

ISR(TIMER0_COMPA_vect) {
    ms_ticks++;                     /* interrupts are already off inside an ISR */
}

static void timer0_init(void) {
    TCCR0A = (1 << WGM01);                /* CTC mode */
    TCCR0B = (1 << CS01) | (1 << CS00);   /* clk/64 -> 250 kHz */
    OCR0A  = 249;                         /* 250 counts = 1 ms */
    TIMSK0 = (1 << OCIE0A);               /* interrupt on compare match A */
}

/* ms_ticks is 4 bytes and the AVR reads it one byte at a time. If the tick
 * ISR fired mid-read we'd get a torn value (half old, half new).
 * ATOMIC_BLOCK holds interrupts off for just the copy, then restores
 * whatever interrupt state was there before. */
static uint32_t millis(void) {
    uint32_t t;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        t = ms_ticks;
    }
    return t;
}


/* ---------- UART0: TX only, 8N1 ---------- */

static void uart_init(void) {
    UBRR0H = (uint8_t)(UBRR_VALUE >> 8);
    UBRR0L = (uint8_t)(UBRR_VALUE);
    UCSR0B = (1 << TXEN0);                    /* enable TX (takes over PD1) */
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);   /* 8 data bits, no parity, 1 stop */
}

static void uart_send_char(uint8_t c) {
    while (!(UCSR0A & (1 << UDRE0)));         /* wait for empty data register */
    UDR0 = c;
}

static void uart_send_string(const char *s) {
    while (*s) {                              /* stop at the '\0' terminator */
        uart_send_char((uint8_t)*s++);
    }
}


/* ---------- main ---------- */

int main(void) {
    timer0_init();
    uart_init();
    sei();

    uint32_t last = millis();

    while (1) {
        uint32_t now = millis();

        /* subtraction idiom: stays correct when ms_ticks rolls over */
        if ((now - last) >= HELLO_INTERVAL_MS) {
            last = now;
            uart_send_string("hello\r\n");
        }
    }
}
