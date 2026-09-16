# The browser front-ends

There are two of them, and they are the same page:

| Output | What it is |
|---|---|
| `components/webcfg/web/index.html` | the settings page the module serves over its own Wi-Fi, gzipped into the firmware image at build time |
| `webtool/index.html` | that same page driven over a USB cable instead, served from GitHub Pages |

**Both are generated. Do not edit either one.** Edit the sources here and run:

```bash
tools/build_web.py
```

## Why it works this way

Maintained by hand — one file copied from the other — the two drift, and the
drift is invisible: nobody diffs them against each other, so a stale copy shows
up as a feature that works over Wi-Fi and silently does nothing over USB. A
missing `/api/profile` in one endpoint map is enough to break switching between
saved cameras on one transport only.

Generating both from one source is what makes that class of bug impossible
rather than merely unlikely.

## Layout

```
web/
  device.html.in    template for the on-device settings page
  usb.html.in       template for the USB browser tool
  shared/
    app.css         the design system, one copy
    app.html        header, the eight sections, footer, toast
    app.js          the settings logic: polling, form, camera list, OSD editor
  usb/
    gate.css        connect-gate and connection-badge styling
    gate.html       the connect gate and the flash panel
    transport.js    Web Serial link, and the fetch('/api/...') shim
    flash.js        the esptool-js flashing module
```

A template is plain HTML with include lines in it:

```html
<!-- include: shared/app.css -->
```

The generator replaces each one with that file, verbatim, and substitutes
`@VERSION@` from the `VERSION` file — which is why the tool's build label no
longer has to be bumped by hand.

## The transport seam

`shared/app.js` never speaks to a transport directly. It calls
`fetch('/api/...')` and nothing else, which on the device is a real HTTP request
to `esp_http_server`, and in the browser tool is intercepted by
`usb/transport.js` and turned into a line on the serial link. The firmware
answers both from the same builders (`webcfg_state_json()`,
`webcfg_apply_config()` and friends in `components/webcfg/`), so the data
contract is shared too.

Two consequences worth knowing:

- **Every endpoint the shared page can reach has to be in the map** in
  `usb/transport.js`. One that is missing works over Wi-Fi and fails over USB,
  which is exactly the bug described above — so `tools/build_web.py` refuses to
  run when the map does not cover every `/api/` call in `shared/app.js`.
- **The shared page must not assume a transport.** Where the USB tool needs to
  react to module state, it defines `window.camlinkOnState`, which the shared
  page calls if it exists and ignores if it does not.

## The outputs are committed

They are checked in rather than built on demand, so that neither consumer needs
Python: the firmware build only has to gzip a file that is already there, and
GitHub Pages only has to serve one.

The cost is that a committed output can go stale against the sources. CI runs
`tools/build_web.py --check`, which fails the build when it has.
