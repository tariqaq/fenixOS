#!/usr/bin/env python3
"""Copy the built board firmware into the PC app's resources (PLAN §3.5 Flasher).

  python tools/bundle_firmware.py        after `board/idf.ps1 build`

Writes app/src-tauri/resources/firmware/: the bin files named in board/build/flasher_args.json
and firmware.json (version, then offset / file / size / sha256 for each). The PC app flashes
exactly these with espflash. The LittleFS app store and NVS settings are not in the image, so
apps and settings survive an update.
"""
import hashlib
import json
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "board" / "build"
OUT = ROOT / "app" / "src-tauri" / "resources" / "firmware"


def main():
    args_file = BUILD / "flasher_args.json"
    if not args_file.exists():
        sys.exit("board/build/flasher_args.json missing: build the firmware first (board/idf.ps1 build)")
    args = json.loads(args_file.read_text(encoding="utf-8"))
    version = json.loads((BUILD / "project_description.json").read_text(encoding="utf-8"))["project_version"]
    want = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    if version != want:
        sys.exit(f"board/build is v{version} but VERSION says {want}: rebuild the firmware")
    chip = args.get("extra_esptool_args", {}).get("chip", "esp32")
    files = []
    for f in OUT.glob("*.bin"):
        f.unlink()
    for off, rel in sorted(args["flash_files"].items(), key=lambda kv: int(kv[0], 16)):
        src = BUILD / rel
        data = src.read_bytes()
        shutil.copyfile(src, OUT / src.name)
        files.append({"offset": int(off, 16), "file": src.name, "size": len(data),
                      "sha256": hashlib.sha256(data).hexdigest()})
    meta = {"version": version, "chip": chip, "files": files}
    (OUT / "firmware.json").write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8", newline="\n")
    total = sum(f["size"] for f in files)
    print(f"firmware v{version}: {len(files)} files, {total // 1024} KB -> {OUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
