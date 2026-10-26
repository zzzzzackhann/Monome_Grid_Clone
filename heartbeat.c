/*
 * heartbeat.c - V2 MIDI controller bring-up test (ATmega328PB, 16 MHz crystal)
 *
 * Sends middle C note-on, then note-off 500 ms later, forever.
 * Proves the whole chain:
 *   328PB PD1 (TXD0) -> 16U2 PD2 (RXD1) -> USB -> Mac
 *
 * Only PD1 (via the UART) and Timer0 are touched. Every other pin stays an
 * input (reset default), so nothing here can fight the decoder, the LED
 * driver, or the matrix rows.
 *
 * Build:
 *   avr-gcc -mmcu=atmega328p -Os -DF_CPU=16000000UL -o heartbeat.elf heartbeat.c
 *   avr-objcopy -O ihex -R .eeprom heartbeat.elf heartbeat.hex
 *
 * Flash (USBasp on J2, USB-C unplugged):
 *   avrdude -p m328pb -c usbasp -U flash:w:heartbeat.hex:i
 *
 * Then unplug the USBasp, plug in USB-C, and watch "arduino_midi" in Max
 * (midiin) or any MIDI monitor.
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include <stdint.h>

#ifndef F_CPU
#define F_CPU 16000000UL
#endif

/* ---- MIDI / timing constants ------------------------------------------ */
#define MIDI_BAUD      31250UL
/* UBRR = F_CPU / (16 * baud) - 1  ->  16 MHz / 500000 - 1 = 31 (0% error) */
#define UBRR_VALUE     ((F_CPU / (16UL * MIDI_BAUD)) - 1)

#define MIDI_NOTE_ON   0x90   /* status byte, channel 1 */
#define MIDI_NOTE_OFF  0x80   /* status byte, channel 1 */
#define NOTE_MIDDLE_C  60
#define VELOCITY_MAX   127

#define HEARTBEAT_MS   500

/* ---- millis(): Timer0 in CTC mode, 1 kHz tick ------------------------- */
static volatile uint32_t ms_ticks = 0;

ISR(TIMER0_COMPA_vect) {
    ms_ticks++;
}

static void timer0_init(void) {
    TCCR0A = (1 << WGM01);               /* CTC mode */
    TCCR0B = (1 << CS01) | (1 << CS00);  /* clk/64 -> 250 kHz */
    OCR0A  = 249;                        /* 250 counts = 1 ms */
    TIMSK0 = (1 << OCIE0A);              /* interrupt on compare match A */
}

/*
 * A uint32_t is 4 bytes, and the AVR reads it one byte at a time. If the
 * tick ISR fires mid-read, you get a torn value (half old, half new).
 * ATOMIC_BLOCK holds interrupts off just for the copy.
 */
static uint32_t millis(void) {
    uint32_t t;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        t = ms_ticks;
    }
    return t;
}

/* ---- UART0: TX only, 31250 baud, 8N1 ---------------------------------- */
static void uart_init(void) {
    UBRR0H = (uint8_t)(UBRR_VALUE >> 8);
    UBRR0L = (uint8_t)(UBRR_VALUE);
    UCSR0B = (1 << TXEN0);                        /* enable TX; this also takes over PD1 */
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);       /* 8 data bits, no parity, 1 stop bit */
}

static void uart_send(uint8_t b) {
    while (!(UCSR0A & (1 << UDRE0)));             /* wait until the data register is empty */
    UDR0 = b;
}

static void midi_send(uint8_t status, uint8_t data1, uint8_t data2) {
    uart_send(status);
    uart_send(data1);
    uart_send(data2);
}

/* ---- main ------------------------------------------------------------- */
int main(void) {
    uart_init();
    timer0_init();
    sei();

    uint32_t last = 0;
    uint8_t  note_is_on = 0;

    while (1) {
        /* Subtraction form survives the 32-bit rollover. */
        if ((millis() - last) >= HEARTBEAT_MS) {
            last += HEARTBEAT_MS;                 /* += keeps the beat from drifting */

            if (note_is_on) {
                midi_send(MIDI_NOTE_OFF, NOTE_MIDDLE_C, 0);
            } else {
                midi_send(MIDI_NOTE_ON, NOTE_MIDDLE_C, VELOCITY_MAX);
            }
            note_is_on = !note_is_on;
        }
    }
}
