/*midi controller*/

/*The next time you do a project like this, you should build one thing at a time and test that it's working, instead of building everything at once and then testing*/

#include <avr/io.h> //for register names
#include <avr/interrupt.h> //for ISR

#define DEBUG_MODE 0

#if DEBUG_MODE
    #define UBRR_VAL 103 //make baud rate 9600
#else 
    #define UBRR_VAL 31 //make baud rate 31250, MIDI standard
#endif

#define  DEBOUNCE_TIME 200

volatile uint16_t ms = 0;
volatile uint16_t int0_ms_last_press = 0;
volatile uint16_t int1_ms_last_press = 0;
volatile uint8_t int0_next_note = 1;
volatile uint8_t int1_next_note = 1;

void uart_init(void) {
    UBRR0H = 0;
    UBRR0L = UBRR_VAL;
    UCSR0B = (1 << TXEN0); 
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}

/*INT0 is PD2, INT1 is PD3 */
void interrupts_init(void) {
    EIMSK |= (1 << INT0) | (1 << INT1); //initialize interrupts 0 and 1
    EICRA |= (1 << ISC00) | (1 << ISC10); //sets any logical change to trigger interrupts
    DDRD &= ~((1 << 2) | (1 << 3)); //sets D2 and D3 as inputs (buttons)
    DDRD |= (1 << 4) | (1 << 5); //sets D4 and D5 as outputs
    PORTD |= (1 << 2) | (1 << 3); //pull D2 and D3 high
}


void timer0_init(void) {
    TCCR0A = (1 << WGM01); 
    TCCR0B = (1 << CS01) | (1 << CS00); //set prescaler to 64. 16MHz/64 = 250000 Hz
    OCR0A = 249;
    TIMSK0 = (1 << OCIE0A); //set output compare interrupt enable to enable the timer to trigger an interrupt (ISR)
}


void uart_send_char(uint8_t c) {
    while(!(UCSR0A & (1 << UDRE0))); //while the data register empty bit is NOT 1 in the control/status reg A, wait
    UDR0 = c;
}

ISR(INT0_vect) { //button on PD2
    uint8_t message[3];

    if((ms - int0_ms_last_press) >= DEBOUNCE_TIME) { 
        if (int0_next_note == 1) { //if the next note is on (like first press), send note-on message and then make next_note off
            message[0] = 0x90;
            message[1] = 0x3c;
            message[2] = 0x40;
            int0_next_note = 0;
        } else {
            message[0] = 0x80;
            message[1] = 0x3c;
            message[2] = 0x00;
            int0_next_note = 1;
        }
        uart_send_char(message[0]);
        uart_send_char(message[1]);
        uart_send_char(message[2]);

        int0_ms_last_press = ms;

        PORTD ^= (1 << 4); //alternate LED state
    }
}

ISR(INT1_vect) { //button on PD3
    uint8_t message[3];
    
    if((ms - int1_ms_last_press) >= DEBOUNCE_TIME) {
        if (int1_next_note == 1) { //if the next note is on (like first press), send note-on message and then make next_note off
            message[0] = 0x90;
            message[1] = 0x40;
            message[2] = 0x40;
            int1_next_note = 0;
        } else {
            message[0] = 0x80;
            message[1] = 0x40;
            message[2] = 0x00;
            int1_next_note = 1;
        }
        uart_send_char(message[0]);
        uart_send_char(message[1]);
        uart_send_char(message[2]);

        int1_ms_last_press = ms;

        PORTD ^= (1 << 5); //alternate LED state
    }
}

ISR (TIMER0_COMPA_vect) {
    ms++; 
}

/*When using A0 and A1 pins as interrupts, you don't actually need to set their DDR  */
uint16_t adc_read(uint8_t channel) {
    ADMUX = (1 << REFS0) | (channel & 0x0F); //set reference voltage to Vcc
    /*setting start conversion bit to 1 sets ADC to free running mode*/
    ADCSRA = (1 << ADEN) | (1 << ADSC) | (1 << ADPS0) | (1 << ADPS1) | (1 << ADPS2); 

    while(ADCSRA & (1 << ADSC));

    return ADC;
}

int main(void){
    sei();
    interrupts_init();
    uart_init();
    timer0_init();

    uint16_t ms_last_read = 0;
    uint16_t prev_cc_1 = 0;
    uint16_t prev_cc_2 = 0;

    /*ADC is getting a 10-bit value, so 0-1023. 1023/x = 127. x = 8.0551 */
    while(1) {
        uint16_t midi_cc_1 = adc_read(0) / 8.0551;
        uint16_t midi_cc_2 = adc_read(1) / 8.0551;

        if((ms - ms_last_read) >= 50) {
            if(midi_cc_1 != prev_cc_1) {
                uart_send_char(0xB0); //send CC message on channel 1
                uart_send_char(0x00); //CC 01
                uart_send_char(midi_cc_1); //midi value to send

                prev_cc_1 = midi_cc_1;
                ms_last_read = ms;
            } else if(midi_cc_2 != prev_cc_2) {
                uart_send_char(0xB0); //send CC message on channel 2
                uart_send_char(0x01); //CC 02
                uart_send_char(midi_cc_2);

                prev_cc_2 = midi_cc_2;
                ms_last_read = ms;
            }
        }
    }
}
