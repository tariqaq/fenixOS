#!/usr/bin/env python3
"""Open-source licenses list for Settings > About (artboard 13).

  python tools/licenses.py           write app/src/lib/licenses.json
  python tools/licenses.py --check   fail if it is out of date (CI)

Collects what ships: the Rust crates the PC app links (cargo tree, Windows target, normal
dependencies, our own path crates left out), the UI's runtime npm packages plus the
Svelte runtime (from app/node_modules, so run `pnpm install` first), and the board firmware's libraries (versions
from board/dependencies.lock and LovyanGFX's library.json).
"""
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "app" / "src" / "lib" / "licenses.json"


def rust():
    out = subprocess.run(
        ["cargo", "tree", "-p", "fenixos", "-e", "normal", "--prefix", "none",
         "--format", "{p}|{l}", "--target", "x86_64-pc-windows-msvc"],
        cwd=ROOT, check=True, capture_output=True, text=True).stdout
    seen = {}
    for line in out.splitlines():
        # cargo tree marks a crate it already printed with a trailing " (*)".
        p, _, lic = line.removesuffix(" (*)").partition("|")
        p = p.strip()
        if "(" in p:  # a path crate: part of fenixOS itself
            continue
        name, ver = p.rsplit(" ", 1)
        seen[(name, ver)] = lic.strip() or "see crate"
    return [{"name": n, "version": v.lstrip("v"), "license": l} for (n, v), l in sorted(seen.items())]


def npm():
    pkg = json.loads((ROOT / "app" / "package.json").read_text(encoding="utf-8"))
    rows = []
    # Svelte is a dev dependency, but its runtime is compiled into the UI bundle.
    for name in sorted([*pkg["dependencies"], "svelte"]):
        meta = json.loads((ROOT / "app" / "node_modules" / name / "package.json").read_text(encoding="utf-8"))
        rows.append({"name": name, "version": meta["version"], "license": meta.get("license", "see package")})
    return rows


def board():
    lock = (ROOT / "board" / "dependencies.lock").read_text(encoding="utf-8")

    def locked(comp):
        m = re.search(rf"^  {re.escape(comp)}:\n(?:    .*\n)*?    version: (\S+)", lock, re.M)
        return m.group(1).strip("'\"") if m else "?"

    lgfx = json.loads((ROOT / "board" / "components" / "LovyanGFX" / "library.json").read_text(encoding="utf-8"))
    return [
        {"name": "ESP-IDF", "version": locked("idf"), "license": "Apache-2.0"},
        {"name": "LVGL", "version": locked("lvgl/lvgl"), "license": "MIT"},
        {"name": "LovyanGFX", "version": lgfx["version"], "license": lgfx["license"]},
        {"name": "esp_littlefs (joltwallet)", "version": locked("joltwallet/littlefs"), "license": "MIT"},
        {"name": "littlefs (in esp_littlefs)", "version": "", "license": "BSD-3-Clause"},
        {"name": "Inter (board fonts)", "version": "", "license": "OFL-1.1"},
        {"name": "Phosphor Icons (board icons)", "version": "", "license": "MIT"},
    ]


def build():
    return {
        "board": board(),
        "web": npm(),
        "rust": rust(),
    }


def main():
    text = json.dumps(build(), indent=1) + "\n"
    if "--check" in sys.argv:
        old = OUT.read_text(encoding="utf-8") if OUT.exists() else ""
        if old != text:
            import difflib
            sys.stdout.writelines(difflib.unified_diff(old.splitlines(True), text.splitlines(True), "committed", "now", n=1))
            sys.exit(f"{OUT.relative_to(ROOT)} is out of date: run python tools/licenses.py")
        print("licenses ok")
        return
    OUT.write_text(text, encoding="utf-8", newline="\n")
    d = build()
    print(f"licenses: {len(d['board'])} board, {len(d['web'])} web, {len(d['rust'])} rust -> {OUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
