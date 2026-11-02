# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Bare-metal AVR C firmware (no Arduino core, no libraries beyond avr-libc) for a USB MIDI controller built on an **ATmega328PB @ 16 MHz crystal**, plus the KiCad design files for its boards.

Each top-level `.c` file is a standalone program with its own `main()`; there is no shared code or headers. They are bring-up tests, built one at a time:

- `hello_serial.c` – prints "hello" over UART0 at 9600 baud to verify the USB-serial path.
- `heartbeat.c` – sends middle-C note-on/off every 500 ms at MIDI baud (31250) to verify the full MIDI chain. Touches only PD1 and Timer0; every other pin stays an input so it can't fight the V2 board's decoder, LED driver, or matrix rows.
- `midi-02.c` – earlier controller logic: two buttons on INT0/INT1 (PD2/PD3) toggling notes + LEDs on PD4/PD5, two pots on ADC0/ADC1 sent as CC. Hardcodes UBRR (no `F_CPU` define); `DEBUG_MODE 1` switches to 9600 baud.
- `osc-test.c` – currently empty.

## Build & flash

The `Makefile` builds one program, selected by `TARGET` (default `hello_serial`):

    make                         # build $(TARGET).hex
    make TARGET=heartbeat        # build a different program
    make flash TARGET=heartbeat  # flash via USBasp
    make clean TARGET=heartbeat

Key toolchain quirk: the installed avr-gcc headers don't know the 328PB, so code is **compiled as `-mmcu=atmega328p`** while avrdude uses **`-p m328pb`** (the real chip, needed for the signature check). Keep these two separate; don't "fix" one to match the other. Only 328P-compatible peripherals/registers are usable (e.g. USART0, not the 328PB's extra USART1/timers) unless the headers are updated.

Flashing uses a USBasp on header J2 with USB-C unplugged (`-B 125kHz`). Afterwards unplug the USBasp and connect USB-C.

There are no tests or linters; verification is on hardware:
- Serial: `ls /dev/tty.usbmodem*` then `screen /dev/tty.usbmodemXXXX 9600`.
- MIDI: the device appears as "arduino_midi" in Max (`midiin`) or any MIDI monitor.

## Hardware signal path

328PB TXD0 (PD1) → ATmega16U2 RXD1 (PD2) → USB → Mac. The 16U2 firmware determines what the host sees: a USB-serial bridge (LUFA USBtoSerial; `Arduino-COMBINED-dfu-usbserial-atmega16u2-Uno-Rev3.hex`) or a USB-MIDI device (`arduino_midi.hex`). So the UART baud on the 328PB must match the 16U2 firmware currently loaded: 9600 for serial, 31250 for MIDI.

## Code conventions

Follow the style of `hello_serial.c` / `heartbeat.c` (the newer files):
- `F_CPU` passed by the Makefile with an `#ifndef` fallback; UBRR computed from `F_CPU` and baud.
- `millis()` from Timer0 CTC at 1 kHz (prescaler 64, `OCR0A = 249`), read inside `ATOMIC_BLOCK(ATOMIC_RESTORESTATE)` because the counter is multi-byte.
- Elapsed-time checks use the rollover-safe `(now - last) >= interval` form.
- `static` helpers, section banner comments, and comments explaining *why* register bits are set.

## Repo layout

- `Midi_Controller_02/` – KiCad project for the main board (hierarchical sheets for the button/LED matrix, `usb-midi-uart.kicad_sch`) and `Midi_Controller_02_Cntrol/` for the control board. `production/` folders hold fab outputs (Gerber zips, BOM, positions) from the fabrication-toolkit plugin.
- `Adafruit_Trellis/` – reference KiCad footprints/symbols (tracked, though now listed in `.gitignore`).
- Gitignored local-only: `ISP_programmer/` (KiCad ISP board), `*[abandoned]/` older board revisions, and build outputs (`*.elf`, `*.hex`, including the 16U2 firmware hex files).
