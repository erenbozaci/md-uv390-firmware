#!/usr/bin/env bash
# Build / flash / screenshot helper for the UV-MD390 Plus (MDUV380, 10W variant).
#
#   ./uv390.sh build          clean build (container)
#   ./uv390.sh flash          flash build/OpenGD77_MDUV380_UV380_PLUS_10W.bin (radio must be in DFU mode)
#   ./uv390.sh all            build, wait for the radio in DFU mode, flash, then screenshot
#   ./uv390.sh shot [file]    screenshot of the running radio (default: screen.png)
#   ./uv390.sh size           flash/RAM usage of the last build (RAM is almost full!)
#
# DFU mode: power off, hold SK1, power on. Python tools live in ./.venv (created on first use).

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FW="$REPO/MDUV380_firmware"
PLATFORM="${PLATFORM:-MDUV380}"
VARIANT="${VARIANT:-UV380_PLUS_10W}"
BIN="$FW/build/OpenGD77_${PLATFORM}_${VARIANT}.bin"
MODEL="${MODEL:-MD-UV380}"
DONOR="${DONOR:-$REPO/donor/MD9600-CSV(2571V5)-V26.45.bin}"
VENV="$REPO/.venv"
LOADER="$FW/tools/opengd77_stm32_firmware_loader.py"
INI="$HOME/.gd77firmwareloader.ini"

die() { echo "ERROR: $*" >&2; exit 1; }

engine() {
    if [ -n "${CONTAINER_ENGINE:-}" ]; then echo "$CONTAINER_ENGINE"
    elif command -v docker >/dev/null 2>&1; then echo docker
    elif command -v podman >/dev/null 2>&1; then echo podman
    else die "neither docker nor podman found"; fi
}

venv() {
    if [ ! -x "$VENV/bin/python" ]; then
        echo "Creating $VENV (pyusb, pyserial, pillow)..."
        python3 -m venv "$VENV"
        "$VENV/bin/pip" install -q "pyusb>=1.2" pyserial pillow
    fi
}

dfu_present() { lsusb 2>/dev/null | grep -qi "0483:df11"; }

do_build() {
    local eng; eng="$(engine)"
    echo "== build $PLATFORM $VARIANT ($eng) =="
    ( cd "$FW" && make rebuild CONTAINER_ENGINE="$eng" PLATFORM="$PLATFORM" VARIANT="$VARIANT" 2>&1 \
        | grep -E "error|warning: |CLEAN|overflowed|undefined" | grep -v "#warning\|Wcpp" || true )
    [ -f "$BIN" ] || die "build failed, no $BIN"
    echo "built: $BIN"
    sha256sum "$BIN"
}

do_size() {
    local eng; eng="$(engine)"
    [ -f "$FW/build/MDUV380_FW.elf" ] || die "no build yet"
    "$eng" run --rm -v "$REPO:/src:Z" -w /src/MDUV380_firmware opengd77-mduv380-builder:latest \
        arm-none-eabi-size build/MDUV380_FW.elf
    echo "(RAM = data + bss: keep it a few hundred bytes below the limit, see CLAUDE.md)"
}

register_donor() {
    # The loader only accepts one exact official file; without it the radio ends up FM only.
    if grep -q "sourcestm32firmware *= *.\+" "$INI" 2>/dev/null; then return; fi
    [ -f "$DONOR" ] || die "codec donor not found: $DONOR (set DONOR=...). Flashing without it gives an FM-only radio."
    "$VENV/bin/python" "$LOADER" -s "$DONOR" | grep -E "codec source|!!!" || true
    grep -q "sourcestm32firmware *= *.\+" "$INI" 2>/dev/null || die "donor file was rejected by the loader (wrong hash)"
}

wait_dfu() {
    local timeout="${1:-120}"
    if dfu_present; then return; fi
    echo "Put the radio in DFU mode (power off, hold SK1, power on). Waiting up to ${timeout}s..."
    for _ in $(seq 1 "$timeout"); do
        dfu_present && return
        sleep 1
    done
    die "no radio in DFU mode"
}

do_flash() {
    [ -f "$BIN" ] || die "no $BIN, run: $0 build"
    venv
    register_donor
    dfu_present || die "radio is not in DFU mode (power off, hold SK1, power on)"
    echo "== flash $BIN =="
    local out
    out="$("$VENV/bin/python" "$LOADER" -m "$MODEL" -f "$BIN" 2>&1 | tr '\r' '\n' | grep -vE "^ \*\*\*   > .* [0-9]+% *$")"
    echo "$out" | grep -E "Patching|Flashing|Finished|complete|!!!|Error" || true
    echo "$out" | grep -q "Patching for DMR" || die "flashed WITHOUT the DMR codec (FM only), check the donor file"
    echo "$out" | grep -q "Finished" || die "flash did not finish"
    echo "Flash OK (with DMR codec). Radio is rebooting."
}

do_shot() {
    local out="${1:-screen.png}"
    venv
    echo "Waiting for the radio's serial port..."
    for _ in $(seq 1 40); do
        ls /dev/ttyACM* >/dev/null 2>&1 && break
        sleep 1
    done
    sleep "${SHOT_DELAY:-0}"
    "$VENV/bin/python" "$FW/tools/screen_grab.py" "$out"
}

case "${1:-}" in
    build) do_build ;;
    flash) do_flash ;;
    size)  do_size ;;
    shot)  do_shot "${2:-screen.png}" ;;
    all)
        do_build
        venv
        wait_dfu 180
        do_flash
        SHOT_DELAY=10 do_shot "${2:-screen.png}"
        ;;
    *)
        sed -n '2,11p' "$0" | sed 's/^# \{0,1\}//'
        exit 1
        ;;
esac
