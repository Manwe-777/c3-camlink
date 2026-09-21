# Hardware testing — state and what is left

Bench session on Linux (Arch), 2026-09-07, against an ESP32-C3 Supermini over
USB and a **GoPro HERO (2024)**, MAC `CB:58:A0:4D:A4:A2`. Firmware `1.1.0-25`;
continued on macOS the same day, firmware `1.1.0-26` → `1.1.0-27`.

The point of this file is that the next session does not repeat what is already
settled, and does the open items in an order where each one is testable.

---

## Verified on hardware — do not redo

| Area | Evidence |
|---|---|
| `-21` shared BLE transport | `BONDING OK`, MTU 500, all three channels subscribed (`notify=0x40/0x45/0x4a`), `GOPRO: session up (GoPro HERO (2024))` |
| Model + firmware readback | `camera model "HERO" -> label HERO`, `camera firmware H24.03.02.20.00` |
| Status registration | accepted, 33 bytes of current values for the eight registered ids |
| Bind from a scan | `bind mac=CB58A04DA4A2` → `bound:true`; profile `GPRO` auto-created |
| Video recording | `rec on` → `encoding=1`, clip counts 3s→19s, `left` 7030s→7009s; `rec off` stops cleanly |
| `-22` does not over-refuse | on a video preset `group=1000 valid=1`, shutter allowed, films normally |
| Status ids 6/8/10/13/33/35/70 | all report live and correct (busy, encoding, clip, SD, left, battery) |
| Flashing over USB | `esptool write_flash 0x20000 build/c3-camlink.bin` — app only, NVS survives |

Serial access on Linux needed `/etc/udev/rules.d/99-esp32-c3.rules`:

```
SUBSYSTEM=="tty", ATTRS{idVendor}=="303a", ATTRS{idProduct}=="1001", MODE="0666"
```

`TAG+="uaccess"` does **not** work at `99-`; `73-seat-late.rules` runs the
`uaccess` builtin at 73, so a tag set later is never acted on. Number it below
73 or use `MODE`.

---

## Open items, in the order to do them

### 1. Webtool over USB — SETTLED: works on macOS, Linux-only fault

Bench on macOS, 2026-09-07, same board at `/dev/cu.usbmodem101`, page served
no-cache on `localhost:8795`, firmware `1.1.0-26`: Connect over USB opens the
port, `api mode` answers `{"usbcfg":true}`, state/config populate, the bound
camera shows as `CB:58:A0:4D:A4:A2`. So the "device has been lost" on the first
`reader.read()` is Linux-specific and the page was not the cause. The two
Linux candidates below are still unseparated (a DTR drop on close via `hupcl`
making the C3 re-enumerate, or Chrome handing back a stale `SerialPort` from
`getPorts()`); nobody on a Mac can chase them.

A third lead, found by accident on the Mac: with a terminal serial monitor
still holding the port, the tool fails with exactly the Linux signature —
`port.open()` resolves, the first read reports `The device has been lost`.
Stopping the other process fixed it at once. On Arch, check for ModemManager
or a leftover `screen`/`minicom`/`idf.py monitor` on the device before
chasing the two causes above.

For the record, what Linux showed:

```
port.open({baudRate:115200})            -> resolves,      1ms
reader.read()                           -> NetworkError: The device has been lost.   2ms
writer.write('api 1 state\n')           -> no error, goes nowhere
reply                                    -> none, 4s budget expires
```

### 2. Webtool silent failures — FIXED in the page (same day)

What changed in `dist/index.html`, and why:

- **`Board._read()` no longer swallows the error.** A deliberate `disconnect()`
  sets `closing` and is not a fault; any other end of the read loop is a lost
  link: it is logged to the console, stored in `board.lastError`, every pending
  `api()` promise is rejected with it (instead of each waiting out its own 4s
  timeout), and `board.onlost` puts the connect gate back with the reason in
  the red banner.
- **`ensureUsbcfg()` tells a timeout from old firmware.** Only an explicit
  `unknown-verb` reply from the module counts as "old firmware, assume ready".
  A timeout or a lost link now fails the connect with the reason, instead of
  reporting success on a dead link.
- **`api()` fails fast when there is no port or writer**, and a rejected
  `writer.write()` rejects the call instead of leaving it to the timeout.
- **The Disconnect badge never showed, on any platform.** `#usbConn` is
  `display:none` in the stylesheet, and the code "showed" it by clearing the
  inline style to `''`, which just falls back to the stylesheet. So the badge
  was hidden the moment the 800ms interval ran, whether or not the link was
  alive. It is now set to `'block'` explicitly. This is what the Linux session
  read as "the Disconnect button vanished".

Verified on the Mac, same day: cable pulled mid-session three times, each
time the banner read `USB link lost: The device has been lost`, the badge
went, the connect gate came back, and the console carried the error. Replug
and Connect reconnects without a page reload.

### 3. `-22` shutter guard — FIXED: status 97, not 96 (1.1.0-27)

The measurement the Linux session could not get, taken on the Mac the same day
with ALL statuses registered (`gp qry 01 53` does it directly; no need to
provoke the refusal) and the dial turned photo → video → photo → video → photo:

```
status 97 = 65536     photo   (0x10000)
status 97 = 5         video
status 97 = 0         video
status 97 = 3         video
status 97 = 0         video
status 97 = 5         video
status 97 = 65536     photo
```

Status **96 never pushed once** — it sits at 1000 (VIDEO) whatever the dial
says. Status **97** is the active preset id, and Open GoPro numbers presets as
`<group> << 16 | <index>`: video `0x0000xxxx`, photo `0x0001xxxx`, timelapse
`0x0002xxxx`. (Status 89 also moved, 12 ↔ 16, but 97 is the documented one.)

Registered one at a time: **97 accepted, 96 accepted, 43 refused (status 1)**.
So 43 was the id that sank the Linux session's batch, and it must never be in
the registration list for this camera.

The fix: `gopro_preset_group_from_id()` maps 97 onto the same 1000-series the
guard already speaks, 97 is in the registration batch (39 bytes of current
values now, was 33), and 96 only counts until 97 has spoken. Host test
`test_preset.c` §5 runs the mapping on the recorded values. Verified:

```
gopro preset: group=1001 valid=1 (from status 97, id=0x10000) -> STILLS, shutter refused
rec on -> W GOPRO: NOT starting: the camera is in a photo or timelapse preset...
          left stayed 7008s: NO photo taken (the owner confirmed)
          !! warning: PHOTO   <- the PHOTO rung of the -23 ladder, first time it fired
dial to video:
gopro preset: group=1000 valid=1 (from status 97, id=0x5) -> films, shutter allowed
rec on -> encoding=1, clip 3s -> 14s; rec off -> shutter OFF, idle, left 7008 -> 6993s
```

#### 3b. Found while verifying: the refused request could not be cancelled

Between the two halves above, `rec off` printed `record: off` but the module
kept re-issuing the refused start every 3 s, the latch read `on` again, and the
moment the dial reached video **the camera started recording by itself** —
nobody had asked, and the OSD had said `REC?` the whole time.

Two causes, both in the override, both fixed in 1.1.0-27:

- `unsent` in `camlink.c` was "the camera disagrees with the intent, within
  3 s of the last send". With a retry every 3 s and a camera that refuses,
  that is always, so every press was absorbed. It now means what its comment
  says: the intent differs from what was last SENT while the rate limiter
  holds it.
- A press inverted "what the camera is doing", which for a refused start is
  idle, so the press re-asked for the start it was trying to cancel. Past the
  `CAMLINK_NOT_REC_GRACE_MS` grace — when the OSD is already showing the
  refusal — a press now inverts the intent instead. Inside the grace the old
  behaviour stands, so an impatient double press still does one thing.

`test_override.c` §12 and §13 pin both, including the armed case: a pilot
whose camera refuses can press to hold IDLE, and disarming releases it.

Verified on hardware, 1.1.0-27, dial on photo: `rec on` → three refused
retries over 6 s; `rec off` → `record: off`, no further retry in the next
10 s, `latch=off`, OSD `IDLE`. Dial to video → `status 97 = 5`, and 19 s
later still `rec=idle`, no shutter command, `PHOTO` warning cleared.

### 4. `-23` warning ladder — two rungs fired on hardware, four cannot on this bench

Exercised on 1.1.0-27 with the HERO (2024):

- **PHOTO** — fired the moment the guard in item 3 started reading status 97
  with the dial on photo: `!! warning: PHOTO`, OSD row 4 `PHOTO`. Cleared
  when the dial moved to video.
- **NO SD** — card pulled with the camera on: `status 33 = 2` (removed),
  `34/35/38/39 = 0`, then `!! warning: NO SD`, OSD row 3 `SD 00:00`, row 4
  `NO SD`. The camera also dropped itself onto a photo preset when the card
  went (`97 = 0x10000`), and the ladder ranked NO SD above STILLS as written.
  Card back in: `33 = 0`, `35 = 7008`, row 4 blank, the camera returned to
  its video preset (`97 = 5`) by itself.

Not reachable here, and why:

- **HOT** needs `temp_over >= 2` from a camera that is actually overheating.
- **BATT LOW** needs the camera at or below 15 %; it sat at 99–100 % all day.
- **SD LOW** needs under 120 s left on the card; this one has 1 h 56.
- **REC?** needs a start that goes unmet past the grace with nothing the
  ladder ranks higher. On this bench every unmet start was a photo preset,
  which STILLS outranks. It is reachable in flight: armed, camera silent.

The LED half of `-23` was not watched; the OSD rows above are the evidence.

### 5. TX power failure on every connect — FIXED (1.1.0-27)

```
W BLE: TX power CONN_HDL0 -> -24 dBm failed: ESP_FAIL
```

Cause, from the C3 controller source (`bt.c`, `esp_ble_tx_power_set`): for
the `CONN_HDLn` power types the type value IS the HCI connection handle it
passes down. So `CONN_HDL0` means "the link with handle 0", and on this part
the first link gets handle **1** (`Connected, conn_id=0 hci_handle=1`). The
call could never succeed. Not related to the enum-spacing patch in `usbcli.c`.

Fix: the GATTC connect event carries `conn_handle`; the transport keeps it
and sets the connection power through `esp_ble_tx_power_set_enhanced()` with
that handle, then reads it back. Verified:

```
I BLE: Connected, conn_id=0 hci_handle=1
I BLE: TX power CONN (handle 1) set to -12 dBm, reads back -12 dBm
tx -24 -> radio -24 dBm until reboot   (no warning; the live set now lands)
tx -12 -> radio -12 dBm until reboot
```

The arm-time drop to -24 dBm therefore reaches the live link now. Nobody has
measured the RF effect on an ELRS receiver; that remains a flight-line test.

### 6. 16-bit status family — IMPLEMENTED (1.1.0-27), untested for lack of a MISSION 1

How it works: `cam_gopro_wide_ids(model)` picks the family to try first, the
registration goes out as `0x56` with `[id hi][id lo]` when wide, replies and
pushes on `0x56`/`0x96` are walked with 2-byte ids, the keep-alive uses the
`0xFF` marker form and flips (at most twice) when the settings replies refuse
it or never come, and the registration ladder is: our list in the model's
family → our list in the other family → ALL statuses in the model's family.
`st` shows `ids=1-byte` or `ids=2-byte`.

The walker and the setting-reply parser are pure (`gopro_status_next()`,
`gopro_setting_reply()` in `gopro_cam.h`) and pinned by
`test/host/test_gopro_status.c` on hand-built frames of both families, a
truncated element, and a 1-byte frame misread as wide (must not run off the
end). On the HERO (2024) nothing changed: `status registration accepted
(1-byte ids), 39 bytes of current values`. The 2-byte path has never met a
camera.

### 7. Flight controller and OSD — never connected

`fc: link=down armed=no` for the entire session; there was no flight controller
on the bench. OSD rows render (`IDLE`, battery, `SD 1H57`) and `osd_writes`
climbs, but nothing was ever drawn to a real FC, and no arming path was
exercised end to end. Everything about arming behaviour remains bench-only.

---

## Notes for whoever picks this up

- **`-full.bin` erases your settings.** `esptool merge_bin` pads the gaps
  between segments with `0xFF`, so flashing it at `0x0` writes over NVS at
  `0x9000–0xf000`. Reasoning from the segment offsets says otherwise and is
  wrong. Use `write_flash 0x20000 build/c3-camlink.bin` to keep the binding.
- **`VERSION` is read at configure time.** A build made before a `VERSION` edit
  is stamped with the old version, so a freshly flashed module reports the
  previous one. Rebuild after bumping.
- **The webtool's `build` label must never be a hardcoded literal.** It is
  generated by `tools/build_web.py`, which stamps `@VERSION@` from the `VERSION`
  file, so the label cannot disagree with the firmware.
- The GoPro drains about **1%/minute** while connected over BLE. It went 78% →
  40% across this session; charge it before a long bench run.
