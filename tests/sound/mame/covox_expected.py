#!/usr/bin/env python3
"""Write the sound the covox test should produce, from the test's own data.

usage: covox_expected.py out.wav [cpu_hz]

MAME has no Spectrum Covox device, so this takes the sine table and the
loop timing from covox.asm. For each port the test plays the wave twice:
first with the Covox disabled (silence), then enabled. The output is that
sequence with the same durations, held sample by sample as a DAC does.
cpu_hz is 3500000 for a 48K (default) or 3546900 for a 128K.
"""
import sys
import wave
import numpy as np

SINE = [128, 152, 175, 196, 214, 228, 237, 242, 243, 242, 237, 228, 214, 196, 175, 152,
        128, 104, 81, 60, 42, 28, 19, 14, 13, 14, 19, 28, 42, 60, 81, 104]
# (port, delay count N, cycles) as in covox.asm
PORTS = [(0xFB, 5, 1378), (0xDD, 10, 892), (0xDF, 2, 2052)]
RATE = 48000


def cycle_tstates(n):
    return 32 * (53 + 13 * n) + 31


def wave_samples(n, cycles, cpu):
    # Value of the DAC at each output instant, held until the next write.
    dt = (53 + 13 * n) / cpu
    t_cycle = cycle_tstates(n) / cpu
    total = cycles * t_cycle
    t = np.arange(int(total * RATE)) / RATE
    k = np.floor((t % t_cycle) / dt).astype(int)
    k = np.minimum(k, 31)
    return (np.array(SINE)[k] - 128) / 128.0 * 0.5, total


def main():
    out = sys.argv[1]
    cpu = float(sys.argv[2]) if len(sys.argv) > 2 else 3500000.0
    parts = []
    for port, n, cycles in PORTS:
        w, total = wave_samples(n, cycles, cpu)
        print("port 0x%02X: N=%d, %.1f Hz, %.2f s" % (port, n, cpu / cycle_tstates(n), total))
        parts.append(np.zeros_like(w))
        parts.append(w)
    data = np.concatenate(parts)
    pcm = (data * 32767).astype("<i2")
    with wave.open(out, "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(RATE)
        f.writeframes(pcm.tobytes())


main()
