#!/usr/bin/env python3
"""App repo template (P44): zip templates/app into fenixos-app-template.zip at the repo root.

  python tools/app_template.py           write the zip
  python tools/app_template.py --check   fail if the zip is out of date (CI)

The zip holds one folder, fenixos-app-template/. Entries are sorted and dated 1980-01-01. The check
compares names and contents, not zip bytes (zlib builds may compress differently), and that the
template's design/tokens.json equals the repo's.
"""
import io
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "templates" / "app"
OUT = ROOT / "fenixos-app-template.zip"
TOP = "fenixos-app-template"
SKIP = {"target", "dist"}


def lf(data):
    """Same bytes from a Windows (CRLF) or Linux checkout."""
    return data.replace(b"\r\n", b"\n")


def entries():
    """{name in the zip: bytes} from templates/app."""
    out = {}
    for p in sorted(SRC.rglob("*")):
        rel = p.relative_to(SRC)
        if p.is_dir() or SKIP & set(rel.parts):
            continue
        data = p.read_bytes()
        out[f"{TOP}/{rel.as_posix()}"] = data if p.suffix == ".png" else lf(data)
    return out


def build(files):
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name, data in files.items():
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, data)
    return buf.getvalue()


def main():
    files = entries()
    if files[f"{TOP}/design/tokens.json"] != lf((ROOT / "design" / "tokens.json").read_bytes()):
        sys.exit("templates/app/design/tokens.json differs from design/tokens.json: copy it")
    if "--check" in sys.argv[1:]:
        have = {}
        if OUT.exists():
            with zipfile.ZipFile(OUT) as z:
                have = {n: z.read(n) for n in z.namelist()}
        if have != files:
            sys.exit(f"{OUT.name} is out of date: run python tools/app_template.py")
        print(f"{OUT.name}: up to date ({len(files)} files)")
        return
    data = build(files)
    OUT.write_bytes(data)
    print(f"{OUT.name}: {len(files)} files, {len(data)} bytes")


if __name__ == "__main__":
    main()
