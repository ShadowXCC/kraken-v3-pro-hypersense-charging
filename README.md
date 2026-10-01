# kraken-hypersense-charging

Keeps **HyperSense haptics working while the Razer Kraken V3 Pro is charging**, on Linux, with no
firmware modification.

Plug the charge cable into a Kraken V3 Pro and the haptics cut out, and the earcup lighting with
them. That is not a hardware interlock: the headset's radio chip tells its audio chip "haptics
off" when it sees the charger, and the audio chip then acknowledges every HyperSense command and
quietly ignores it. One command from the host clears that flag again, and the wireless dongle
relays it unfiltered.

The flag lives in RAM, so it comes back every time the headset goes on charge, power-cycles or
re-links. This daemon watches for those moments and sends the command again.

See [FINDINGS.md](FINDINGS.md) for how the gate was found and what exactly is known.

## Before you use this

Razer's stated reason for the restriction is charging efficiency, and the manual says not to wear
the headset while charging. **Those reasons have been bypassed here, not disproved.** So far this has had no adverse effects on the single tested headset.

- **Heat is the real risk.** The haptic drivers sit inside the earcups, and so does the battery
  while it charges. The headset's rated operating range is 0–40 °C.
- Before you rely on this, do one full charge with HyperSense on High while checking the earcup
  temperature by hand now and then. If it gets uncomfortably warm, stop.
- Whether a 500 mA USB 2.0 port can supply charging **and** haptic peaks is untested — the
  original work used a wall charger specifically to take that variable out. A powered port or
  charger is the safer choice.
- Everything is instantly reversible: `kraken-hypersense-charging once --off`, unplug the cable,
  or power-cycle the headset.

Use it on your own hardware at your own risk.

## What it needs

- A Kraken V3 Pro and its HyperSpeed dongle (`1532:052c`).
- Linux, a C compiler and `make`. No libraries beyond libc.
- **For the automatic part: an OpenRazer build with Kraken V3 Pro support**, which is where the
  `charge_status` attribute comes from. That support is not in upstream OpenRazer yet. Without it
  the daemon has no way to tell when the headset goes on charge, and will say so; `once` still
  works, because it can take the control interface from `cdc_acm` instead.
- Root. The driver's sysfs attributes are root-only, and moving the dongle's control interface
  between drivers means writing to `/sys/bus/usb/drivers/*/{bind,unbind}`.

## Build and install

```sh
git clone https://github.com/ShadowXCC/kraken-v3-pro-hypersense-charging.git
cd kraken-v3-pro-hypersense-charging
make
make check              # unit tests, no hardware needed
sudo make install       # /usr/local by default; PREFIX=/usr to change it
sudo systemctl daemon-reload
sudo systemctl enable --now kraken-hypersense-charging.service
```

Check what it can see:

```sh
sudo kraken-hypersense-charging status
```

```
dongle:            1532:052c at 1-3 (bus 001 dev 018), node /dev/bus/usb/001/018
control iface:     1-3:1.4 -> razerkraken_cdc
kraken sysfs:      0003:1532:052C.001D
charge_status:     1 (on the cable)
config:            chroma=yes poll=0.50s settle=2.00s min_interval=5.00s keepalive=off
```

Uninstall with `sudo make uninstall` (the config file in `/etc` is left alone).

## Usage

```
kraken-hypersense-charging [options] [daemon|once|status]

  daemon          watch for the charger and clear the gate when it appears (default)
  once            clear the gate now and exit
  status          show what the service can see, and exit

  -c, --config PATH   config file (default /etc/kraken-hypersense-charging.conf)
  -n, --dry-run       with 'once': print the frames, send nothing
      --off           with 'once': re-apply the block instead of clearing it
      --if-charging   with 'once': do nothing unless the headset is on the cable
      --no-chroma     do not touch the Chroma gate this run
  -v, --verbose       debug logging
  -q, --quiet         errors only
```

Logs go to the journal: `journalctl -u kraken-hypersense-charging -f`.

## Configuration

`/etc/kraken-hypersense-charging.conf`, documented inline. The settings are `chroma`,
`poll_seconds`, `settle_seconds`, `min_send_interval` and `keepalive_seconds`.

`keepalive_seconds` is **0 (off)** by default. Every known way the gate comes back also moves
`charge_status`, so the event triggers should be enough; set it (120 is reasonable) only if you
see haptics drop out with nothing in the log to explain it.

## How it decides when to send

It polls the driver's `charge_status`, which is a cached value inside the driver rather than a
USB round-trip, so polling it twice a second costs nothing on the bus. It sends when:

1. **`charge_status` goes 0 → 1** — the cable went in. This is the main case. The send waits
   `settle_seconds` first, because the headset asserts the gate itself when it sees the charger
   and an earlier send would simply be overwritten.
2. **It starts, or the dongle or driver is rebound** while the headset is already charging.
3. **`charge_status` dips 1 → 0 → 1** — what a headset power-cycle looks like with the cable
   still in, because the first battery report after a re-link carries state `0x00`.
4. **The machine resumes from suspend** — `CLOCK_BOOTTIME` keeps running while suspended and
   `CLOCK_MONOTONIC` does not, so the gap between them gives a resume away.

It never sends while the headset is on battery, and never twice inside `min_send_interval`.

## How it shares the dongle with OpenRazer

The OpenRazer driver owns the dongle's control channel, and only one thing can hold it at a time.
Rather than taking it away, this daemon **borrows it for about a second** per send:

1. unbind interface 4 from `razerkraken_cdc` (which releases interface 5 with it);
2. claim both through usbfs, send, read the acknowledgement;
3. bind interface 4 straight back, and check that the driver really took it.

The driver's probe path resets its own state and re-reads link, battery and lighting, so it comes
back by itself. Signals are blocked for the duration, so the daemon can never be killed with the
interface left driverless. If a hand-back ever did fail, it logs the exact command to repair it.

Polychromatic, `openrazer-daemon` and anything else keep working normally between borrows. A write
from the daemon that lands exactly during a borrow may fail with `ENODEV`; retry it.

## If something goes wrong

**`charge_status: unknown`** — the Kraken driver does not hold the control interface. Check
`status`: if `control iface` shows `cdc_acm` or `(no driver)` instead of `razerkraken_cdc`, the
driver is not installed, or udev has not handed the interface over. Replug the dongle.

**"the dongle stopped accepting data; replug it"** — the dongle's relay has jammed, which some
units do. Nothing more is sent until it is replugged, and the daemon picks up again by itself when
it is.

**"is left with no driver"** — the only serious failure mode, and the log line tells you the exact
command to fix it. It should not happen: the hand-back has a re-probe fallback.

**Haptics still off while charging** — check that HyperSense itself is switched on at the headset,
and that the log shows an acknowledged send. A send that is acked with a non-zero status usually
means the headset was off or the radio link was down.

## Credits

The protocol work behind this — the dongle's CDC transport, the frame format, and the firmware
analysis that found the two gate commands — came out of adding Kraken V3 Pro support to OpenRazer,
which in turn built on [paladin-devops's OpenRazer PR #2884](https://github.com/openrazer/openrazer/pull/2884)
for the first lighting frames.

## Licence

MIT. See [LICENSE](LICENSE).
