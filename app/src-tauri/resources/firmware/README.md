# Bundled board firmware

Filled by `python tools/bundle_firmware.py` after a firmware build: the bin files and `firmware.json` (version, offsets, sizes, sha256). They are git-ignored, and so is the signed `fenixOS_<v>.fwp` (P52, `docs/ota.md`); release builds create them in CI. The PC app flashes them with `crates/fenix-flash` (espflash).
