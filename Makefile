TARGET = heartbeat

MCU = atmega328pb
F_CPU = 16000000UL
PORT = /dev/tty.usbserial-210
BAUD = 115200

$(TARGET).elf: $(TARGET).c
	avr-gcc -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Os -o $(TARGET).elf $(TARGET).c

$(TARGET).hex: $(TARGET).elf
	avr-objcopy -O ihex -R .eeprom $(TARGET).elf $(TARGET).hex

# flash: $(TARGET).hex
# 	avrdude -c arduino -p $(MCU) -P $(PORT) -b $(BAUD) -U flash:w:$(TARGET).hex

flash: heartbeat.hex
	avrdude -c usbasp -p m328pb -B 125kHz -U flash:w:heartbeat.hex:i

clean:
	rm -f $(TARGET).elf $(TARGET).hex
