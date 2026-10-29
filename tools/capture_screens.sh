#!/usr/bin/env bash
# Drive the board through the system screens and the sample app (tools/sample-app) with
# `fenix tap`/`swipe` and save a PNG of each with `fenix shot`. Starts and ends on Home (landscape, Tiles). Needs the board on the link.
# Coordinates follow the layouts in board/components/fenix_apps.
# Usage: tools/capture_screens.sh <out-dir>   (erases the board's settings first)
set -e
OUT=${1:-shots}
F=${FENIX:-target/debug/fenix.exe}
mkdir -p "$OUT"
shot() { "$F" shot "$OUT/$1.png" 2 >/dev/null; echo "$1"; }
tap() { "$F" tap "$1" "$2" >/dev/null; sleep 0.6; }
# Swipes start in the right margin, so they scroll the page instead of moving a slider.
up() { "$F" swipe 314 "$1" 314 "$2" >/dev/null; sleep 0.6; }
back() { tap 30 16; }

# Known start: erase the settings partition (nvs, see board/partitions.csv) so the board
# boots with defaults (landscape, Tiles), then wait for the boot screen to finish.
ESPTOOL_PY=${ESPTOOL_PY:-/c/Espressif/tools/python/v5.5.5/venv/Scripts/python.exe}
"$ESPTOOL_PY" -m esptool --port "${PORT:-COM9}" erase_region 0x9000 0x6000 >/dev/null
sleep 4
"$F" time >/dev/null
# The sample app gives Home a fourth tile and the app screens below.
PKG=${PKG:-target/sample.fxp}
SAMPLE=io.github.tariqaq.sample
"${FENIX_PKG:-target/debug/fenix-pkg.exe}" build tools/sample-app/board -o "$PKG" >/dev/null
"$F" install "$PKG" >/dev/null
sleep 1
shot home
# Landscape: Settings tree
tap 58 200; shot settings
tap 160 60; shot display
up 200 100; shot display_scrolled
tap 160 155; shot rotation
tap 160 101                          # Portrait
shot rotation_portrait
back; back; back; shot home_portrait
tap 62 206; shot settings_portrait
tap 120 60; shot display_portrait
tap 120 255                          # Rotation
tap 120 60                           # Landscape
back; back
tap 160 101; shot home_style
tap 230 100; shot home_style_dock    # Dock
back; back; shot home_dock
tap 52 185                           # Settings (dock of 4: system apps + sample)
tap 160 101; tap 90 100              # Tiles
back
up 220 40; tap 160 208; shot about     # scrolled to the end
back
tap 160 142; shot storage
back
tap 160 183; shot host
back; back
tap 158 200; shot session
tap 160 226; sleep 0.3               # Cancel
tap 258 200; shot stream
back
# Host app (landscape): scroll the tiles, open the sample, set values like its PC half would.
"$F" swipe 314 200 40 200 >/dev/null; sleep 0.8
tap 254 200
"$F" set "$PKG" cpu=42 "cpu_text=42 %" mem=63 "mem_text=5.1 GB" on=true level=80 >/dev/null
shot app
"$F" host "$SAMPLE" off >/dev/null; shot app_host_off
"$F" host "$SAMPLE" on >/dev/null
back
"$F" remove "$SAMPLE" >/dev/null; sleep 0.6
