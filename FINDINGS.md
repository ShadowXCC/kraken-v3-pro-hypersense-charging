# How the charging block works, and how it is lifted

Everything here was established on real hardware between 2026-09-29 and 2026-09-30: Razer Synapse
captures from a Windows VM, static analysis of the headset firmware in Ghidra, and commands sent
from Linux over the dongle's serial channel.

## The hardware

The Kraken V3 Pro is three processors, not one:

| Chip | Job |
|---|---|
| NXH3670 (headset) | 2.4 GHz radio, USB, **battery and charger management**, the Razer command protocol |
| STM32 (headset) | audio, **Lofelt haptics**, Chroma LEDs |
| NXH3670 (dongle) | the other end of the radio link; relays host commands |

The dongle presents USB audio, a media-key HID interface, and a **CDC-ACM serial port on
interfaces 4 and 5**. Every setting — lighting, brightness, HyperSense, sidetone, power saving —
travels over that serial port as Bluetooth HCI H4 packets with Razer payloads in vendor command
`0xFFF3`. None of it goes through HID feature reports.

## The block

Plug in the charge cable and haptics stop, along with the earcup LEDs. Things that are **not** the
cause, each ruled out by experiment:

- the charge source — a PC port and a wall charger behave identically, so the firmware reacts to
  cable power, not to a USB data connection;
- the host — Synapse sends exactly the same HyperSense frames while charging as on battery, and
  the headset **acknowledges them and does nothing**; there is no charging-specific exchange
  anywhere in the capture;
- any setting — NXP's `appconf` holds only radio and audio parameters, with nothing about charging.

Static analysis of the STM32 image (loaded at `0x08004000`, not `0x08000000`) found the mechanism.
The STM32 has two master gates:

| Gate | Address | Set by | Effect while set |
|---|---|---|---|
| Haptics | `0x200049ef` | command **`0x94` `switch_haptic`** | HyperSense sets and button presses are acked, then discarded |
| Chroma | `0x200049ce` | command **`0x93` `switch_chroma`** | lighting commands are acked, then discarded |

**The STM32 contains no charging logic at all.** The NXH3670 sends it `switch_haptic(0)` and
`switch_chroma(0)` when the charger is connected, over an internal chip link. That is the whole
block: a flag in RAM, set by a different processor.

(An early draft of this analysis blamed `0x200049f0`. That address is the HyperSense *level* index,
0–3 for 0/35/60/100 %, not the gate.)

## Lifting it

The dongle relays `0x93`/`0x94` from the host without filtering them, so the host can simply clear
the gate again:

```
switch_haptic(1)   01 f3 ff 18 02 21 40 94 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 6b
switch_haptic(0)   01 f3 ff 18 02 21 40 94 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 6c
switch_chroma(1)   01 f3 ff 18 02 21 40 93 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 6c
```

28 bytes: `01 f3 ff 18` is the H4 header for vendor command `0xFFF3`, `02 21 40` is the settings
prefix, byte 7 is the command, byte 8 is the argument, and the last byte makes
`sum(bytes 7..27) == 0 mod 256`. The STM32 reads the argument from struct byte 1, and struct byte
N is host frame byte N+7 — which is how the argument lands on byte 8.

Verified in two steps on 2026-09-30:

1. **On battery first**, where charging could not confound the result: `switch_haptic(0)` stopped
   the haptics, `switch_haptic(1)` brought them back. Both acked `status=0x00`. This proved the
   command, the byte mapping, and that the dongle relays it.
2. **Then with the cable in:** haptics stopped as always when the cable went in, `switch_haptic(1)`
   was sent, and **the haptics came back while charging**. No firmware modification anywhere.

## Why a service is needed

The gate is headset RAM, so anything that re-runs the charger logic restores it: going on charge,
a headset power cycle, and almost certainly a cable replug or a radio link drop.

Worse, **the re-assertion is invisible from the host.** The NXH3670 commands the STM32 over an
internal link that no host-side monitor can see; only the haptics themselves show it. So the
service cannot watch for the block coming back. It infers it from the events that cause it, which
is what [README.md](README.md#how-it-decides-when-to-send) describes.

## What is still open

- **Trigger 3 is an inference.** The 1 → 0 → 1 dip in `charge_status` after a headset power-cycle
  follows from the driver updating its cached battery state on every report, including the first
  one after a re-link, which carries state `0x00`. It has not been measured how long that dip
  lasts; if a 0.5 s poll turns out to miss it, `keepalive_seconds` is the fallback.
- **The 500 mA question.** Whether a USB 2.0 PC port can supply charging plus haptic peaks is
  untested. That current budget is exactly the "charging efficiency" reason Razer gave, and it is
  the one part of their reasoning that this work has not examined at all.
- **Heat.** Unmeasured over a full charge at HyperSense High. See the warning in the README.
