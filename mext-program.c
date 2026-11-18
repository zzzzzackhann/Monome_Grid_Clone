/* On 328PB, RX is PD0 and TX and PD1 */
/* This is a test that stores data (chars) in a buffer of limited size (a ring buffer),
reads each byte and sends it back to the computer over uart. 
them one by one*/

/* I2C SDA is on PB0 and SCL is on PB1 */

#ifndef F_CPU
#define F_CPU 16000000UL
#endif 

#define DEBUG 0
#define RX_BUFFER_SIZE 128

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

#define DEMUX_A0 PD4
#define DEMUX_A1 PD5
#define DEMUX_A2 PD6
#define DEMUX_A3 PD7
#define DEMUX_MASK ((1 << DEMUX_A0) | (1 << DEMUX_A1) | (1 << DEMUX_A2) | (1 << DEMUX_A3))

#define ROWS_C_MASK ((1 << PC0) | (1 << PC1) | (1 << PC2) | (1 << PC3) | (1 << PC4) | (1 << PC5))
#define ROWS_D_MASK ((1 << PD2) | (1 << PD3))

#define GRID_COLS 16
#define GRID_ROWS 8
#define KEY_SCAN_MS 5

static volatile uint32_t ms_ticks = 0;

/* Button demux is on pins PD4, PD5, PD6, and PD7 */
static void demux_init(void) {
    DDRD |= DEMUX_MASK; // set 4 demux pins (A0-A3) as ouputs, default driven low
    
    DDRD &= ~ROWS_D_MASK; //turn PD2 and PD3 into inputs for Row 6 and Row 7 
    PORTD |= ROWS_D_MASK; // pull input pins high with internal pullup resistor
    DDRC &= ~ROWS_C_MASK; // input pins from Rows 0-5
    PORTC |= ((1 << PC0) | (1 << PC1) | (1 << PC2) | (1 << PC3) | (1 << PC4) | (1 << PC5)); // set pull up resistor
}

/* This is a sick technique for scanning through a fixed value in the registers*/
/* While row 1 is being held high by pull up resistors, the columns cycle through all of them.
If during any column check the pin goes low, it's because the button is pressed. */
static void demux_select(uint8_t column) {
    uint8_t d = PORTD & ~DEMUX_MASK; // keeps low four bits as they are and CLEARS four demux address bits (PD4-7)

    if (column & 0x01) d |= (1 << DEMUX_A0); // is bit 0 of the column number set? (true for columns 1, 3, 5 etc. )
    if (column & 0x02) d |= (1 << DEMUX_A1);
    if (column & 0x04) d |= (1 << DEMUX_A2);
    if (column & 0x08) d |= (1 << DEMUX_A3);

    PORTD = d;
}

static uint8_t read_rows(void) {
    return (PINC & ROWS_C_MASK) | ((PIND & ROWS_D_MASK) << 4);
}

static uint8_t button_state[GRID_COLS * GRID_ROWS];

static void demux_scan(void) {
    for (uint8_t column = 0; column < GRID_COLS; column++) {
        demux_select(column);
        _delay_us(5); // give the electrical reading time to settle
        uint8_t rows = read_rows();

        for (uint8_t row = 0; row < GRID_ROWS; row++) {
            uint8_t pressed = !(rows & (1 << row));
            uint8_t i = row * GRID_COLS + column;

            /* If the current state of the button is different than the previosuly stored state, send the messages to change the state*/
            if (pressed != button_state[i]) {
                button_state[i] = pressed;
                uart_send(pressed ? 0x21 : 0x20);
                uart_send(column);
                uart_send(row);
            }
        }
    }
}


#define MEXT_MAX_PAYLOAD 34
#define LED_REFRESH_MS 20

ISR(TIMER0_COMPA_vect) {
    ms_ticks++;
}

static void timer0_init(void) {
    TCCR0A = (1 << WGM01);
    TCCR0B = (1 << CS01) | (1 << CS00);
    OCR0A  = 249;
    TIMSK0 = (1 << OCIE0A);
}

static uint32_t millis(void) {
    uint32_t t;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        t = ms_ticks;
    }
    return t;
}

static uint8_t grid_level[GRID_COLS * GRID_ROWS];
static uint8_t grid_intensity = 15;
static uint8_t leds_dirty;

static uint8_t led_index(uint8_t x, uint8_t y) {
    return y * GRID_COLS + x;
}

static void leds_render(void) {
    for (uint8_t y = 0; y < GRID_ROWS; y++) {
        for (uint8_t x = 0; x < GRID_COLS; x++) {
            uint16_t level = grid_level[y * GRID_COLS + x];
            led_pwm[led_index(x, y)] = (level * 17 * (grid_intensity + 1)) >> 4;
        }
    }
    is31_update();
}

static void set_level(uint8_t x, uint8_t y, uint8_t level) {
    if (x < GRID_COLS && y < GRID_ROWS) {
        if (level > 15) level = 15;
        grid_level[y * GRID_COLS + x] = level;
        leds_dirty = 1;
    }
}

static void set_all(uint8_t level) {
    for (uint8_t y = 0; y < GRID_ROWS; y++) {
        for (uint8_t x = 0; x < GRID_COLS; x++) {
            set_level(x, y, level);
        }
    }
}

static uint8_t nibble(const uint8_t *packed, uint8_t n) {
    uint8_t b = packed[n / 2];
    return (n & 1) ? (b & 0x0F) : (b >> 4);
}

static void mext_send_id(void) {
    const char *name = "zacks monome grid";

    uart_send(0x01);
    for (uint8_t i = 0; i < 32; i++) {
        if (*name) {
            uart_send(*name);
            name++;
        } else {
            uart_send(0);
        }
    }
}

static uint8_t mext_payload_length(uint8_t cmd) {
    switch (cmd) {
    case 0x02: return 32;
    case 0x04: return 3;
    case 0x06: return 2;
    case 0x08: return 2;
    case 0x10: return 2;
    case 0x11: return 2;
    case 0x14: return 10;
    case 0x15: return 3;
    case 0x16: return 3;
    case 0x17: return 1;
    case 0x18: return 3;
    case 0x19: return 1;
    case 0x1A: return 34;
    case 0x1B: return 6;
    case 0x1C: return 6;
    default:   return 0;
    }
}

static void mext_handle(uint8_t cmd, const uint8_t *p) {
    uint8_t x = p[0];
    uint8_t y = p[1];

    switch (cmd) {
    case 0x00:
        uart_send(0x00);
        uart_send(0x01);
        uart_send(0x01);
        break;
    case 0x01:
        mext_send_id();
        break;
    case 0x05:
        uart_send(0x03);
        uart_send(GRID_COLS);
        uart_send(GRID_ROWS);
        break;
    case 0x10:
        set_level(x, y, 0);
        break;
    case 0x11:
        set_level(x, y, 15);
        break;
    case 0x12:
        set_all(0);
        break;
    case 0x13:
        set_all(15);
        break;
    case 0x14:
        for (uint8_t r = 0; r < 8; r++) {
            for (uint8_t c = 0; c < 8; c++) {
                set_level(x + c, y + r, (p[2 + r] & (1 << c)) ? 15 : 0);
            }
        }
        break;
    case 0x15:
        for (uint8_t c = 0; c < 8; c++) {
            set_level(x + c, y, (p[2] & (1 << c)) ? 15 : 0);
        }
        break;
    case 0x16:
        for (uint8_t r = 0; r < 8; r++) {
            set_level(x, y + r, (p[2] & (1 << r)) ? 15 : 0);
        }
        break;
    case 0x17:
        grid_intensity = p[0] & 0x0F;
        leds_dirty = 1;
        break;
    case 0x18:
        set_level(x, y, p[2]);
        break;
    case 0x19:
        set_all(p[0]);
        break;
    case 0x1A:
        for (uint8_t n = 0; n < 64; n++) {
            set_level(x + (n % 8), y + (n / 8), nibble(&p[2], n));
        }
        break;
    case 0x1B:
        for (uint8_t n = 0; n < 8; n++) {
            set_level(x + n, y, nibble(&p[2], n));
        }
        break;
    case 0x1C:
        for (uint8_t n = 0; n < 8; n++) {
            set_level(x, y + n, nibble(&p[2], n));
        }
        break;
    }
}

static uint8_t mext_cmd;
static uint8_t mext_len;
static uint8_t mext_pos;
static uint8_t mext_busy;
static uint8_t mext_payload[MEXT_MAX_PAYLOAD];

static void mext_receive(uint8_t byte) {
    if (!mext_busy) {
        mext_cmd = byte;
        mext_len = mext_payload_length(byte);
        mext_pos = 0;
        mext_busy = 1;
    } else {
        mext_payload[mext_pos++] = byte;
    }

    if (mext_pos == mext_len) {
        mext_busy = 0;
        mext_handle(mext_cmd, mext_payload);
    }
}

int main(void) {
    uart_init();
    timer0_init();
    i2c_init();
    demux_init();
    sei();

    is31_init();

    uint32_t last_scan = 0;
    uint32_t last_led = 0;

    while (1) {
        while (rx_buff.count > 0) {
            mext_receive(read_from_ring_buffer(&rx_buff));
        }

        uint32_t now = millis();

        if ((now - last_scan) >= KEY_SCAN_MS) {
            last_scan = now;
            demux_scan();
        }

        if (leds_dirty && (now - last_led) >= LED_REFRESH_MS) {
            last_led = now;
            leds_dirty = 0;
            leds_render();
        }
    }
}
