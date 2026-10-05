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
