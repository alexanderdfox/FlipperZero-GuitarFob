# Guitar Key Fob — Flipper Zero Sub-GHz App

Turn your Flipper Zero into a guitar-pluck style analog RF key fob. Each button sends an envelope-shaped **ASK (amplitude-shift keying)** burst on **315 MHz (US)** or **433.92 MHz (EU)**, with harmonics, vibrato, and an optional rolling identifier preamble.

> **Note:** Flipper’s CC1101 radio sends digital OOK/ASK, not true linear AM audio. This app approximates a “guitar pluck” by modulating carrier on/off in time with a decaying sine envelope — the same approach used for RAW `.sub` replay, but generated in real time.

## Features

| Button | Note | Behavior |
|--------|------|----------|
| **Unlock** | G (196 Hz) | Fundamental + 2nd/3rd harmonics |
| **Lock** | B (247 Hz) | Single-note pluck |
| **Trunk** | E (330 Hz) | Slow +8 Hz pitch bend |
| **Alarm** | G–B–E–G riff (default) | Four quick plucks — **customizable** |

Each action uses its own **custom riff** — a comma-separated list of note frequencies (Hz). Optional pitch bend per note with `+BEND` (e.g. `330+8`).

Additional behavior:

- **Rolling code preamble** — encodes button ID + incrementing counter before each pluck
- **Speaker feedback** — audible pluck on the Flipper speaker during TX/RX (toggle in Settings)
- **Unified app** — one FAP with **Transmit** and **Receive** modes from the main menu
- **Persistent settings** — TX: `/ext/apps_data/guitar_key_fob/settings.txt`, RX: `/ext/apps_data/guitar_riff_rx/settings.txt`

## Project structure

```
guitar_key_fob/          # Unified TX + RX FAP
tools/
  guitar_key_fob.py      # Desktop waveform simulator
```

### App modules (`guitar_key_fob/`)

```
  guitar_key_fob.c       # Main menu + entry point
  guitar_tx.c            # Transmit mode
  guitar_rx.c            # Receive mode
  waveform.c/h, rf_tx.c/h, riff.c/h, settings.c/h   # TX engine
  rx_decoder.c/h, rx_settings.c/h, rx_ui.c/h       # RX engine
```

**Receive mode features:**

- **Listen** — async Sub-GHz RX on 315 / 433.92 MHz
- **Decode** — rolling code + note sequence + button ID from preamble
- **Identify** — shows which button was pressed (Unlock / Lock / Trunk / Alarm)
- **Sync Riff** — save last received riff to any profile slot
- **Speaker** — replay received riff on the Flipper speaker

## Requirements

- [Flipper Zero](https://flipperzero.one/) with official firmware (recent release recommended)
- [uFBT](https://github.com/flipperdevices/flipperzero-ufbt) — `pip install ufbt`
- USB data cable for `ufbt launch`

## Build & install

```bash
make build      # → guitar_key_fob/dist/guitar_key_fob.fap
make install    # build + upload over USB
```

Or manually: `cd guitar_key_fob && ufbt && ufbt launch`

Copy `guitar_key_fob.fap` to `apps/Sub-GHz/` on the Flipper SD card.

If `ufbt` is not found after `pip install ufbt`, reload your shell or add Python user scripts to PATH:

```bash
export PATH="$HOME/Library/Python/3.9/bin:$PATH"
```

## Two-Flipper demo

1. **Flipper A** — Guitar Key Fob → **Transmit** → any button (set carrier in Settings)
2. **Flipper B** — Guitar Key Fob → **Receive** → **Listen**
3. RX shows which button was pressed, decoded riff, and rolling code
4. **Sync Riff** → pick Unlock/Lock/Trunk/Alarm to store the pattern for future matching

## Usage on device

Open **Apps → Sub-GHz → Guitar Key Fob** and pick **Transmit** or **Receive**.

### Transmit mode

1. Select **Unlock**, **Lock**, **Trunk**, or **Alarm Riff**
2. Flipper transmits the pluck and shows status (frequency + rolling code)
3. **Settings** → adjust carrier band, envelope, harmonics, vibrato, speaker
4. **Settings → Edit Riffs** → customize the note sequence for Unlock, Lock, Trunk, and Alarm
5. Press **Back** to return to the main menu

### Custom riff format

| Action | Default riff | Meaning |
|--------|--------------|---------|
| Unlock | `196` | Single G note (196 Hz) |
| Lock | `247` | Single B note |
| Trunk | `330+8` | E note with +8 Hz pitch bend |
| Alarm | `196,247,330,196` | Four-note riff |

- **Single note:** `247`
- **Multi-note:** `196,247,330,196` (up to 6 notes)
- **Pitch bend:** append `+8` to a note, e.g. `330+8`
- Frequencies must be between 50–2000 Hz

Harmonics and envelope settings still apply to every note in a riff.

### Receive mode

1. **Settings** → set the same carrier as the transmitter (315 or 433.92 MHz)
2. **Listen** — hold near transmitting Flipper
3. View identified button, decoded riff, and rolling code
4. **Sync Riff** → assign the last received riff to Unlock / Lock / Trunk / Alarm
5. Synced profiles saved to `/ext/apps_data/guitar_riff_rx/settings.txt`

## Desktop simulator (optional)

Test waveforms on your PC without transmitting:

```bash
python3 tools/guitar_key_fob.py unlock --plot
python3 tools/guitar_key_fob.py trunk --export /tmp/trunk_pluck.sub
```

The Python script mirrors the C waveform math and can export a RAW `.sub` file for comparison in the stock Sub-GHz app.

## How the RF signal works

1. **Envelope** — 10–15 ms linear attack, exponential decay (~400–700 ms configurable)
2. **Baseband** — `sin(f) + 0.5·sin(2f) + 0.25·sin(3f)` with optional vibrato/pitch bend
3. **ASK mapping** — sample at 2 kHz (500 µs); carrier **ON** when amplitude ≥ 15%, **OFF** otherwise
4. **Preamble** — short OOK sync + 16-bit rolling code, then 2 ms gap, then pluck
5. **TX** — `FuriHalSubGhzPresetOok650Async` preset via `subghz_devices_start_async_tx()`

## Legal & compatibility

- **Regulations:** Only transmit on frequencies allowed in your region. The app checks `furi_hal_subghz_is_frequency_valid()` and will refuse blocked bands.
- **Receivers:** Use **Guitar Riff RX** on a second Flipper, or build your own decoder for the guitar-pluck ASK format + rolling preamble.
- **Range / power:** Uses stock Flipper Sub-GHz power levels. Do not use to interfere with licensed services.

## Why C instead of MicroPython?

MicroPython ports for Flipper exist but do not expose reliable, low-latency Sub-GHz async TX. A **C FAP** using the official `subghz` SDK is the supported path for custom RAW transmission with GUI and settings.

## Troubleshooting

| Issue | Fix |
|-------|-----|
| `TX failed` | Enable **Sub-GHz debug / region** matching your band; try 433.92 MHz in EU |
| No speaker sound | Settings → Speaker → **On** |
| Build errors | Update uFBT SDK: `ufbt update` |
| App missing on Flipper | Ensure `guitar_key_fob.fap` is in `apps/Sub-GHz/` and firmware is up to date |

## License

MIT License. See [LICENSE](LICENSE) for details.
