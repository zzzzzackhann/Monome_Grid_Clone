import serial, time
s = serial.Serial('/dev/cu.usbmodemm00000011', 115200, timeout=0.1)
time.sleep(2)
s.write(b'\x00')
while True:
    data = s.read(64)
    if data:
        print(data.hex(' '))
