#!/usr/bin/env bash
# Host-side tests. No hardware, no ESP-IDF required.
#
# Several of these compile the ACTUAL firmware source rather than a copy of it
# (osd_format.c, url_decode.c), which is the point: a test against a
# re-implementation of the logic passes while the shipped version is broken.
set -euo pipefail
cd "$(dirname "$0")"

CFLAGS=(-Wall -Wextra -O1)

# One row per suite: name | include dir | extra sources
#
# Adding a suite is ONE line here, on purpose. Split across a compile list and
# a separate run list, a suite added to only one of them either never builds or
# builds and never runs -- and the second looks exactly like a passing run.
SUITES=(
  "test_msp_frames    | .                              |"
  "test_duml          | .                              |"
  "test_osd_format    | ../../components/camlink/include | ../../components/camlink/osd_format.c"
  "test_osd_layout    | ../../components/camlink/include | ../../components/camlink/osd_format.c"
  "test_warnings      | ../../components/camlink/include | ../../components/camlink/osd_format.c"
  "test_camvendor     | ../../components/camvendor/include |"
  "test_preset        | ../../components/gopro/include  |"
  "test_gopro_status  | ../../components/gopro/include  |"
  "test_switch_window | ../../components/camlink/include |"
  "test_url_decode    | ../../components/webcfg/include | ../../components/webcfg/url_decode.c"
  "test_json_buf      | ../../components/webcfg/include | ../../components/webcfg/json_buf.c"
  "test_press         | ../../components/camlink/include |"
  "test_override      | ../../components/camlink/include |"
  "test_setup_guard   | ../../components/camlink/include |"
)

trim() { echo "$1" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//'; }

echo "=== building ==="
for row in "${SUITES[@]}"; do
    IFS='|' read -r name inc extra <<<"$row"
    name=$(trim "$name"); inc=$(trim "$inc"); extra=$(trim "$extra")
    # shellcheck disable=SC2086
    cc "${CFLAGS[@]}" -I"$inc" -o "$name" "$name.c" $extra
done

echo
fail=0
for row in "${SUITES[@]}"; do
    IFS='|' read -r name _ _ <<<"$row"
    name=$(trim "$name")
    if ! "./$name"; then
        echo "!!! $name FAILED"
        fail=1
    fi
    echo
done

if [ "$fail" -ne 0 ]; then
    echo "some suites failed"
    exit 1
fi
echo "all ${#SUITES[@]} suites passed"
