TARGET       = hello_serial

# avr-gcc name: the installed headers don't know the 328PB, so compile as a 328P
MCU          = atmega328p
# avrdude name: the chip actually on the board (needed for the signature check)
AVRDUDE_PART = m328pb
F_CPU        = 16000000UL

# These are actions, not files
.PHONY: all flash clean

# First rule = default, so plain `make` builds the hex
all: $(TARGET).hex

$(TARGET).elf: $(TARGET).c
	avr-gcc -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Os -o $@ $<

$(TARGET).hex: $(TARGET).elf
	avr-objcopy -O ihex -R .eeprom $< $@

flash: $(TARGET).hex
	avrdude -c usbasp -p $(AVRDUDE_PART) -B 125kHz -U flash:w:$<:i

clean:
	rm -f $(TARGET).elf $(TARGET).hex
