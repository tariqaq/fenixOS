"""Read the board's UART for a while and print it (spikes and debugging).

Usage: python tools/serial_read.py [seconds] [--reset] [--port COM9] [--baud 115200]
--reset pulses EN through RTS (the CYD auto-reset circuit) before reading.
The port is opened with DTR and RTS released, so opening alone does not reset the board.
Reads never wait (timeout 0) and the loop polls: a read timeout takes the CH340 driver's crash
path (docs/board-info.md, P23).
"""
import argparse, sys, time

import serial

ap = argparse.ArgumentParser()
ap.add_argument("seconds", nargs="?", type=float, default=10)
ap.add_argument("--reset", action="store_true")
ap.add_argument("--port", default="COM9")
ap.add_argument("--baud", type=int, default=115200)
a = ap.parse_args()

s = serial.Serial()
s.port, s.baudrate, s.timeout = a.port, a.baud, 0
s.dtr = False
s.rts = False
s.open()
if a.reset:
    s.rts = True
    time.sleep(0.1)
    s.rts = False
end = time.time() + a.seconds
while time.time() < end:
    data = s.read(4096)
    if data:
        sys.stdout.write(data.decode("utf-8", "replace"))
        sys.stdout.flush()
    else:
        time.sleep(0.01)
s.close()
