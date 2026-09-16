# c3-camlink — roadmap

Priorities for what to build next, ordered by value per effort and by what can
be tested on the hardware on hand (GoPro HERO 2024 and HERO5 Black, ESP32-C3,
Betaflight FC). GoPro-side work is testable now; DJI is inherited and works;
Sony is untestable here and deferred.

Status: ✅ done · 🔨 in progress · ⏳ next · 💤 deferred

Versioning: one number in the `VERSION` file (currently 1.1.0-27). The firmware
reads it at configure time and the browser tool is stamped from it by
`tools/build_web.py`, so the two cannot disagree.

## Now

1. ✅ **Warnings** — the module's core promise: know it is recording while you
   can still do something. Surface the most important problem on the OSD, and
   flash the module LED, when:
   - armed / switched on but the camera is **not actually recording** (`REC?`)
   - camera too hot (`CAM HOT`)
   - no card or card full (`NO SD`)
   - card nearly full (`SD LOW`)
   - battery low (`BAT LOW`)
   - on a photo or timelapse preset, so the shutter would take stills (`PHOTO`)
   Uses data the module already has. Fixed thresholds to start; per-warning
   toggles and thresholds are a follow-up. `PHOTO` and `NO SD` have fired on
   the bench; the rest are reasoned, not yet seen -- see
   [HARDWARE_TESTING.md](HARDWARE_TESTING.md).

2. ✅ **HiLight tag on an AUX switch** — flip a switch mid-flight, the GoPro
   drops a HiLight marker. Small, delightful, GoPro-testable on both cameras.

## Next

3. ✅ **Auto power-on / wake the camera** — covered by design: the module
   connects to the bound camera on power-up, and connecting wakes a sleeping
   GoPro, so powering the quad brings the camera up. No separate toggle needed.

4. ✅ **Stop-delay (crash insurance)** — keep recording a configurable few
   seconds after the automatic stop; re-arming cancels it. Default 5 s.

5. ✅ **Resolution / FPS OSD tokens** — GoPro resolution and framerate as OSD
   fields, mapped from the camera settings with a raw-number fallback.

6. ✅ **Saved camera profiles** — remember more than one camera (the HERO 2024
   and the HERO5) and pick which to fly without re-pairing.

## Follow-ups to the above

- Per-warning enable toggles and thresholds in the settings UI (battery %, SD
  minutes), once the warnings engine (1) is in.
- Warnings config exposed over both the Wi-Fi server and the serial API.

## Deferred

- 💤 **GPS time sync** — needs GPS on the aircraft and the FC to report it over
  MSP. Revisit when flying with GPS.
- 💤 **DJI / Sony parity** — DJI already works; Sony is untestable without the
  hardware.
- 💤 **GoPro preset switching** — load presets from an AUX channel; more complex
  and lower priority than HiLight.
