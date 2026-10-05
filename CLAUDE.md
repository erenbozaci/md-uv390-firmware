# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Unofficial fork of OpenGD77 (R20260131 upstream snapshot) for STM32F405-based radios: MDUV380 / RT3S / DM-1701 / RT-84. All firmware lives in `MDUV380_firmware/` (originally an STM32CubeIDE project; `.cproject` is the original build definition). Licence is non-commercial (see `license.txt`). The repo contains no AMBE codec code/data — it is patched in at flash time from a donor firmware; `application/source/dmr_codec/codec_bin.S` and `linkerdata/` are placeholders produced by `tools/codec_cleaner.py`.

## Build

Run from `MDUV380_firmware/`. There are no tests or linter.

```sh
make build                                            # containerised (podman; CONTAINER_ENGINE=docker for docker) -> build/OpenGD77_MDUV380.bin
make build PLATFORM=MDUV380 VARIANT=UV380_PLUS_10W    # -> build/OpenGD77_MDUV380_UV380_PLUS_10W.bin
make build PLATFORM=RT84_DM1701 VARIANT=DM1701
make build-all                                        # all three, bins copied to dist/
make rebuild                                          # clean + build
make all                                              # native build; needs arm-none-eabi-gcc + python3 on host (what runs inside the container)
```

- `PLATFORM` / `VARIANT` become `-DPLATFORM_<x>` / `-DPLATFORM_VARIANT_<y>`; source is heavily `#if defined(PLATFORM_...)`-gated. Objects from different platforms must not mix — `make clean` between them (`build-all` does this).
- Makefile was reconstructed from `.cproject` (sources: `SeggerRTT application Core Middlewares Drivers USB_DEVICE`, all `*.c`/`*.s`, minus `languages_builder.c`). New `.c` files are picked up automatically via `find`.
- Pipeline: `prepare` (codec_cleaner -C generates codec placeholder) → link ELF → objcopy `.bin` → `codec_cleaner.py -i/-o` post-process into final `OUT_NAME.bin`.
- `make flash` runs the loader script (`LOADER`/`PYTHON` default to paths outside this repo; override on the command line). Needs the radio in DFU mode, run on host not in container.
- `prepare` / `prepare.bat` at repo root are the original upstream codec_cleaner wrappers (Linux binary); the Makefile uses the Python port instead.

## CI / releases

`.github/workflows/build.yml` builds the 3-variant matrix in the Containerfile image on push/PR. Pushing a tag `R<upstream YYYYMMDD>-EA4IPW.<N>` publishes a GitHub Release, renaming bins to `OpenGD77_MDUV380_RT3S.bin`, `..._RT3S_10W.bin`, `OpenGD77_RT84_DM1701.bin` plus `SHA256SUMS`. If you add/rename a variant, update both the Makefile `ALL_VARIANTS` and the workflow matrix + release staging step.

## Architecture

FreeRTOS firmware on STM32F405 (HAL + USB CDC device). Under `MDUV380_firmware/application/`:

- `source/applicationMain.c` — entry/tasks; `Core/Src/main.c`+`freertos.c` are CubeMX-generated startup.
- `source/hardware/` — radio chip drivers (AT1846S RF, HR-C6000 DMR, display HX8353E, SPI flash, EEPROM); `interfaces/` — MCU peripheral wrappers (gpio, spi, i2c, i2s, adc, dac, gps, settings storage).
- `source/functions/` — radio logic: `trx.c` (RX/TX state), `codeplug.c`, `settings.c`, `hotspot.c`, `aprs.c`, `satellite.c`, `voicePrompts.c`, etc.
- `source/user_interface/` — one file per menu/screen (`menu*.c`, `uiVFOMode.c`, `uiChannelMode.c`, ...); `source/io/` — buttons, keyboard, display, LEDs.
- `include/` mirrors `source/`; `include/user_interface/languages/` holds the language packs (`LANGUAGE_BUILD_*` defines select a baked-in default).

Local feature work so far: VFO sweep band scope + scrolling waterfall in `uiVFOMode.c` (toggled with `*`).

## Notes

- `PLANS/unify_sourcecodes.md` describes the intended merge of the MD2017/MD9600 sibling upstream trees into this one (the source already contains `PLATFORM_MD2017`/`MD9600`/`MD380` ifdefs).
- `tools/` (under `MDUV380_firmware/`) holds the Python firmware loader, custom-data tool and `codec_cleaner.py` (uv project: `pyproject.toml`, `uv.lock`).
- Untracked IDE files (`.project`, `.settings/`) are not part of the repo.

## Saving tokens (read this before exploring)

- Do not read whole files over ~1000 lines (`HR-C6000.c` 3100, `uiVFOMode.c` 4200, `uiChannelMode.c` 4000, `uiUtilities.c` 4700, `aprs.c` 2000). `grep -n` for the symbol, then `Read` with `offset`/`limit`.
- Never read `Drivers/`, `Middlewares/`, `USB_DEVICE/`, `Core/` (vendor/CubeMX code) or `include/user_interface/languages/*.h` (20 near-identical string tables).
- Build output is noisy: pipe `make` through `grep -E "error|warning|CLEAN"`. `#warning` lines in `AT1846S.c` and `radioHardwareInterface.c` are pre-existing.
- The C6000 manual is at `hr-c6000/HR_C6000_translated.pdf`. Convert with `pdftotext -layout` to a scratch file and grep it (data frame types, RAM map, TX/RX procedures are in sections 5.3-5.4.5). Register notes: `hr-c6000/HR-C6000 Registers_G4EML.docx` (zip, `word/document.xml`).

## Building and flashing (UV390 Plus 10W is the target radio)

- Build: `cd MDUV380_firmware && make rebuild CONTAINER_ENGINE=docker PLATFORM=MDUV380 VARIANT=UV380_PLUS_10W` (use `rebuild`, not `build`, when switching variant or after header-only changes; podman is the Makefile default, docker works).
- Flash: radio in DFU (off, hold SK1, power on, check `lsusb | grep 0483:df11`), then `python tools/opengd77_stm32_firmware_loader.py -m MD-UV380 -f build/OpenGD77_MDUV380_UV380_PLUS_10W.bin`. Needs `pyusb` (use a venv; the Makefile's default `LOADER`/`PYTHON` paths do not exist here).
- **DMR needs the codec donor**: `donor/MD9600-CSV(2571V5)-V26.45.bin` (SHA-256 `d8a65330...f11f`, the loader rejects any other file, including the `P26.45` GPS one). It is registered in `~/.gd77firmwareloader.ini`; register again with `-s <file>` **alone** (`-s` combined with `-l` exits before saving). The loader prints `Patching for DMR` when it is used; without it the radio ends up FM-only. `donor/` is gitignored, never commit it.
- Flashing is irreversible-ish for the running firmware; only flash when the user asks.

## Messaging feature (added in this fork)

- `application/source/functions/messaging.c` + `include/functions/messaging.h`: RAM-only ring buffer (12 messages, lost at power-off), canned messages, `messagingReceive()` (beep + toast), `messagingTick()` (called from the main loop, drains the DMR receive mailbox, 10 s watchdog on DMR TX), `messagingSendAPRS()`, `messagingSendDMR()`.
- UI: `user_interface/menuMessages.c` (`MENU_MESSAGES`, Main menu "Messages", string offset 281 = `.messages`, language tag version 7). Digital channel: To = DMR ID (digits) -> DMR data call. Analog channel: To = callsign -> APRS message.
- **APRS send**: `aprsMessageSend()` in `aprs.c` reuses the beacon path (`aprsBeaconingSendBeacon`) with a private `aprsOutgoingMessage.pending` flag; works with beaconing OFF but needs an APRS config on the channel. There is no APRS receive path.
- **DMR receive**: `hrc6000SysReceivedDataInt()` (interrupt context) hands good data frames (types 0x6 header, 0x7/0x8/0xA blocks, read from SPI page 0x02) to `messagingDmrRxFrame()`. Only unconfirmed/confirmed data packets (DPF 2/3) addressed to our ID or current TG; no UDT/short data; text is the longest printable run (8-bit or UTF-16LE), heuristic.
- **DMR transmit**: `HRC6000DataTxStart()` queues a job; the main loop (`applicationMain.c`, just before `hasSignal = false;`) holds a software PTT while `HRC6000DataTxIsActive()`, so the normal TX screen runs. In the slot state machine (`hrc6000TimeslotInterruptHandler`) `dataTx.inUse` bypasses the voice branches: data header x3 (reg 0x50 = 0x60), then one rate-1/2 block per active slot (0x70), no terminator. Header CRC/block CRC-32 come from the chip.
- **Unverified on air** (as of this writing): data header type (0x60 vs 0x64), SAP value, payload format against commercial radios, RMO/repeater operation, ack handling. Treat failures there as expected-until-tested.

## Gotchas

- `menuFunctions[]` and `menusData.data[]` in `menuSystem.c` must stay in the same order as `enum MENU_SCREENS`; quickkey menus go above the "Add new menus" comment (max 32).
- Adding a language string = new field at the end of `stringsTable_t` + a line in every language file + bump `LANGUAGE_TAG_VERSION` (language packs flashed separately must be rebuilt).
- Language/source files in `languages/` are Windows-1252: edit them as bytes, not UTF-8.
- `aprsBeaconingSendBeacon()` and everything under `aprs.c` runs in the UI task and calls `vTaskDelay`; do not call from interrupt context.

## Dual Watch options (this fork)

- Options -> "Dual Watch" (`menuDualWatchOptions.c`, `MENU_DUAL_WATCH`, reuses language string 143 so no language-file change). Stored in `nonVolatileSettings.dualWatchOptions` (the old `UNUSED_1`, 0 = stock behaviour, no settings reset); bit layout and `DUALWATCH_*` macros in `settings.h`.
- Options: Auto start (starts at every VFO screen entry, also after TX), Home VFO, Speed (default / 90 / 200 / 400 ms), On RX Switch/Stay. Logic is in `uiVFOMode.c`: `dualWatchStart()` (shared with the VFO quick menu), `dualWatchStayCheck()` (called first in `scanning()`).
- Stay = on the non-home VFO a carrier only gives a rate-limited beep and the scan hops back; there is no real simultaneous receive (single AT1846S on MDUV380) and no priority pre-emption while paused on a signal.

## Screenshots / verifying the UI on the real radio

- The running radio is a USB serial device (`/dev/ttyACM0`, "OpenMDUV380Plus_10W"). `MDUV380_firmware/tools/screen_grab.py out.png` reads the display buffer (CPS 'R' command, area 6) and writes a PNG; then `Read` the PNG to look at it. Needs `pyserial` + `pillow` (venv in the scratchpad). Works in normal operation; not in DFU/hotspot mode. Do not send CPS command 0 (it opens the CPS screen on the radio).
- Channel side: option "Side B: Channel" makes Dual Watch alternate between the VFO and the current zone channel (`dwChannelData`; `currentChannelData` is switched, `currentVFONumber` is not; flags `dualWatchChannelSide`/`dualWatchOnChannel`). Known limits: stopping (key or PTT) while on the channel retunes the VFO, so PTT transmits on the VFO; channel-side loads touch per-VFO TG index state.
- Arrow keys: the MD-UV390 has only Up/Down arrow keys (no Left/Right); on MDUV380 `KEY_INCREASE`/`KEY_DECREASE` are `KEY_FRONT_UP`/`KEY_FRONT_DOWN` (keyboard.h). Their behaviour is the stock one (squelch in analog, TG step in digital); an earlier A/B-switch remap conflicted with the stock RX/TX focus handling and was removed. The red key switches between the channel and VFO screens (stock).

## RAM is almost full (important)

- The MDUV380 build links with only a few tens of bytes to spare in `RAM` (`arm-none-eabi-size build/MDUV380_FW.elf`: data+bss about 191.6 KB). Adding ~200 bytes of static data made the link fail with "section `._user_heap_stack' will not fit in region `RAM'". Keep new static buffers tiny (messaging store is 12 entries, DMR RX reassembly 12 blocks on purpose) and check the link after every addition.

## Review findings kept as known limits

- DMR TX payload is raw UTF-16LE with SAP 0 (only this firmware reads it; commercial radios expect IP/UDP text). The message CRC-32 is left to the chip (unverified); RX does not re-check it in software, and the pad octet count is read from header octet 1 low nibble.
- A DMR message is added to the inbox as "sent" when queued, not when the transmission finished.

## Dual screen (Anytone style, two independent rows)

- Options -> Dual Watch -> "Dual scr: On": the VFO screen and the channel screen both draw two rows, A (top) and B (bottom), by `uiDualScreenDraw()` in `uiDualScreen.c`. Each row is independently a VFO (row A = VFO A, row B = VFO B) or a channel; a channel row has its own zone and index in it (`slotZone[]`, `slotIndex[]`, RAM only). Three text lines per row: name / frequency + FM|DMR / zone (small font). The active row is highlighted.
- Keys (`uiDualScreenHandleKey()`, called first in both `handleEvent()`s, not used with SK1/SK2, while scanning, TX, digit entry or channel details): Up/Down arrows (`KEY_FRONT_UP/DOWN`) select the other row, red key toggles the active row VFO <-> channel, rotary is stock (frequency or channel). Arrows therefore no longer do squelch/TG in this mode.
- No pointer swapping: the active row is simply the stock VFO or channel screen (switched with `menuSystemSetCurrentMenu`, channel data flagged invalid with `channelScreenChannelData.rxFreq = 0` so it reloads, zone set like the zone list does); the other row is only drawn from the codeplug / `settingsVFOChannel`. State bits `DUALWATCH_LINE_A_CHANNEL/_LINE_B_CHANNEL/_ACTIVE_B` in `dualWatchOptions`; `uiDualScreenScreenEntered()` re-syncs when a screen is entered any other way.
- Limits: the TG override and the channel TG list index are shared by both channel rows; only one receiver, the inactive row is not monitored (Dual Watch is separate and not linked to the rows yet).
