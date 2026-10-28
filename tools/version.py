"""Keep every version string in step with the VERSION file (lockstep SemVer).

Usage: python tools/version.py          (write VERSION into every target)
       python tools/version.py --check  (fail if any target differs)
"""
import json, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
VERSION = (ROOT / "VERSION").read_text(encoding="utf-8").strip()

# (file, regex with one group for the version, kind)
TARGETS = [
    ("Cargo.toml", r'(?ms)^\[workspace\.package\].*?^version = "([^"]+)"'),
    ("app/package.json", None),
    ("app/src-tauri/tauri.conf.json", None),
]


def json_get(path):
    return json.loads(path.read_text(encoding="utf-8"))["version"]


def json_set(path, v):
    text = path.read_text(encoding="utf-8")
    text = re.sub(r'("version"\s*:\s*")[^"]+(")', rf"\g<1>{v}\g<2>", text, count=1)
    path.write_text(text, encoding="utf-8", newline="\n")


def main():
    check = "--check" in sys.argv
    bad = []
    for rel, rx in TARGETS:
        path = ROOT / rel
        if not path.exists():
            continue
        if rx is None:
            cur = json_get(path)
            if cur != VERSION:
                bad.append(f"{rel}: {cur}") if check else json_set(path, VERSION)
        else:
            text = path.read_text(encoding="utf-8")
            m = re.search(rx, text)
            if not m:
                sys.exit(f"{rel}: version not found")
            if m.group(1) != VERSION:
                if check:
                    bad.append(f"{rel}: {m.group(1)}")
                else:
                    text = text[: m.start(1)] + VERSION + text[m.end(1):]
                    path.write_text(text, encoding="utf-8", newline="\n")
    # The board reads VERSION directly in board/CMakeLists.txt, so it cannot drift.
    if bad:
        sys.exit(f"VERSION is {VERSION}, but: " + "; ".join(bad) + " (run python tools/version.py)")
    print(f"version {VERSION}: ok" if check else f"version {VERSION}: written")


if __name__ == "__main__":
    main()
