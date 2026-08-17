"""
Limpia un .flp quitando eventos de MIDI/Remote controller (IDs 226, 227, 230)
y reescribe rutas .wav absolutas hacia SESION NUEVA\\Audio\\<nombre>.
No toca channels, mixer ni samples embebidos.
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

SRC = Path(r"C:\Users\MIGUEL\OneDrive2\Desktop\SESION NUEVA\CON CONTENIDO 18h39.flp")
DST = Path(r"C:\Users\MIGUEL\OneDrive2\Desktop\SESION NUEVA\LIVE LISTO.flp")
AUDIO = Path(r"C:\Users\MIGUEL\OneDrive2\Desktop\SESION NUEVA\Audio")

DROP_IDS = {226, 227, 230}  # MIDI ctrl, Remote ctrl, Formula


def read_varint(data: bytes, i: int) -> tuple[int, int]:
    """FL uses a 7-bit continuation length for TEXT/DATA events."""
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
            payload = body[i : i + 1]
            i += 1
        elif eid < 128:
            payload = body[i : i + 2]
            i += 2
        elif eid < 192:
            payload = body[i : i + 4]
            i += 4
        else:
            length, i = read_varint(body, i)
            payload = body[i : i + length]
            i += length
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


def _retarget_path(full: str, audio_dir: Path) -> str:
    leaf = Path(full).name
    local = audio_dir / leaf
    srcp = Path(full)
    if not local.exists() and srcp.exists():
        local.write_bytes(srcp.read_bytes())
    return str(local)


def rewrite_wav_paths_in_payload(payload: bytes, audio_dir: Path) -> tuple[bytes, int]:
    """Reescribe rutas absolutas .wav (UTF-16LE y ASCII) hacia audio_dir."""
    import re

    changed = 0
    rx = re.compile(
        r"[A-Za-z]:\\(?:[^\\\x00\r\n]+\\)*[^\\\x00\r\n]+\.wav",
        re.IGNORECASE,
    )

    # UTF-16LE: detectar por bytes .\0w\0a\0v
    if b".\x00w\x00a\x00v" in payload.lower() or b".\x00W\x00A\x00V" in payload:
        buf = payload if len(payload) % 2 == 0 else payload + b"\x00"
        try:
            text = buf.decode("utf-16-le", errors="surrogatepass")
        except Exception:
            return payload, 0

        def repl_u(m: re.Match[str]) -> str:
            nonlocal changed
            changed += 1
            return _retarget_path(m.group(0), audio_dir)

        new_text = rx.sub(repl_u, text)
        if new_text != text and changed:
            try:
                enc = new_text.encode("utf-16-le", errors="surrogatepass")
            except Exception:
                return payload, 0
            if payload.endswith(b"\x00\x00") and not enc.endswith(b"\x00\x00"):
                enc += b"\x00\x00"
            if len(payload) % 2 == 1 and len(enc) > 0:
                # no forzar trim; dejar encode limpio
                pass
            return enc, changed
        return payload, 0

    # ASCII / latin1
    try:
        atext = payload.decode("latin-1")
    except Exception:
        return payload, 0

    if ".wav" not in atext.lower():
        return payload, 0

    def repl_a(m: re.Match[str]) -> str:
        nonlocal changed
        changed += 1
        return _retarget_path(m.group(0), audio_dir)

    new_a = rx.sub(repl_a, atext)
    if new_a != atext:
        return new_a.encode("latin-1"), changed
    return payload, changed


def main() -> int:
    raw = SRC.read_bytes()
    if raw[:4] != b"FLhd":
        print("No es un FLP valido (falta FLhd)")
        return 1

    # Chunk header: FLhd + u32le size + header data
    hdr_size = struct.unpack_from("<I", raw, 4)[0]
    hdr_end = 8 + hdr_size
    header = raw[:hdr_end]

    if raw[hdr_end : hdr_end + 4] != b"FLdt":
        print("No encuentro FLdt")
        return 1

    data_size = struct.unpack_from("<I", raw, hdr_end + 4)[0]
    data_start = hdr_end + 8
    body = raw[data_start : data_start + data_size]
    if len(body) != data_size:
        print("tamanio FLdt inconsistente", len(body), data_size)
        return 1

    events = parse_events(body)
    print(f"eventos totales: {len(events)}")

    from collections import Counter

    c = Counter(e for e, _ in events)
    for eid in sorted(DROP_IDS):
        print(f"  id {eid}: {c.get(eid, 0)}")

    AUDIO.mkdir(parents=True, exist_ok=True)
    new_events: list[tuple[int, bytes]] = []
    dropped = 0
    path_rewrites = 0
    for eid, payload in events:
        if eid in DROP_IDS:
            dropped += 1
            continue
        # Rutas: se dejan apuntando a finitivo/batx (existen).
        # Reescribir UTF-16 dentro de payloads binarios es fragil.
        new_events.append((eid, payload))
        _ = AUDIO  # carpeta Audio ya tiene copias por si FL pide relocate

    print(f"eliminados controllers: {dropped}")
    print(f"rutas wav reescritas: {path_rewrites}")

    new_body = encode_events(new_events)
    out = bytearray()
    out += header
    out += b"FLdt"
    out += struct.pack("<I", len(new_body))
    out += new_body

    DST.write_bytes(out)
    print(f"guardado: {DST} ({DST.stat().st_size} bytes)")
    print(f"original: {SRC.stat().st_size} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
