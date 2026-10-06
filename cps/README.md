# UV390 CPS (PyQt5)

Desktop codeplug editor for this firmware fork (MDUV380 / UV390 Plus): read the radio over USB, edit
channels, contacts, TG lists and zones, write the changes back.

    pip install -r requirements.txt
    python run.py

- Radio on, normal mode (not DFU / hotspot). Linux: user needs the `dialout` group (`/dev/ttyACM0`).
- Edited rows are tinted (green = new, amber = changed). The header shows what is unsaved.
- **Write to radio** first saves a backup of what is on the radio now to `Documents/UV390CPS backups/`,
  then writes only the bytes that changed, reads them back to verify, and restarts the radio.
  Only the MD-UV380 / UV390 radio type is accepted for writing.
- Fields the program does not know about are written back unchanged (every record keeps its raw bytes).
- File menu: backup open/save (`.json`, works without a radio), CSV export.

Protocol and layout: `uv390cps/protocol.py`, `uv390cps/codeplug.py` (from `usb_com.c`, `codeplug.c`).
Not edited yet: general settings other than callsign / DMR ID, DTMF, APRS configs, quick keys, DMR ID database.

Standalone builds (PyInstaller): `./build.sh` on each OS, or the `cps` GitHub workflow (Linux, Windows,
macOS artifacts).
