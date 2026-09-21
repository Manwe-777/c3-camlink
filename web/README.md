# The browser front-ends

There are two of them, and they are the same page:

| Output | What it is |
|---|---|
| `components/webcfg/web/index.html` | the settings page the module serves over its own Wi-Fi, gzipped into the firmware image at build time |
| `dist/index.html` | that same page driven over a USB cable instead, served from GitHub Pages |

**Both are generated. Do not edit either one.** Edit the sources here and run:

```bash
npm install      # once
npm run build    # tsc, then tools/build_web.py
```

`npm run check` is what CI runs: typecheck, lint, and a rebuild that fails if
either committed page has drifted from these sources.

Node is needed ONLY to edit the page. The firmware build and the Pages deploy
both consume the committed outputs, so neither has ever heard of npm.

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
    app.ts          the settings logic: polling, form, camera list, OSD editor
  usb/
    gate.css        connect-gate and connection-badge styling
    gate.html       the connect gate and the flash panel
    transport.ts    Web Serial link, and the fetch('/api/...') shim
    flash.ts        the esptool-js flashing module
  types/
    api.d.ts        the shapes the firmware's JSON actually has
    esptool.d.ts    the slice of esptool-js the flash panel uses
  .build/           tsc output -- gitignored, an intermediate, never edited
```

## The logic is TypeScript

`app.ts` and `transport.ts` compile to `.build/` and are inlined from there.
Neither is an ES module: they go into a bare `<script>` tag, so they must not
emit `import`. That is why the shared types live in `types/api.d.ts` with no
top-level `import` or `export` -- a file like that is ambient, and its
declarations are global. `flash.ts` is the exception and IS a module, because
esptool-js is loaded by URL from a CDN.

`tsc` targets es2022 and does not minify. Type annotations are erased, so the
emit costs no runtime bytes; gzipped, the on-device page moved by well under a
kilobyte, against a 1.9 MB OTA slot.

### What the types are actually for

`Endpoint` in `types/api.d.ts` is a closed union of every `/api/` name the
shared page may reach, and `transport.ts` types its verb table as
`Record<Endpoint, string>`. A name in one and not the other no longer
compiles.

That replaces a hand-written checker in `tools/build_web.py` that regexed both
files and diffed the results. The bug it guarded against is the one below --
worth keeping in mind, because it is the reason any of this exists.

A template is plain HTML with include lines in it:

```html
<!-- include: shared/app.css -->
```

The generator replaces each one with that file, verbatim, and substitutes
`@VERSION@` from the `VERSION` file, so the tool's build label cannot disagree
with the firmware.

An include ending in `.js` is resolved from `.build/`, not from the source
tree: the template names the file the browser ends up running, and only
`build_web.py` knows it was compiled from TypeScript to get there.

## The transport seam

`shared/app.ts` never speaks to a transport directly. It calls `api(ep)`,
which is a `fetch('/api/' + ep)` and nothing else, which on the device is a real HTTP request
to `esp_http_server`, and in the browser tool is intercepted by
`usb/transport.ts` and turned into a line on the serial link. The firmware
answers both from the same builders (`webcfg_state_json()`,
`webcfg_apply_config()` and friends in `components/webcfg/`), so the data
contract is shared too.

Two consequences worth knowing:

- **Every endpoint the shared page can reach has to be in the map** in
  `usb/transport.ts`. One that is missing works over Wi-Fi and fails over USB,
  which is exactly the bug described above — so the map is typed
  `Record<Endpoint, string>` and a gap is a compile error.
- **The shared page must not assume a transport.** Where the USB tool needs to
  react to module state, it defines `window.camlinkOnState`, which the shared
  page calls if it exists and ignores if it does not.

## The outputs are committed

They are checked in rather than built on demand, so that neither consumer needs
Python: the firmware build only has to gzip a file that is already there, and
GitHub Pages only has to serve one.

The cost is that a committed output can go stale against the sources. CI
compiles and regenerates, then fails the build if either page moved.
