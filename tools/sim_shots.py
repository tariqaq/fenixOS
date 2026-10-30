#!/usr/bin/env python3
"""Board screens from the PC simulator, checked against golden images.

  python tools/sim_shots.py            build, run board/sim/screens.txt, compare with board/sim/golden
  python tools/sim_shots.py --update   same, but rewrite the golden images (review the diff in git)
  python tools/sim_shots.py --no-build skip the CMake build (use the existing fenix_sim.exe)

Needs Windows with the Visual Studio C++ tools (board/sim/build.ps1) and cargo (fenix-pkg builds
the sample app). Shots land in board/sim/build/shots; failures also write <name>.diff.png there,
with the changed pixels in red.
"""
import shutil
import struct
import subprocess
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SIM = ROOT / "board" / "sim"
BUILD = SIM / "build"
SHOTS = BUILD / "shots"
GOLDEN = SIM / "golden"
EXE = BUILD / "fenix_sim.exe"
SIM_TIMEOUT_S = 60


def read_png(path):
    """(w, h, rows of RGB bytes) for the 8-bit RGB, filter-0 PNGs that fenix_sim and this tool write."""
    data = path.read_bytes()
    pos, idat, w, h = 8, b"", 0, 0
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
            if (depth, ctype) != (8, 2):
                sys.exit(f"{path}: not 8-bit RGB")
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = 1 + w * 3
    rows = []
    for y in range(h):
        line = raw[y * stride:(y + 1) * stride]
        if line[0] != 0:
            sys.exit(f"{path}: unexpected PNG filter")
        rows.append(line[1:])
    return w, h, rows


def write_png(path, w, h, rows):
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    raw = b"".join(b"\0" + r for r in rows)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def run(cmd):
    print("+", " ".join(str(c) for c in cmd), flush=True)
    if subprocess.run(cmd, cwd=ROOT).returncode != 0:
        sys.exit(f"failed: {cmd[0]}")


def main():
    args = sys.argv[1:]
    if not (SIM / "lv_kconfig.h").exists():
        sys.exit("board/sim/lv_kconfig.h missing: run python tools/sim_config.py")
    if "--no-build" not in args:
        run(["pwsh", "-NoProfile", "-File", str(SIM / "build.ps1")])
    run(["cargo", "run", "-q", "-p", "fenix-pkg", "--", "build", "tools/sample-app/board", "-o",
         str(BUILD / "sample.fxp")])
    if SHOTS.exists():
        shutil.rmtree(SHOTS)
    # A run takes well under a second; one hang was seen once (P42, not reproduced in P45), so a
    # stuck simulator fails the check instead of blocking CI.
    try:
        rc = subprocess.run([str(EXE), "run", "screens.txt", "--out", str(SHOTS)], cwd=SIM,
                            stdout=subprocess.DEVNULL, timeout=SIM_TIMEOUT_S).returncode
    except subprocess.TimeoutExpired:
        sys.exit(f"fenix_sim did not finish in {SIM_TIMEOUT_S} s (killed)")
    if rc != 0:
        sys.exit("fenix_sim failed")

    shots = sorted(SHOTS.glob("*.png"))
    if "--update" in args:
        GOLDEN.mkdir(exist_ok=True)
        for old in GOLDEN.glob("*.png"):
            old.unlink()
        for s in shots:
            write_png(GOLDEN / s.name, *read_png(s))
        print(f"{len(shots)} golden images in {GOLDEN.relative_to(ROOT)}")
        return

    bad = []
    names = {s.name for s in shots}
    for g in sorted(GOLDEN.glob("*.png")):
        if g.name not in names:
            bad.append(f"{g.stem}: no longer shot")
    for s in shots:
        g = GOLDEN / s.name
        if not g.exists():
            bad.append(f"{s.stem}: new screen with no golden image")
            continue
        sw, sh, srows = read_png(s)
        gw, gh, grows = read_png(g)
        if (sw, sh) != (gw, gh):
            bad.append(f"{s.stem}: {sw}x{sh}, golden {gw}x{gh}")
            continue
        diff, out = 0, []
        for a, b in zip(srows, grows):
            row = bytearray(a)
            for x in range(0, len(a), 3):
                if a[x:x + 3] != b[x:x + 3]:
                    diff += 1
                    row[x:x + 3] = b"\xff\x00\x00"
            out.append(bytes(row))
        if diff:
            write_png(SHOTS / f"{s.stem}.diff.png", sw, sh, out)
            bad.append(f"{s.stem}: {diff} pixels differ ({s.stem}.diff.png)")
    for b in bad:
        print("FAIL", b)
    if bad:
        sys.exit(f"{len(bad)} of {len(shots)} screens differ (python tools/sim_shots.py --update if intended)")
    print(f"{len(shots)} screens match the golden images")


if __name__ == "__main__":
    main()
