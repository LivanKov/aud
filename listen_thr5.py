#!/usr/bin/env python3
"""Read live audio from a Yamaha THR5 via ALSA and display input levels."""

import array
import math
import shutil
import subprocess
import sys
import time

# Try direct hardware first, then software/alias fallbacks.
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
FRAMES_PER_CHUNK = 2048
MIN_DBFS = -60.0


def rms_dbfs(samples: array.array) -> float:
    if not samples:
        return -120.0

    sum_squares = 0.0
    for sample in samples:
        sum_squares += float(sample) * float(sample)

    rms = math.sqrt(sum_squares / len(samples))
    if rms <= 0.0:
        return -120.0
    return 20.0 * math.log10(rms / 32768.0)


def meter_bar(dbfs: float, width: int) -> str:
    clipped_dbfs = max(MIN_DBFS, min(0.0, dbfs))
    ratio = (clipped_dbfs - MIN_DBFS) / (0.0 - MIN_DBFS)
    filled = int(round(ratio * width))
    return "[" + ("#" * filled) + (" " * (width - filled)) + "]"


def print_usage_hint() -> None:
    print(
        "Tip: if card indices change after reboot, check `/proc/asound/cards` "
        "and adjust DEVICE_CANDIDATES in this script.",
        file=sys.stderr,
    )


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
        process = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except FileNotFoundError:
        return None, "Error: `arecord` not found. Install ALSA utilities first."

    if process.stdout is None or process.stderr is None:
        return None, "Error: failed to open arecord streams."

    # arecord exits quickly when device open fails (busy/no such device/etc).
    time.sleep(0.15)
    if process.poll() is not None:
        stderr_text = process.stderr.read().decode("utf-8", errors="replace").strip()
        return None, stderr_text or f"arecord exited with code {process.returncode}"

    return process, ""


def main() -> int:
    process = None
    selected_device = ""
    errors = []
    for candidate in DEVICE_CANDIDATES:
        process, error = start_arecord(candidate)
        if process is not None:
            selected_device = candidate
            break
        errors.append((candidate, error))

    if process is None:
        busy_seen = any("Device or resource busy" in error for _, error in errors)
        print("Error: could not open any THR5 input candidate.", file=sys.stderr)
        for device, error in errors:
            print(f"- {device}: {error}", file=sys.stderr)
        if busy_seen:
            print(
                "Hint: another app has the THR5 input open. Close DAW/browser/other "
                "recorders and retry.",
                file=sys.stderr,
            )
        return 1

    print(f"Listening to {selected_device} at {SAMPLE_RATE} Hz. Press Ctrl+C to stop.")
    print_usage_hint()

    bytes_per_frame = CHANNELS * 2  # 16-bit signed PCM, stereo
    bytes_per_chunk = FRAMES_PER_CHUNK * bytes_per_frame

    try:
        while True:
            chunk = process.stdout.read(bytes_per_chunk)
            if not chunk:
                break

            samples = array.array("h")
            samples.frombytes(chunk)
            if len(samples) < 2:
                continue

            left_samples = samples[0::2]
            right_samples = samples[1::2]

            left_dbfs = rms_dbfs(left_samples)
            right_dbfs = rms_dbfs(right_samples)

            terminal_width = shutil.get_terminal_size(fallback=(100, 24)).columns
            meter_width = max(10, min(36, (terminal_width - 30) // 2))

            left_meter = meter_bar(left_dbfs, meter_width)
            right_meter = meter_bar(right_dbfs, meter_width)
            print(
                f"\rL {left_meter} {left_dbfs:6.1f} dBFS | "
                f"R {right_meter} {right_dbfs:6.1f} dBFS",
                end="",
                flush=True,
            )
    except KeyboardInterrupt:
        pass
    finally:
        process.terminate()
        try:
            process.wait(timeout=1.0)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()

    stderr_text = process.stderr.read().decode("utf-8", errors="replace").strip()
    if process.returncode not in (0, None):
        print("\n", file=sys.stderr)
        print(f"arecord exited with code {process.returncode}.", file=sys.stderr)
        if stderr_text:
            print(stderr_text, file=sys.stderr)
        return 1

    print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
