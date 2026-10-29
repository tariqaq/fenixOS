#!/usr/bin/env python3
"""App registry tooling (docs/registry.md).

  python tools/registry.py            check every registry/apps/*.json and write registry/index.json
  python tools/registry.py --check    same checks; fail if index.json is out of date (CI)
  python tools/registry.py --local OUT MANIFEST --board FXP [--pc EXE]
                                      write a local registry for testing (FENIX_REGISTRY=OUT)
"""
import argparse
import hashlib
import json
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REG = ROOT / "registry"
INDEX_FORMAT = 1

TILES = {"gray", "indigo", "blue", "teal", "green", "orange", "red", "purple", "pink"}
PERMISSIONS = {"keys", "media", "system-stats", "network", "files", "programs"}
SETTING_TYPES = {"toggle", "slider", "choice", "text", "button"}
ALLOWED_URL = "https://github.com/tariqaq/"
MAX_BOARD = 64 * 1024

ID_RE = re.compile(r"^io\.github\.[A-Za-z0-9-]+\.[A-Za-z0-9._-]+$")
VER_RE = re.compile(r"^\d+\.\d+\.\d+$")
KEY_RE = re.compile(r"^[A-Za-z0-9_-]{1,32}$")
SHA_RE = re.compile(r"^[0-9a-f]{64}$")


def check(m, local=False):
    """Problems with one manifest, as a list of strings (empty when it is fine)."""
    errs = []

    def need(cond, msg):
        if not cond:
            errs.append(msg)

    def text(key, limit):
        v = m.get(key)
        need(isinstance(v, str) and 0 < len(v.encode()) <= limit, f"{key}: 1..{limit} bytes")

    need(isinstance(m.get("id"), str) and ID_RE.match(m["id"]) and len(m["id"]) <= 48,
         "id: io.github.<owner>.<name>, letters digits . - _, <= 48 bytes")
    text("name", 24)
    text("description", 100)
    for k in ("version", "min_fenixos"):
        need(isinstance(m.get(k), str) and VER_RE.match(m[k]), f"{k}: x.y.z")
    c = m.get("color")
    need(c in TILES or (isinstance(c, str) and re.match(r"^#[0-9A-Fa-f]{6}$", c)), "color: tile token or #RRGGBB")
    need(isinstance(m.get("icon"), str) and re.match(r"^[a-z0-9-]+$", m["icon"]), "icon: Phosphor icon name")
    need(isinstance(m.get("repo"), str) and m["repo"].startswith(ALLOWED_URL), f"repo: must start with {ALLOWED_URL}")

    def download(key, required):
        d = m.get(key)
        if d is None:
            need(not required, f"{key}: missing")
            return
        if not isinstance(d, dict):
            errs.append(f"{key}: object")
            return
        url = d.get("url")
        ok_url = isinstance(url, str) and (url.startswith(ALLOWED_URL) or (local and "://" not in url and ".." not in url))
        need(ok_url, f"{key}.url: must start with {ALLOWED_URL}")
        need(isinstance(d.get("sha256"), str) and SHA_RE.match(d["sha256"]), f"{key}.sha256: 64 lowercase hex")
        need(isinstance(d.get("size"), int) and d["size"] > 0, f"{key}.size: bytes > 0")
        if key == "board":
            need(isinstance(d.get("size"), int) and d["size"] <= MAX_BOARD, f"board.size: <= {MAX_BOARD}")
        if key == "pc":
            need(isinstance(d.get("elevated", False), bool), "pc.elevated: true/false")

    download("board", True)
    download("pc", False)
    perms = m.get("permissions", [])
    need(isinstance(perms, list) and all(p in PERMISSIONS for p in perms) and len(set(perms)) == len(perms),
         f"permissions: unique, from {sorted(PERMISSIONS)}")
    settings = m.get("settings", [])
    if not isinstance(settings, list):
        errs.append("settings: list")
        settings = []
    keys = set()
    for i, s in enumerate(settings):
        where = f"settings[{i}]"
        if not isinstance(s, dict):
            errs.append(f"{where}: object")
            continue
        t = s.get("type")
        need(t in SETTING_TYPES, f"{where}.type: one of {sorted(SETTING_TYPES)}")
        need(isinstance(s.get("key"), str) and KEY_RE.match(s["key"]) and s["key"] not in keys, f"{where}.key: unique, letters digits _ -")
        keys.add(s.get("key"))
        need(isinstance(s.get("label"), str) and 0 < len(s["label"]) <= 40, f"{where}.label: 1..40 chars")
        d = s.get("default")
        if t == "toggle":
            need(isinstance(d, bool), f"{where}.default: true/false")
        elif t == "slider":
            nums = [s.get(k) for k in ("min", "max", "step")]
            ok = all(isinstance(x, (int, float)) and not isinstance(x, bool) for x in nums + [d])
            need(ok and nums[0] < nums[1] and nums[2] > 0 and nums[0] <= d <= nums[1], f"{where}: min < max, step > 0, min <= default <= max")
        elif t == "choice":
            opts = s.get("options")
            ok = isinstance(opts, list) and opts and all(isinstance(o, dict) and isinstance(o.get("value"), str) and isinstance(o.get("label"), str) for o in opts)
            need(ok and d in [o["value"] for o in opts], f"{where}: options [{{value, label}}], default is one of the values")
        elif t == "text":
            need(isinstance(d, str) and len(d) <= s.get("max_len", 100), f"{where}.default: string <= max_len")
    return errs


def load_apps():
    apps, errs = [], []
    for p in sorted((REG / "apps").glob("*.json")):
        try:
            m = json.loads(p.read_text(encoding="utf-8"))
        except json.JSONDecodeError as e:
            errs.append(f"{p.name}: {e}")
            continue
        for e in check(m):
            errs.append(f"{p.name}: {e}")
        if m.get("id") and p.stem != m["id"]:
            errs.append(f"{p.name}: file name must be <id>.json")
        apps.append(m)
    return apps, errs


def index_text(apps):
    apps = sorted(apps, key=lambda m: m["id"])
    return json.dumps({"format": INDEX_FORMAT, "apps": apps}, indent=2, ensure_ascii=False) + "\n"


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def make_local(out, manifest, board, pc):
    out = Path(out)
    m = json.loads(Path(manifest).read_text(encoding="utf-8"))
    files = out / "files"
    files.mkdir(parents=True, exist_ok=True)
    for key, src in (("board", board), ("pc", pc)):
        if not src:
            m.pop(key, None)
            continue
        src = Path(src)
        dst = files / src.name
        shutil.copyfile(src, dst)
        entry = m.get(key) or {}
        entry.update(url=f"files/{src.name}", sha256=sha256(dst), size=dst.stat().st_size)
        m[key] = {k: entry[k] for k in ("url", "sha256", "size", "elevated") if k in entry}
    errs = check(m, local=True)
    if errs:
        sys.exit("manifest: " + "; ".join(errs))
    (out / "index.json").write_text(index_text([m]), encoding="utf-8")
    print(f"local registry: {out / 'index.json'} ({m['id']} {m['version']})")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--local", nargs=2, metavar=("OUT", "MANIFEST"))
    ap.add_argument("--board")
    ap.add_argument("--pc")
    a = ap.parse_args()
    if a.local:
        if not a.board:
            sys.exit("--local needs --board")
        return make_local(a.local[0], a.local[1], a.board, a.pc)
    apps, errs = load_apps()
    if errs:
        sys.exit("\n".join(errs))
    want = index_text(apps)
    idx = REG / "index.json"
    if a.check:
        have = idx.read_text(encoding="utf-8") if idx.exists() else ""
        if have != want:
            sys.exit("registry/index.json is out of date: run python tools/registry.py")
        print(f"registry: {len(apps)} apps, ok")
        return
    idx.write_text(want, encoding="utf-8", newline="\n")
    print(f"registry: wrote index.json ({len(apps)} apps)")


if __name__ == "__main__":
    main()
