#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026, Alexander Fox
"""Desktop waveform simulator for the Guitar Key Fob Flipper app.

Generates guitar-pluck envelopes, plots amplitude, and exports Flipper-compatible
RAW timing lines for testing (paste into a .sub file or compare on a scope).

Usage:
  python3 tools/guitar_key_fob.py unlock --plot
  python3 tools/guitar_key_fob.py alarm --export pluck.sub
"""

from __future__ import annotations

import argparse
import math
from dataclasses import dataclass
from pathlib import Path

# Mirror guitar_types.h defaults
NOTE_UNLOCK = 196.0
NOTE_LOCK = 247.0
NOTE_TRUNK = 330.0
DEFAULT_ATTACK_MS = 12
DEFAULT_DECAY_MS = 550
ASK_SAMPLE_US = 500


@dataclass
class PluckParams:
    base_hz: float
    harmonic2_ratio: float = 0.5
    harmonic3_ratio: float = 0.25
    pitch_bend_hz: float = 0.0
    pitch_bend_duration_s: float = 0.0
    vibrato_depth: float = 0.08
    vibrato_hz: float = 5.0
    attack_s: float = DEFAULT_ATTACK_MS / 1000.0
    decay_tau_s: float = DEFAULT_DECAY_MS / 1000.0
    duration_s: float | None = None

    def __post_init__(self) -> None:
        if self.duration_s is None:
            self.duration_s = self.attack_s + self.decay_tau_s * 4.0


def envelope(t: float, attack_s: float, decay_tau_s: float) -> float:
    if t < 0:
        return 0.0
    if t < attack_s:
        return t / attack_s
    return math.exp(-(t - attack_s) / decay_tau_s)


def pluck_amplitude(params: PluckParams, t: float) -> float:
    if t < 0 or t > params.duration_s:
        return 0.0
    env = envelope(t, params.attack_s, params.decay_tau_s)
    if env <= 0.0001:
        return 0.0

    freq = params.base_hz
    if params.pitch_bend_hz and params.pitch_bend_duration_s:
        bend = min(t / params.pitch_bend_duration_s, 1.0)
        freq += params.pitch_bend_hz * bend

    if params.vibrato_depth and params.vibrato_hz:
        freq *= 1.0 + 0.02 * params.vibrato_depth * math.sin(
            2.0 * math.pi * params.vibrato_hz * t
        )

    w = 2.0 * math.pi * freq
    sample = (
        math.sin(w * t)
        + params.harmonic2_ratio * math.sin(2.0 * w * t)
        + params.harmonic3_ratio * math.sin(3.0 * w * t)
    )
    norm = 1.0 + params.harmonic2_ratio + params.harmonic3_ratio
    return env * abs(sample / norm)


def params_for_action(action: str) -> PluckParams:
    action = action.lower()
    if action == "unlock":
        return PluckParams(base_hz=NOTE_UNLOCK)
    if action == "lock":
        return PluckParams(base_hz=NOTE_LOCK)
    if action == "trunk":
        p = PluckParams(base_hz=NOTE_TRUNK)
        p.pitch_bend_hz = 8.0
        p.pitch_bend_duration_s = p.duration_s
        return p
    if action == "alarm":
        return PluckParams(base_hz=NOTE_UNLOCK, duration_s=0.18)
    raise ValueError(f"Unknown action: {action}")


def build_ask_durations(params: PluckParams, threshold: float = 0.15) -> list[tuple[bool, int]]:
    dt = ASK_SAMPLE_US / 1_000_000.0
    samples: list[tuple[bool, int]] = []
    t = 0.0
    while t <= params.duration_s:
        on = pluck_amplitude(params, t) >= threshold
        samples.append((on, ASK_SAMPLE_US))
        t += dt

    merged: list[tuple[bool, int]] = []
    for level, duration in samples:
        if merged and merged[-1][0] == level:
            merged[-1] = (level, merged[-1][1] + duration)
        else:
            merged.append((level, duration))
    return merged


def export_sub(path: Path, frequency_hz: int, durations: list[tuple[bool, int]]) -> None:
    lines = [
        "Filetype: Flipper SubGhz RAW File",
        "Version: 1",
        f"Frequency: {frequency_hz}",
        "Preset: FuriHalSubGhzPresetOok650Async",
        "Protocol: RAW",
    ]
    chunk: list[str] = []
    for level, duration in durations:
        token = str(duration if level else -duration)
        chunk.append(token)
        if len(chunk) >= 32:
            lines.append("RAW_Data: " + " ".join(chunk))
            chunk = []
    if chunk:
        lines.append("RAW_Data: " + " ".join(chunk))
    path.write_text("\n".join(lines) + "\n")


def maybe_plot(params: PluckParams) -> None:
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print("Install matplotlib to use --plot")
        return

    dt = 0.001
    ts = [i * dt for i in range(int(params.duration_s / dt) + 1)]
    amps = [pluck_amplitude(params, t) for t in ts]
    plt.figure(figsize=(8, 3))
    plt.plot(ts, amps)
    plt.title(f"Guitar pluck {params.base_hz:.0f} Hz")
    plt.xlabel("Time (s)")
    plt.ylabel("Amplitude")
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.show()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "action",
        choices=["unlock", "lock", "trunk", "alarm"],
        help="Key fob action to simulate",
    )
    parser.add_argument("--frequency", type=int, default=315_000_000, help="Carrier Hz")
    parser.add_argument("--plot", action="store_true", help="Show amplitude plot")
    parser.add_argument("--export", type=Path, help="Write RAW .sub file")
    args = parser.parse_args()

    params = params_for_action(args.action)
    durations = build_ask_durations(params)

    print(f"Action: {args.action}")
    print(f"Base note: {params.base_hz:.1f} Hz")
    print(f"Duration: {params.duration_s:.3f} s")
    print(f"ASK segments: {len(durations)}")

    if args.plot:
        maybe_plot(params)

    if args.export:
        export_sub(args.export, args.frequency, durations)
        print(f"Exported {args.export}")


if __name__ == "__main__":
    main()
