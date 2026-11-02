#!/usr/bin/env python3
"""Print the dominant frequency over time in a WAV file.

usage: analyse.py file.wav [window_seconds]

The WAV is cut into windows. Each window gets its strongest frequency
(Hann window, FFT, parabolic peak interpolation) and its RMS level.
Neighbouring windows with the same frequency (within 3 percent) merge into
one line: start time, end time, frequency, level. Windows below a small
level print as silence.
"""
import sys
import wave
import numpy as np


def load(path):
    with wave.open(path, "rb") as w:
        rate = w.getframerate()
        ch = w.getnchannels()
        raw = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2")
    x = raw.reshape(-1, ch).astype(float) / 32768.0
    return rate, x.mean(axis=1)


def peak(win, rate):
    spec = np.abs(np.fft.rfft(win * np.hanning(len(win))))
    spec[0:2] = 0
    k = int(np.argmax(spec))
    if 1 <= k < len(spec) - 1:
        a, b, c = np.log(spec[k - 1] + 1e-12), np.log(spec[k] + 1e-12), np.log(spec[k + 1] + 1e-12)
        d = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0
        k = k + d
    flat = np.exp(np.mean(np.log(spec[1:] + 1e-12))) / (np.mean(spec[1:]) + 1e-12)
    return k * rate / len(win), flat


def segments(path, win_s=0.25):
    rate, x = load(path)
    n = int(rate * win_s)
    out = []
    for i in range(0, len(x) - n + 1, n):
        w = x[i:i + n]
        w = w - w.mean()
        rms = float(np.sqrt(np.mean(w * w)))
        if rms < 0.003:
            out.append((i / rate, None, rms, 0.0))
            continue
        f, flat = peak(w, rate)
        out.append((i / rate, f, rms, flat))
    merged = []
    for t, f, rms, flat in out:
        same = False
        if merged:
            pt0, pt1, pf, prms, pflat, cnt = merged[-1]
            if (pf is None and f is None) or (pf is not None and f is not None and abs(f - pf) < 0.03 * pf):
                same = True
        if same:
            merged[-1] = (pt0, t + win_s, pf, (prms * cnt + rms) / (cnt + 1), (pflat * cnt + flat) / (cnt + 1), cnt + 1)
        else:
            merged.append((t, t + win_s, f, rms, flat, 1))
    return merged


if __name__ == "__main__":
    win = float(sys.argv[2]) if len(sys.argv) > 2 else 0.25
    for t0, t1, f, rms, flat, cnt in segments(sys.argv[1], win):
        if f is None:
            print("%7.2f - %7.2f  silence" % (t0, t1))
        else:
            kind = "noise-like" if flat > 0.25 else "tonal"
            print("%7.2f - %7.2f  %8.1f Hz  rms %.3f  %s" % (t0, t1, f, rms, kind))
