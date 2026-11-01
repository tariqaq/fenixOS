#!/usr/bin/env python3
"""Signed firmware packages (.fwp) for over-the-air updates (P52, docs/ota.md).

  python tools/sign_fw.py keygen               make the signing key (once; never in git)
  python tools/sign_fw.py sign <app.bin> <out.fwp>   sign a built app image
  python tools/sign_fw.py verify <file.fwp>    check a package against the public key
  python tools/sign_fw.py pubkey               write the public key header for the firmware
  python tools/sign_fw.py --self-test          good, bad signature, bad hash, wrong key

Key: the FENIX_FW_SIGNING_KEY environment variable (the PEM text, as in the release workflow),
else %USERPROFILE%\\.fenixos\\fw-signing.pem. The public key is compiled into the firmware from
board/components/fenix_sys/fw_pubkey.h.

Format, integers little-endian, str = len:u8 || bytes:
  magic "FXFW" || format:u8 (1) || version:str || size:u32 || sha256:32 || signature:64 || image
signature = ECDSA P-256 over sha256(image), raw r || s. The board also checks that `version` is
the version inside the image (esp_app_desc_t), so a header cannot relabel an older image.
"""
import hashlib
import os
import struct
import sys
from pathlib import Path

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils

ROOT = Path(__file__).resolve().parent.parent
KEY_FILE = Path(os.environ.get("USERPROFILE", str(Path.home()))) / ".fenixos" / "fw-signing.pem"
PUB_H = ROOT / "board" / "components" / "fenix_sys" / "fw_pubkey.h"
MAGIC = b"FXFW"
FORMAT = 1
APP_DESC_MAGIC = 0xABCD5432


def load_key():
    pem = os.environ.get("FENIX_FW_SIGNING_KEY")
    if pem:
        data = pem.encode()
    elif KEY_FILE.exists():
        data = KEY_FILE.read_bytes()
    else:
        sys.exit(f"no signing key: set FENIX_FW_SIGNING_KEY or make {KEY_FILE} (keygen)")
    return serialization.load_pem_private_key(data, password=None)


def pub_bytes(pub) -> bytes:
    """65-byte uncompressed point (04 || X || Y)."""
    return pub.public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)


def image_version(image: bytes) -> str:
    """The version string of an ESP-IDF app image (esp_app_desc_t after the first segment header)."""
    if len(image) < 80 or image[0] != 0xE9:
        raise ValueError("not an ESP32 app image")
    magic, = struct.unpack_from("<I", image, 32)
    if magic != APP_DESC_MAGIC:
        raise ValueError("no app description in the image")
    return image[48:80].split(b"\0", 1)[0].decode()


def pack(image: bytes, key) -> bytes:
    version = image_version(image).encode()
    digest = hashlib.sha256(image).digest()
    der = key.sign(digest, ec.ECDSA(utils.Prehashed(hashes.SHA256())))
    r, s = utils.decode_dss_signature(der)
    sig = r.to_bytes(32, "big") + s.to_bytes(32, "big")
    head = MAGIC + bytes([FORMAT, len(version)]) + version + struct.pack("<I", len(image)) + digest + sig
    return head + image


def parse(fwp: bytes):
    """(version, size, sha256, signature, image), or ValueError."""
    if fwp[:4] != MAGIC or len(fwp) < 6 or fwp[4] != FORMAT:
        raise ValueError("not a fenixOS firmware package")
    n = fwp[5]
    version = fwp[6:6 + n].decode()
    p = 6 + n
    size, = struct.unpack_from("<I", fwp, p)
    sha = fwp[p + 4:p + 36]
    sig = fwp[p + 36:p + 100]
    image = fwp[p + 100:]
    if len(image) != size:
        raise ValueError(f"image is {len(image)} B, header says {size}")
    return version, size, sha, sig, image


def check(fwp: bytes, pub) -> str:
    """The checks the board makes; returns the version or raises ValueError."""
    version, _size, sha, sig, image = parse(fwp)
    if hashlib.sha256(image).digest() != sha:
        raise ValueError("bad hash")
    der = utils.encode_dss_signature(int.from_bytes(sig[:32], "big"), int.from_bytes(sig[32:], "big"))
    try:
        pub.verify(der, sha, ec.ECDSA(utils.Prehashed(hashes.SHA256())))
    except InvalidSignature:
        raise ValueError("bad signature") from None
    if image_version(image) != version:
        raise ValueError("version does not match the image")
    return version


def header_pub():
    """The public key compiled into the firmware."""
    text = PUB_H.read_text(encoding="utf-8")
    body = text[text.index("{") + 1:text.index("}")]
    raw = bytes(int(x, 16) for x in body.replace("\n", " ").split(",") if x.strip())
    return ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), raw)


def write_header(pub):
    raw = pub_bytes(pub)
    rows = [", ".join(f"0x{b:02x}" for b in raw[i:i + 13]) for i in range(0, len(raw), 13)]
    body = ",\n    ".join(rows)
    PUB_H.write_text(
        "// Firmware signing public key (P52): ECDSA P-256, uncompressed point (04 || X || Y).\n"
        "// Written by `python tools/sign_fw.py pubkey`; the private key is never in git (docs/ota.md).\n"
        "#pragma once\n#include <stdint.h>\n\n"
        f"static const uint8_t FW_PUBKEY[65] = {{\n    {body}}};\n",
        encoding="utf-8", newline="\n")
    print(f"wrote {PUB_H.relative_to(ROOT)}")


def self_test():
    image = bytearray(4096)
    image[0] = 0xE9
    struct.pack_into("<I", image, 32, APP_DESC_MAGIC)
    image[48:54] = b"9.9.99"
    image = bytes(image)
    key, other = ec.generate_private_key(ec.SECP256R1()), ec.generate_private_key(ec.SECP256R1())
    good = pack(image, key)
    assert check(good, key.public_key()) == "9.9.99", "good package"
    cases = {
        "bad signature": bytearray(good),
        "bad hash": bytearray(good),
    }
    off = 6 + good[5] + 4  # sha256 field
    cases["bad signature"][off + 32 + 5] ^= 1
    cases["bad hash"][-1] ^= 1
    for want, data in cases.items():
        try:
            check(bytes(data), key.public_key())
            sys.exit(f"self-test: {want} passed the check")
        except ValueError as e:
            assert str(e) == want, f"{want}: got {e}"
    try:
        check(good, other.public_key())
        sys.exit("self-test: wrong key passed the check")
    except ValueError as e:
        assert str(e) == "bad signature", e
    relabelled = good[:6] + b"9.9.98" + good[12:]
    try:
        check(relabelled, key.public_key())
        sys.exit("self-test: relabelled version passed the check")
    except ValueError as e:
        assert "version" in str(e), e
    print("sign_fw self-test: good, bad signature, bad hash, wrong key, relabelled version: ok")


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return
    cmd = argv[0]
    if cmd == "--self-test":
        self_test()
    elif cmd == "keygen":
        if KEY_FILE.exists():
            sys.exit(f"{KEY_FILE} exists; not replacing it (a new key needs one USB update)")
        KEY_FILE.parent.mkdir(parents=True, exist_ok=True)
        key = ec.generate_private_key(ec.SECP256R1())
        KEY_FILE.write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                               serialization.NoEncryption()))
        print(f"wrote {KEY_FILE}")
        write_header(key.public_key())
    elif cmd == "pubkey":
        write_header(load_key().public_key())
    elif cmd == "sign" and len(argv) == 3:
        key = load_key()
        if pub_bytes(key.public_key()) != pub_bytes(header_pub()):
            sys.exit(f"the key does not match {PUB_H.relative_to(ROOT)}")
        data = pack(Path(argv[1]).read_bytes(), key)
        Path(argv[2]).write_bytes(data)
        print(f"signed v{check(data, header_pub())}: {len(data)} B -> {argv[2]}")
    elif cmd == "verify" and len(argv) == 2:
        try:
            print(f"v{check(Path(argv[1]).read_bytes(), header_pub())}: ok")
        except ValueError as e:
            sys.exit(f"{argv[1]}: {e}")
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
