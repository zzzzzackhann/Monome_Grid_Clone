/* On 328PB, RX is PD0 and TX and PD1 */
/* This is a test that stores data (chars) in a buffer of limited size (a ring buffer),
reads each byte and sends it back to the computer over uart. 
them one by one*/

/* I2C SDA is on PB0 and SCL is on PB1 */

#ifndef F_CPU
#define F_CPU 16000000UL
#endif 

#define DEBUG 0
#define RX_BUFFER_SIZE 8

#define BAUD 115200UL
/* UBBR = F_CPU / (8 * BAUD) - 1 */
#define UBBR_VAL ((F_CPU / (8UL * BAUD)) - 1)

/* Bit-banging i2c. (Question to answer later: what's the difference between bit banging
and hardware i2c functionailty? 

-from the datasheet p. 85: "If DDxn is written to '1', Pxn is configured as an output pin. 
If DDxn is written to '0', Pxn is configured as an input pin."

If PORTxn is written to '1' when the pin is configured as an output pin, the port pin is driven high. If
PORTxn is written logic zero when the pin is configured as an output pin, the port pin is driven low.
    - an external pullup is pulling the pin high (open drain) even if the PORTxn bit is left floating

PINxn is for reading the value of the output, which can be changed (almost like treating it like an input) by being grounded by external components (in this case switches)

Keep in mind that an output pin is always driven one way or another, never floating. You release it (to make it floating) but turning it into an input. 
So keep PORTBn at 0, so that whenever the DDRB is set to output it drives low, but then get's released when set to input. 
*/

#define I2C_SDA PB0
#define I2C_SCL PB1
#define I2C_DELAY_US 5

/* For LED driver */
#define IS31_ADDR              0x74
#define IS31_COMMAND           0xFD
#define IS31_PAGE_FRAME1       0x00
#define IS31_PAGE_FUNCTION     0x0B

#define IS31_REG_CONFIG        0x00
#define IS31_REG_PICTURE_FRAME 0x01
#define IS31_REG_AUDIO_SYNC    0x06
#define IS31_REG_SHUTDOWN      0x0A

#define IS31_LED_CONTROL       0x00
#define IS31_LED_CONTROL_LEN   18
#define IS31_PWM               0x24
#define IS31_LED_COUNT         144

/* _____________________________________________________ */


#include <avr/io.h> // for registers
#include <avr/interrupt.h>
/* contains the macro ATOMIC_BLOCK, which wraps a block of code so that no
no interrupt can fire while that code executes */
#include <util/atomic.h> 
#include <stdint.h>
#include <util/delay.h>


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

static uint8_t read_from_ring_buffer(volatile ring_buffer* buff) {
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

static void sda_low(void)     { DDRB |=  (1 << I2C_SDA); }
static void sda_release(void) { DDRB &= ~(1 << I2C_SDA); }
static void scl_low(void)     { DDRB |=  (1 << I2C_SCL); }
static void scl_release(void) { DDRB &= ~(1 << I2C_SCL); }
static uint8_t sda_read(void) { return PINB & (1 << I2C_SDA); }

static void i2c_init(void) {
    PORTB &= ~((1 << I2C_SDA) | (1 << I2C_SCL)); // clear PORTBx bits so floating when input (released line)
    sda_release(); // ensure lines are released
    scl_release(); 
}

static void i2c_start(void) {
    sda_release();
    scl_release();
    _delay_us(I2C_DELAY_US);
    sda_low();
    _delay_us(I2C_DELAY_US);
    scl_low();
}

static void i2c_stop(void) {
    sda_low();
    _delay_us(I2C_DELAY_US);
    scl_release();
    _delay_us(I2C_DELAY_US);
    sda_release();
    _delay_us(I2C_DELAY_US);
}

static uint8_t i2c_write_byte(uint8_t byte) {
    uint8_t ack;

    for (uint8_t i = 0; i < 8; i++) {
        if (byte & 0x80) {
            sda_release();
        } else {
            sda_low();
        }
        byte <<= 1;
        _delay_us(I2C_DELAY_US);
        scl_release();
        _delay_us(I2C_DELAY_US);
        scl_low();
    }

    sda_release();
    _delay_us(I2C_DELAY_US);
    scl_release();
    _delay_us(I2C_DELAY_US);
    ack = (sda_read() == 0);
    scl_low();

    return ack;
}

static uint8_t i2c_write_regs(uint8_t addr, uint8_t reg, const uint8_t *data, uint8_t len) {
    uint8_t ok;

    i2c_start();
    ok = i2c_write_byte(addr << 1);
    if (ok) ok = i2c_write_byte(reg);
    for (uint8_t i = 0; ok && i < len; i++) {
        ok = i2c_write_byte(data[i]);
    }
    i2c_stop();

    return ok;
}

static uint8_t i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t value) {
    return i2c_write_regs(addr, reg, &value, 1);
}

static uint8_t led_pwm[IS31_LED_COUNT];

static uint8_t is31_select_page(uint8_t page) {
    return i2c_write_reg(IS31_ADDR, IS31_COMMAND, page);
}

static uint8_t is31_write(uint8_t page, uint8_t reg, uint8_t value) {
    if (!is31_select_page(page)) return 0;
    return i2c_write_reg(IS31_ADDR, reg, value);
}

static void is31_set_all(uint8_t brightness) {
    for (uint8_t i = 0; i < IS31_LED_COUNT; i++) {
        led_pwm[i] = brightness;
    }
}

static uint8_t is31_update(void) {
    if (!is31_select_page(IS31_PAGE_FRAME1)) return 0;
    return i2c_write_regs(IS31_ADDR, IS31_PWM, led_pwm, IS31_LED_COUNT);
}

static uint8_t is31_init(void) {
    uint8_t on[IS31_LED_CONTROL_LEN];

    for (uint8_t i = 0; i < IS31_LED_CONTROL_LEN; i++) {
        on[i] = 0xFF;
    }

    if (!is31_write(IS31_PAGE_FUNCTION, IS31_REG_SHUTDOWN, 0x00)) return 0;
    if (!is31_write(IS31_PAGE_FUNCTION, IS31_REG_CONFIG, 0x00)) return 0;
    if (!is31_write(IS31_PAGE_FUNCTION, IS31_REG_PICTURE_FRAME, 0x00)) return 0;
    if (!is31_write(IS31_PAGE_FUNCTION, IS31_REG_AUDIO_SYNC, 0x00)) return 0;

    if (!is31_select_page(IS31_PAGE_FRAME1)) return 0;
    if (!i2c_write_regs(IS31_ADDR, IS31_LED_CONTROL, on, IS31_LED_CONTROL_LEN)) return 0;

    is31_set_all(0);
    if (!is31_update()) return 0;

    return is31_write(IS31_PAGE_FUNCTION, IS31_REG_SHUTDOWN, 0x01);
}

/* Button demux is on pins PD4, PD5, PD6, and PD7 */
static void demux_init(void) {
    DDRD |= ((1 << PD4) | (1 << PD5) | (1 << PD6) | (1 << PD7));

}


int main(void) {
    uart_init(); // enable uart
    sei(); // enable global interrupts. cli() disables them
    i2c_init();
    is31_init();
    is31_set_all(50);
    is31_update();
    
    while(1) {
        if(rx_buff.count > 0) { // only loop through the following things if there's anything in the rx_buff
            uint8_t serialosc_query = read_from_ring_buffer(&rx_buff); //read once from the ring buffer
            const char *name = "zacks monome grid";

            switch(serialosc_query) {
            case 0x00: //serialosc asking "what do you have?"
                /* answer */
                uart_send(0x00); // op code to answer this query
                uart_send(0x01); // index 1: "led-grid"
                uart_send(0x01); // 1 of them
                break;
            case 0x01: //serial osc asking "what's your name?"
                uart_send(0x01); // opcode to respond to this query
                for (uint8_t i = 0; i < 32; i++) {
                    if(*name) {
                        uart_send(*name);
                        name++;
                    } else {
                        uart_send(0); //pad the name with zeros as it must be 32 bytes
                    }
                }
                break;
            case 0x05: // how big is your grid? 
                uart_send(0x03); // opcode to reply
                uart_send(0x10); // num of columns (16 in hex)
                uart_send(0x08); // num of rows (8)
                break;
            }
        }
    }
}
