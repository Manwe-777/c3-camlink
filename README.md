# c3-camlink

**Arm the quad. The camera rolls.**

[![Version](https://img.shields.io/github/v/release/Manwe-777/c3-camlink?style=flat-square&label=version&color=0F8A4A)](https://github.com/Manwe-777/c3-camlink/releases)
[![Build](https://img.shields.io/github/actions/workflow/status/Manwe-777/c3-camlink/ci.yml?branch=main&style=flat-square&label=build)](https://github.com/Manwe-777/c3-camlink/actions/workflows/ci.yml)
[![Hardware](https://img.shields.io/badge/hardware-ESP32--C3-5B636E?style=flat-square)](#what-you-need)
[![License](https://img.shields.io/badge/license-GPL--3.0-14171C?style=flat-square)](LICENSE)

An ESP32-C3 module that starts your action camera recording the moment you arm
and stops when you disarm. The camera's own state — recording, clip time,
battery, card remaining — is drawn onto your Betaflight OSD, so you know it is
rolling before you leave the ground.

It also warns you when something is wrong while you can still do something
about it: armed but not actually recording, no card, card nearly full, camera
too hot, battery low, or sitting on a photo preset where the shutter would take
stills instead of video.

Setup is from a phone over Wi-Fi, or from a browser over the USB cable. After
the first flash you never need a toolchain again.

## What you need

| | |
|---|---|
| **Module** | An ESP32-C3 Supermini. The only board tested. |
| **Camera** | **GoPro** (HERO 2024 and HERO5 Black verified) or **DJI Osmo** — Nano, 360, or Action. |
| **Flight controller** | Betaflight **2025.12 or newer**, with one spare UART. Hard requirement: older firmware cannot draw the OSD text. |
| **A phone or a USB-C cable** | For setup. Either works. |

The module draws about 80 mA from the flight controller's 5 V rail.

> **Antenna.** The board's printed antenna has to stick out past the frame.
> Carbon fibre blocks 2.4 GHz completely, and no setting compensates for an
> antenna sealed inside a carbon box.

## Wiring

Four wires to a spare UART:

| Module | Flight controller |
|---|---|
| `5V` | `5V` |
| `GND` | `GND` |
| `GPIO4` | UART **RX** |
| `GPIO5` | UART **TX** |

Transmit goes to receive, and the grounds must be common. In Betaflight's Ports
tab no peripheral is needed on that UART — just leave it free.

## Installing

Grab the latest release and flash `c3-camlink-<version>-full.bin` at offset
`0x0`, or open `dist/index.html` in Chrome or Edge and let it do the flashing
over USB. After that, updates install themselves from the settings page.

See [docs/RELEASES.md](docs/RELEASES.md) for what the two artifacts are, why
they are not interchangeable, and how to recover a module that will not boot.

## Building

```bash
idf.py set-target esp32c3     # optional; sdkconfig.defaults already pins it
idf.py build
```

ESP-IDF v5.5. The host tests need no toolchain and no hardware:

```bash
./test/host/run_tests.sh
```

Editing the browser front-ends additionally needs Node:

```bash
cd web && npm install && npm run build
```

## The tree

```
main/                 startup, and the three FreeRTOS tasks
components/
  camlink/            record state machine, warning ladder, OSD rendering
  msp/                MSP client for the flight controller
  camvendor/          which vendor a camera is, from its advertisement
  duml/ gopro/        the two camera sessions
  dji_link/           vendored DJI R SDK protocol stack (MIT)
  board/ ui/          per-part radio facts; button and LED
  webcfg/ usbcli/     the Wi-Fi settings server and the USB serial API
web/                  sources for both browser front-ends -- see web/README.md
dist/                 the USB browser tool, GENERATED from web/, published to Pages
test/host/            tests that run on a laptop
tools/                release and codegen scripts
docs/                 roadmap, hardware testing notes, release process
```

Three things worth knowing before editing:

- **`components/webcfg/web/index.html` and `dist/index.html` are
  generated.** Edit the TypeScript and markup under [`web/`](web/README.md)
  and run `npm run build` there. CI fails if the committed outputs are stale.
  Node is a contributor-only dependency: building the firmware never needs it.
- **Commit messages follow Conventional Commits** (`fix(osd): ...`), because
  the release workflow computes the version from them. See
  [CONTRIBUTING.md](CONTRIBUTING.md), and run
  `git config core.hooksPath .githooks` once to check them locally.
- **`components/dji_link/` is vendored.** Local changes are tagged in-line with
  `C3CAMLINK PATCH` or `C3CAMLINK ADDITION` so the derived work stays separable
  from DJI's original.

## Documentation

- [docs/ROADMAP.md](docs/ROADMAP.md) — what is next, and what is deferred
- [docs/HARDWARE_TESTING.md](docs/HARDWARE_TESTING.md) — what has been tested on real hardware, and what has only been reasoned about
- [docs/RELEASES.md](docs/RELEASES.md) — artifacts, recovery, how releases are cut
- [CONTRIBUTING.md](CONTRIBUTING.md) — the commit message format
- [web/README.md](web/README.md) — how the two browser front-ends are generated

## Licence

GPL-3.0-or-later — see [LICENSE](LICENSE).

One directory is not ours: `components/dji_link/` is vendored from DJI's own
published demo and stays under the MIT it arrived with, which combines with the
GPL without trouble. [NOTICE](NOTICE) records where it came from and the terms
that travel with it.
