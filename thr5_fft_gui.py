#!/usr/bin/env python3
"""Simple live frequency-domain GUI for Yamaha THR5 input."""

from __future__ import annotations

import queue
import subprocess
import sys
import threading
import time

try:
    import matplotlib.pyplot as plt
    from matplotlib.animation import FuncAnimation
    import numpy as np
except ImportError as exc:
    print(
        "Missing dependency. Install with: pip install numpy matplotlib",
        file=sys.stderr,
    )
    print(str(exc), file=sys.stderr)
    sys.exit(1)


DEVICE_CANDIDATES = [
    "hw:2,0",
    "dsnoop:2,0",
    "plughw:2,0",
    "sysdefault:2,0",
    "default",
]
SAMPLE_RATE = 44100
CHANNELS = 2
FRAME_FORMAT = "S16_LE"
FRAMES_PER_CHUNK = 4096


def start_arecord(device: str) -> tuple[subprocess.Popen[bytes] | None, str]:
    cmd = [
        "arecord",
        "-D",
        device,
        "-f",
        FRAME_FORMAT,
        "-c",
        str(CHANNELS),
        "-r",
        str(SAMPLE_RATE),
        "-t",
        "raw",
        "-q",
        "-",
    ]

    try:
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    except FileNotFoundError:
        return None, "arecord not found. Install ALSA utilities."

    if proc.stdout is None or proc.stderr is None:
        return None, "failed to open arecord streams."

    # Open failures (busy/no device) return quickly.
    time.sleep(0.15)
    if proc.poll() is not None:
        err = proc.stderr.read().decode("utf-8", errors="replace").strip()
        return None, err or f"arecord exited with code {proc.returncode}"

    return proc, ""


class CaptureThread(threading.Thread):
    def __init__(self, proc: subprocess.Popen[bytes], out_q: queue.Queue[np.ndarray]):
        super().__init__(daemon=True)
        self.proc = proc
        self.out_q = out_q
        self.stop_event = threading.Event()

    def run(self) -> None:
        if self.proc.stdout is None:
            return

        bytes_per_frame = CHANNELS * 2
        chunk_bytes = FRAMES_PER_CHUNK * bytes_per_frame

        while not self.stop_event.is_set():
            chunk = self.proc.stdout.read(chunk_bytes)
            if not chunk:
                break

            samples = np.frombuffer(chunk, dtype=np.int16)
            if samples.size < CHANNELS:
                continue

            # Stereo -> mono for a single FFT curve.
            mono = samples.reshape(-1, CHANNELS).mean(axis=1).astype(np.float32)
            try:
                self.out_q.put_nowait(mono)
            except queue.Full:
                try:
                    self.out_q.get_nowait()
                except queue.Empty:
                    pass
                self.out_q.put_nowait(mono)

    def stop(self) -> None:
        self.stop_event.set()
        self.proc.terminate()
        try:
            self.proc.wait(timeout=0.8)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()


def choose_device() -> tuple[subprocess.Popen[bytes] | None, str]:
    errors: list[tuple[str, str]] = []
    for dev in DEVICE_CANDIDATES:
        proc, err = start_arecord(dev)
        if proc is not None:
            return proc, dev
        errors.append((dev, err))

    print("Could not open any THR5 input candidate.", file=sys.stderr)
    for dev, err in errors:
        print(f"- {dev}: {err}", file=sys.stderr)
    return None, ""


def main() -> int:
    proc, selected = choose_device()
    if proc is None:
        return 1

    print(f"Using input device: {selected}")

    audio_q: queue.Queue[np.ndarray] = queue.Queue(maxsize=4)
    cap = CaptureThread(proc, audio_q)
    cap.start()

    freqs = np.fft.rfftfreq(FRAMES_PER_CHUNK, d=1.0 / SAMPLE_RATE)
    y_init = np.full(freqs.shape, -120.0, dtype=np.float32)

    fig, ax = plt.subplots()
    (line,) = ax.plot(freqs, y_init, lw=1.0)
    ax.set_title("THR5 Spectrum (Positive Frequencies)")
    ax.set_xlabel("Frequency (Hz)")
    ax.set_ylabel("Magnitude (dBFS)")
    ax.set_xlim(0, SAMPLE_RATE / 2.0)
    ax.set_ylim(-110, 0)
    ax.grid(True, alpha=0.25)

    window = np.hanning(FRAMES_PER_CHUNK).astype(np.float32)
    latest = y_init

    def update(_: int):
        nonlocal latest
        got = False
        while True:
            try:
                chunk = audio_q.get_nowait()
                got = True
            except queue.Empty:
                break

        if got:
            if chunk.size != FRAMES_PER_CHUNK:
                if chunk.size > FRAMES_PER_CHUNK:
                    chunk = chunk[:FRAMES_PER_CHUNK]
                else:
                    padded = np.zeros(FRAMES_PER_CHUNK, dtype=np.float32)
                    padded[: chunk.size] = chunk
                    chunk = padded

            fft_vals = np.fft.rfft(chunk * window)
            mag = np.abs(fft_vals) / (FRAMES_PER_CHUNK / 2.0)
            dbfs = 20.0 * np.log10(np.maximum(mag / 32768.0, 1e-12))
            latest = dbfs.astype(np.float32)

        line.set_ydata(latest)
        return (line,)

    def on_close(_: object) -> None:
        cap.stop()

    fig.canvas.mpl_connect("close_event", on_close)
    _ani = FuncAnimation(fig, update, interval=33, blit=True, cache_frame_data=False)

    try:
        plt.show()
    finally:
        cap.stop()

    return 0


if __name__ == "__main__":
    sys.exit(main())
