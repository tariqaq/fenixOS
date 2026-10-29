"""Spike S3: link speed test against board/spikes/s3 (UART echo firmware).

For each baud: send random 1000-byte blocks (each with a CRC32), read the echo,
count corrupt or missing blocks, and report throughput.
Usage: python tools/spike_s3_baud.py [seconds_per_baud]
"""
import os, sys, time, zlib

import serial

SECS = float(sys.argv[1]) if len(sys.argv) > 1 else 180
BAUDS = [460800, 921600, 1500000, 2000000]

s = serial.Serial()
# Timeout 0: reads never wait in the driver, `read` below polls instead. A read timeout takes the
# CH340 driver's crash path (docs/board-info.md, P23).
s.port, s.baudrate, s.timeout = "COM9", 460800, 0


def read(n, wait=0.5, until=None):
    out = b""
    end = time.time() + wait
    while len(out) < n and time.time() < end and not (until and out.endswith(until)):
        b = s.read(n - len(out))
        if b:
            out += b
        else:
            time.sleep(0.002)
    return out


s.dtr = False
s.rts = False
s.open()
time.sleep(0.3)
s.reset_input_buffer()
cur = 460800
for baud in BAUDS:
    if baud != cur:
        s.write(f"BAUD {baud}\n".encode())
        s.flush()
        ok = read(16, until=b"OK\n")
        time.sleep(0.05)
        s.baudrate = baud
        cur = baud
        time.sleep(0.1)
        s.reset_input_buffer()
        if not ok.endswith(b"OK\n"):
            print(f"{baud}: switch failed ({ok!r})")
            break
    good = bad = 0
    t0 = time.time()
    while time.time() - t0 < SECS:
        body = os.urandom(996)
        blk = body + zlib.crc32(body).to_bytes(4, "little")
        s.write(blk)
        got = read(1000)
        if len(got) == 1000 and zlib.crc32(got[:996]).to_bytes(4, "little") == got[996:]:
            good += 1
        else:
            bad += 1
            time.sleep(0.05)
            s.reset_input_buffer()
    dt = time.time() - t0
    print(f"{baud:>8}: {good} ok, {bad} bad, {good * 1000 / dt / 1024:.1f} KiB/s each way (round trip, 1000 B blocks)", flush=True)
s.close()
