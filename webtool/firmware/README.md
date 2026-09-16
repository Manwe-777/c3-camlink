# Bundled firmware for the browser flasher

The browser tool (`../index.html`) can flash a bundled image with one click.
Both files below are build artifacts and are gitignored, so a fresh checkout —
and the hosted copy on GitHub Pages — does not carry them. The tool's "choose a
.bin file" option works without either, and configuring a module over USB needs
neither.

| File | Written at | Button | Settings |
|---|---|---|---|
| `c3-camlink-app.bin` | `0x20000` | Update firmware | kept — NVS is untouched |
| `c3-camlink-full.bin` | `0x0` | Full install | erased |

`-full.bin` is the merged image, and `esptool merge_bin` pads the gaps between
segments with `0xFF` — so flashing it at `0x0` writes over NVS at
`0x9000–0xf000` and takes the camera binding and the saved settings with it.
That is why there are two.

Generate both from a build:

```bash
idf.py build
cp build/c3-camlink.bin webtool/firmware/c3-camlink-app.bin
(cd build && esptool.py --chip esp32c3 merge_bin \
    -o ../webtool/firmware/c3-camlink-full.bin @flash_args)
```

Or copy a release pair (`c3-camlink-<ver>-update.bin` and
`c3-camlink-<ver>-full.bin`) here and rename them to the names in the table —
the release `-update.bin` is the same app-only image as `-app.bin`.
