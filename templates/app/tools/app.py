#!/usr/bin/env python3
"""fenixOS app tooling (docs/fenixos/registry.md).

  python tools/app.py check            manifest rules, and app/board/app.toml id and version match
  python tools/app.py local            dist/registry: a local registry for FENIX_REGISTRY (uses dist/app.fxp, dist/app.exe)
  python tools/app.py release TAG      dist/<id>.json: the manifest with release URLs, sha256 and sizes, for the
                                       registry PR on tariqaq/fenixOS (registry/apps/<id>.json)

The manifest rules below are copied from fenixOS tools/registry.py (v0.1.13); the registry's CI runs the
same checks.
"""
import argparse
import hashlib
import json
import re
import shutil
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DIST = ROOT / "dist"
MANIFEST = ROOT / "manifest.json"
APP_TOML = ROOT / "app" / "board" / "app.toml"
FILES = {"board": "app.fxp", "pc": "app.exe"}

TILES = {"gray", "indigo", "blue", "teal", "green", "orange", "red", "purple", "pink"}
PERMISSIONS = {"keys", "media", "system-stats", "network", "files", "programs"}
SETTING_TYPES = {"toggle", "slider", "choice", "text", "button"}
ALLOWED_URL = "https://github.com/tariqaq/"
MAX_BOARD = 64 * 1024

ID_RE = re.compile(r"^io\.github\.[A-Za-z0-9-]+\.[A-Za-z0-9._-]+$")
VER_RE = re.compile(r"^\d+\.\d+\.\d+$")
KEY_RE = re.compile(r"^[A-Za-z0-9_-]{1,32}$")
SHA_RE = re.compile(r"^[0-9a-f]{64}$")


INDEX_FORMAT = 1


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


def load():
    m = json.loads(MANIFEST.read_text(encoding="utf-8"))
    t = tomllib.loads(APP_TOML.read_text(encoding="utf-8"))
    errs = []
    for k in ("id", "version"):
        if t.get(k) != m.get(k):
            errs.append(f"{k}: app.toml has {t.get(k)!r}, manifest.json {m.get(k)!r}")
    return m, errs


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fill(m, url_of):
    """Fill url, sha256 and size of each part from dist/; drop `pc` when there is no exe."""
    for key, name in FILES.items():
        f = DIST / name
        if not f.exists():
            if key == "board":
                sys.exit(f"{f.relative_to(ROOT)} missing: run pwsh tools/build.ps1")
            m.pop(key, None)
            continue
        entry = dict(m.get(key) or {})
        entry.update(url=url_of(name), sha256=sha256(f), size=f.stat().st_size)
        m[key] = {k: entry[k] for k in ("url", "sha256", "size", "elevated") if k in entry}
    return m


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["check", "local", "release"])
    ap.add_argument("tag", nargs="?")
    a = ap.parse_args()
    m, errs = load()
    if a.cmd == "check":
        # Placeholder downloads so the rules check everything else before a release exists.
        probe = dict(m, board={"url": "https://github.com/tariqaq/x", "sha256": "0" * 64, "size": 1})
        if "pc" in m:
            probe["pc"] = dict(m["pc"], url="https://github.com/tariqaq/x", sha256="0" * 64, size=1)
        errs += check(probe)
        if errs:
            sys.exit("\n".join(errs))
        print(f"{m['id']} {m['version']}: ok")
    elif a.cmd == "local":
        if errs:
            sys.exit("\n".join(errs))
        out = DIST / "registry"
        (out / "files").mkdir(parents=True, exist_ok=True)
        for name in FILES.values():
            if (DIST / name).exists():
                shutil.copyfile(DIST / name, out / "files" / name)
        m = fill(m, lambda name: f"files/{name}")
        errs = check(m, local=True)
        if errs:
            sys.exit("\n".join(errs))
        (out / "index.json").write_text(json.dumps({"format": INDEX_FORMAT, "apps": [m]}, indent=2) + "\n", encoding="utf-8")
        print(f"local registry: {out}  (set FENIX_REGISTRY to it, then start fenixOS)")
    else:
        if not a.tag:
            sys.exit("release needs the tag, e.g. v0.1.0")
        if a.tag != f"v{m['version']}":
            errs.append(f"tag {a.tag} does not match version {m['version']}")
        if errs:
            sys.exit("\n".join(errs))
        base = f"{m['repo'].rstrip('/')}/releases/download/{a.tag}"
        m = fill(m, lambda name: f"{base}/{name}")
        errs = check(m)
        if errs:
            sys.exit("\n".join(errs))
        out = DIST / f"{m['id']}.json"
        out.write_text(json.dumps(m, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        print(f"{out.relative_to(ROOT)}: add it to tariqaq/fenixOS as registry/apps/{m['id']}.json")


if __name__ == "__main__":
    main()
