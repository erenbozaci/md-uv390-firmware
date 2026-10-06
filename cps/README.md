# UV390 CPS (PyQt5)

Small codeplug reader for this firmware fork (MDUV380 / UV390 Plus). **Read only for now**: it reads
channels, contacts, TG lists, zones and the radio info over the USB serial port, shows them in tables,
saves a backup (`.json`) and exports CSV. Writing to the radio is not implemented yet.

    pip install -r requirements.txt
    python run.py

Radio on and in normal mode (not DFU / hotspot). Linux: your user needs the `dialout` group
(`/dev/ttyACM0`). Layout and protocol: `uv390cps/protocol.py`, `uv390cps/codeplug.py`
(from `usb_com.c` and `codeplug.c`).

Standalone builds (PyInstaller): `./build.sh` on each OS, or the `cps` GitHub workflow builds
Linux, Windows and macOS and attaches them as artifacts.
