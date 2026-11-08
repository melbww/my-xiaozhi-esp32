#!/usr/bin/env python3
"""Reset the board over CH343 (DTR/RTS) and capture serial output for N seconds."""
import sys
import time

import serial

port = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbmodem5D300345091"
seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 30.0

ser = serial.Serial(port, 115200, timeout=0.2)
# Reset pulse: RTS -> EN, DTR -> GPIO0 (typical ESP devkit auto-reset circuit)
ser.dtr = False
ser.rts = True
time.sleep(0.15)
ser.rts = False
time.sleep(0.05)
# Drop anything buffered before the reset (ROM garbage)
ser.reset_input_buffer()

deadline = time.time() + seconds
while time.time() < deadline:
    data = ser.read(4096)
    if data:
        sys.stdout.write(data.decode("utf-8", "replace"))
        sys.stdout.flush()
ser.close()
