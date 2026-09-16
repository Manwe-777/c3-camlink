#!/usr/bin/env bash
# Build the two artifacts a release needs. They are NOT interchangeable, and
# the names say so, because picking the wrong one is the obvious mistake:
#
#   -update.bin   the app alone. This is what the settings page installs.
#   -full.bin     bootloader + partition table + app, flashed at 0x0 over USB.
#                 First flash on a blank board, or recovery.
#
# Feeding -full.bin to the updater would write a bootloader into an app slot.
# The firmware rejects it (the image header check fails), but the names should
# make it unlikely in the first place.
set -euo pipefail
cd "$(dirname "$0")/.."
# Version comes from the VERSION file so the artifact names and the version the
# firmware reports about itself cannot disagree.
VER="v$(cat VERSION)"

# Build from the TRACKED defaults, in a build directory of its own -- never from
# the working sdkconfig.
#
# sdkconfig is gitignored and accumulates whatever menuconfig was last used
# for. Building a release from it ships bench settings -- a debug transmit
# power instead of the -24 dBm flight default, a switch channel that does not
# match the documentation -- in an image nobody tested that way. Nothing catches
# it either: the failure is a factory-fresh unit that is louder than it should
# be next to someone's receiver.
#
# Regenerating from sdkconfig.defaults makes the artifact identical to what a
# clean clone builds, which is the only version of "reproducible" that means
# anything to someone who did not write it.
BUILD_DIR="build-release"
SDKCFG="${BUILD_DIR}/sdkconfig.release"
rm -f "$SDKCFG"

idf.py -B "$BUILD_DIR" -D SDKCONFIG="$SDKCFG" build
mkdir -p releases

cp "${BUILD_DIR}/c3-camlink.bin" "releases/c3-camlink-${VER}-update.bin"
( cd "$BUILD_DIR" && esptool.py --chip esp32c3 merge_bin \
    -o "../releases/c3-camlink-${VER}-full.bin" @flash_args >/dev/null )
cp "$SDKCFG" "releases/sdkconfig-${VER}"

ls -l "releases/c3-camlink-${VER}"*
