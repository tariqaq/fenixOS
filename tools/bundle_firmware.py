#!/usr/bin/env python3
"""Copy the built board firmware into the PC app's resources (PLAN §3.5 Flasher).

  python tools/bundle_firmware.py        after `board/idf.ps1 build`

Writes app/src-tauri/resources/firmware/: the bin files named in board/build/flasher_args.json
and firmware.json (version, then offset / file / size / sha256 for each). The PC app flashes
exactly these with espflash. The LittleFS app store and NVS settings are not in the image, so
apps and settings survive an update.

With the signing key (tools/sign_fw.py: FENIX_FW_SIGNING_KEY or ~/.fenixos/fw-signing.pem) it
also writes fenixOS_<version>.fwp, the signed package the PC app sends over
the link to boards on 0.1.22 and later (P52, docs/ota.md), and names it in firmware.json ("fwp").
Without the key it warns and the app updates over USB only; --require-key makes that an error
(release builds).
"""
import hashlib
import json
import os
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
    for f in [*OUT.glob("*.bin"), *OUT.glob("*.fwp")]:
        f.unlink()
    for off, rel in sorted(args["flash_files"].items(), key=lambda kv: int(kv[0], 16)):
        src = BUILD / rel
        data = src.read_bytes()
        shutil.copyfile(src, OUT / src.name)
        files.append({"offset": int(off, 16), "file": src.name, "size": len(data),
                      "sha256": hashlib.sha256(data).hexdigest()})
    meta = {"version": version, "chip": chip, "files": files}
    fwp = sign(BUILD / args["app"]["file"], version)
    if fwp:
        meta["fwp"] = fwp
    (OUT / "firmware.json").write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8", newline="\n")
    total = sum(f["size"] for f in files)
    print(f"firmware v{version}: {len(files)} files, {total // 1024} KB -> {OUT.relative_to(ROOT)}")


def sign(app_bin: Path, version: str):
    """Write the signed package next to the bins; its file name, or None without the key."""
    try:
        sys.path.insert(0, str(ROOT / "tools"))
        import sign_fw
    except ImportError as e:
        return no_key(f"tools/sign_fw.py needs the cryptography package ({e})")
    if not (os.environ.get("FENIX_FW_SIGNING_KEY") or sign_fw.KEY_FILE.exists()):
        return no_key("no signing key")
    key = sign_fw.load_key()
    if sign_fw.pub_bytes(key.public_key()) != sign_fw.pub_bytes(sign_fw.header_pub()):
        sys.exit("the signing key does not match board/components/fenix_sys/fw_pubkey.h")
    data = sign_fw.pack(app_bin.read_bytes(), key)
    if sign_fw.check(data, sign_fw.header_pub()) != version:
        sys.exit(f"{app_bin.name} is not v{version}")
    name = f"fenixOS_{version}.fwp"
    (OUT / name).write_bytes(data)
    print(f"signed {name}: {len(data) // 1024} KB")
    return name


def no_key(why: str):
    if "--require-key" in sys.argv:
        sys.exit(f"{why}: a release must carry the signed package")
    print(f"warning: {why}; no .fwp, so this app updates the board over USB only")
    return None


if __name__ == "__main__":
    main()
