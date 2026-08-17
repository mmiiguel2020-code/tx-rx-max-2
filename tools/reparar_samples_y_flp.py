"""Diagnostica WAVs del proyecto y regenera LIVE LISTO desde backup limpio."""
from __future__ import annotations

import hashlib
import re
import shutil
import struct
import sys
from collections import Counter
from pathlib import Path

SESION = Path(r"C:\Users\MIGUEL\OneDrive2\Desktop\SESION NUEVA")
SRC_FLP = SESION / "CON CONTENIDO 18h39.flp"
OUT_FLP = SESION / "LIVE LISTO.flp"
AUDIO = SESION / "Audio"
BACKUP_ORIG = Path(
    r"C:\Users\MIGUEL\OneDrive2\Desktop\finitivo 15ago\Backup\finitivo 15ago (overwritten at 18h39).flp"
)
DROP_IDS = {226, 227, 230}


def wav_ok(path: Path) -> tuple[bool, str, int]:
    if not path.exists():
        return False, "missing", 0
    size = path.stat().st_size
    if size < 44:
        return False, "too_small", size
    with path.open("rb") as f:
        hdr = f.read(12)
    if len(hdr) < 12:
        return False, "unreadable", size
    if hdr[:4] != b"RIFF" or hdr[8:12] != b"WAVE":
        return False, f"bad_header:{hdr[:4]!r}/{hdr[8:12]!r}", size
    # chequeo basico de chunk size
    riff_size = struct.unpack_from("<I", hdr, 4)[0]
    if riff_size + 8 > size + 8:  # tolerante
        pass
    if riff_size + 8 < size * 0.5 and size > 1000:
        # a veces padding; no marcar corrupto solo por esto
        pass
    return True, "ok", size


def md5(path: Path) -> str:
    h = hashlib.md5()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def find_paths(flp: Path) -> list[str]:
    raw = flp.read_bytes()
    u16 = raw.decode("utf-16-le", errors="ignore")
    rx = re.compile(r"[A-Za-z]:\\[^\x00-\x1F\"<>|*?]{3,240}?\.wav", re.I)
    return sorted(set(rx.findall(u16)))


def read_varint(data: bytes, i: int) -> tuple[int, int]:
    value = 0
    shift = 0
    while True:
        b = data[i]
        i += 1
        value |= (b & 0x7F) << shift
        if not (b & 0x80):
            break
        shift += 7
    return value, i


def write_varint(n: int) -> bytes:
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            break
    return bytes(out)


def parse_events(body: bytes) -> list[tuple[int, bytes]]:
    events: list[tuple[int, bytes]] = []
    i = 0
    n = len(body)
    while i < n:
        eid = body[i]
        i += 1
        if eid < 64:
            payload, i = body[i : i + 1], i + 1
        elif eid < 128:
            payload, i = body[i : i + 2], i + 2
        elif eid < 192:
            payload, i = body[i : i + 4], i + 4
        else:
            length, i = read_varint(body, i)
            payload, i = body[i : i + length], i + length
        events.append((eid, payload))
    return events


def encode_events(events: list[tuple[int, bytes]]) -> bytes:
    out = bytearray()
    for eid, payload in events:
        out.append(eid)
        if eid < 64:
            out += payload[:1].ljust(1, b"\x00")
        elif eid < 128:
            out += payload[:2].ljust(2, b"\x00")
        elif eid < 192:
            out += payload[:4].ljust(4, b"\x00")
        else:
            out += write_varint(len(payload))
            out += payload
    return bytes(out)


def strip_controllers(src: Path, dst: Path) -> None:
    raw = src.read_bytes()
    hdr_size = struct.unpack_from("<I", raw, 4)[0]
    hdr_end = 8 + hdr_size
    data_size = struct.unpack_from("<I", raw, hdr_end + 4)[0]
    body = raw[hdr_end + 8 : hdr_end + 8 + data_size]
    events = parse_events(body)
    c = Counter(e for e, _ in events)
    print("controllers in src:", {i: c.get(i, 0) for i in DROP_IDS})
    kept = [(e, p) for e, p in events if e not in DROP_IDS]
    new_body = encode_events(kept)
    out = bytearray()
    out += raw[:hdr_end]
    out += b"FLdt"
    out += struct.pack("<I", len(new_body))
    out += new_body
    dst.write_bytes(out)
    print(f"wrote {dst} ({dst.stat().st_size}) dropped {len(events) - len(kept)}")


def main() -> int:
    AUDIO.mkdir(parents=True, exist_ok=True)

    # Asegurar fuente flp
    if not SRC_FLP.exists() and BACKUP_ORIG.exists():
        shutil.copy2(BACKUP_ORIG, SRC_FLP)

    flp_for_paths = SRC_FLP if SRC_FLP.exists() else OUT_FLP
    paths = find_paths(flp_for_paths)
    print(f"referenced wavs: {len(paths)}")

    bad = []
    fixed = 0
    for p in paths:
        path = Path(p)
        leaf = path.name
        local = AUDIO / leaf
        ok, reason, size = wav_ok(path)
        print(f"[{'OK' if ok else 'BAD'}] {reason:12} {size:10}  {path}")
        if not ok:
            bad.append((path, reason))
            # buscar reemplazo
            candidates = [
                local,
                Path(r"C:\Users\MIGUEL\OneDrive2\Desktop\finitivo 15ago") / leaf,
                Path(r"C:\Users\MIGUEL\OneDrive2\Desktop\LIVE 16ago") / leaf,
                Path(r"C:\Users\MIGUEL\OneDrive2\Desktop\batx 14ago\Audio") / leaf,
                Path(r"C:\Users\MIGUEL\OneDrive2\Desktop\finitivo 15ago\Backup") / leaf,
            ]
            replaced = False
            for cand in candidates:
                cok, _, _ = wav_ok(cand)
                if cok and cand.resolve() != path.resolve():
                    if path.parent.exists() or True:
                        # copiar bueno a Audio y tambien al path original si escribible
                        shutil.copy2(cand, local)
                        try:
                            path.parent.mkdir(parents=True, exist_ok=True)
                            shutil.copy2(cand, path)
                        except Exception as e:
                            print(f"  no pude sobrescribir origen: {e}")
                        print(f"  FIXED from {cand}")
                        fixed += 1
                        replaced = True
                        break
            if not replaced:
                print(f"  SIN REEMPLAZO BUENO para {leaf}")
        else:
            # asegurar copia buena en Audio
            if (not local.exists()) or md5(local) != md5(path):
                shutil.copy2(path, local)

    print("--- Audio folder scan ---")
    for w in sorted(AUDIO.glob("*.wav")):
        ok, reason, size = wav_ok(w)
        if not ok:
            print(f"AUDIO BAD {reason} {size} {w.name}")

    print("--- regenerate LIVE LISTO from CON CONTENIDO (strip links only) ---")
    strip_controllers(SRC_FLP, OUT_FLP)

    # verificar paths siguen resolviendo
    still_bad = []
    for p in find_paths(OUT_FLP):
        ok, reason, _ = wav_ok(Path(p))
        if not ok:
            still_bad.append((p, reason))
    print(f"fixed_copies={fixed} still_bad_refs={len(still_bad)}")
    for p, r in still_bad:
        print(" STILL", r, p)
    return 0 if not still_bad else 2


if __name__ == "__main__":
    sys.exit(main())
