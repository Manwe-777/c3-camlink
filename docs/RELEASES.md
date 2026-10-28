# Releases

Every release is published under
[Releases](https://github.com/Manwe-777/c3-camlink/releases), as two files:

| File | What it is | How it goes on |
|---|---|---|
| `c3-camlink-<version>-full.bin` | bootloader, partition table, OTA data and app, merged | flashed at offset `0x0` over USB |
| `c3-camlink-<version>-update.bin` | the app alone | installed by the settings page, over Wi-Fi |

They are not interchangeable. Feeding `-full.bin` to the updater writes a
bootloader into an app slot.

Recovering a module that will not boot needs no toolchain and no rebuild:

```bash
esptool.py --chip esp32c3 -p /dev/cu.usbmodemXXXX write_flash 0x0 c3-camlink-v1.0.0-beta1-full.bin
```

NVS sits at `0x9000` and is not covered by either image, so a rollback keeps the
bound camera and the saved settings. That is deliberate: a rollback is for
recovering from bad firmware, and it should not also cost the user their setup.

**Why the binaries are not in git.** Two artifacts per version, at a few MB
each, and git keeps every byte for ever — a cost every clone pays, against no
benefit to anyone who came for the source. `tools/release.sh` writes them into
`releases/` locally; they are attached to the tag instead.

**The `sdkconfig` snapshot.** `sdkconfig` is gitignored, and a build reproduced
from `sdkconfig.defaults` alone will not carry the same local options (bench
CLI, transmit power) — so the snapshot is what actually makes a release
reproducible. It is 74 KB per version, so it is attached to the tag alongside
the binaries rather than kept in the tree.

## Cutting one

Nobody cuts one by hand. Every push to `main` that passes the build workflow
runs [`release.yml`](../.github/workflows/release.yml), which uses
[semantic-release](https://semantic-release.gitbook.io/) to read the commit
types since the last tag (see [CONTRIBUTING.md](../CONTRIBUTING.md)):

- If none of them ships anything (only `docs`, `ci`, `chore` and the like),
  nothing happens.
- Otherwise it picks the next version, writes it to `VERSION`, regenerates
  `dist/index.html` so the tool's build label matches, commits both as
  `chore(release): x.y.z`, tags `vx.y.z` and opens the GitHub release with
  notes made from the commit summaries.
- It then builds the firmware from that tag with `tools/release.sh`, attaches
  the two images and the `sdkconfig` snapshot to the release, and redeploys the
  Pages tool.

The firmware reports the `VERSION` string about itself, the artifacts are named
from it, and `tools/build_web.py` stamps it into the browser tool's build
label, so all three agree by construction.

### Building the artifacts locally

```bash
tools/release.sh
```

It reads `VERSION`, builds from the tracked defaults into a build directory of
its own, and writes both artifacts plus the `sdkconfig` snapshot into
`releases/`. It never builds from the working `sdkconfig`: that file is
gitignored and holds whatever menuconfig was last used for, so a release built
from it would ship bench settings -- a debug transmit power, or the serial
console -- in an image nobody tested that way. This is the same script the
release workflow runs, so a local build of a tag matches what was published.

### Version numbers before 1.1.0

Up to `1.1.0-31` the number was bumped by hand on every firmware commit, and
there were no tags. `v1.1.0` marks the last of those commits; semantic-release
counts from there.
