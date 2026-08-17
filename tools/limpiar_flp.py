"""Quita controllers MIDI/Remote del FLP y reescribe rutas de samples a Audio\\."""
from __future__ import annotations

import sys
from collections import Counter
from pathlib import Path

import pyflp

SRC = Path(r"C:\Users\MIGUEL\OneDrive2\Desktop\SESION NUEVA\CON CONTENIDO 18h39.flp")
DST = Path(r"C:\Users\MIGUEL\OneDrive2\Desktop\SESION NUEVA\LIVE LISTO.flp")
AUDIO = Path(r"C:\Users\MIGUEL\OneDrive2\Desktop\SESION NUEVA\Audio")


def main() -> int:
    print("parse:", SRC)
    project = pyflp.parse(SRC)

    # --- inventario ---
    channels = list(project.channels)
    print("channels:", len(channels))
    try:
        print("mixer inserts:", len(list(project.mixer)))
    except Exception as e:
        print("mixer:", e)

    # Controllers high-level
    removed_hi = 0
    try:
        ctrls = list(project.controllers)
        print("controllers:", len(ctrls))
        for c in list(ctrls):
            print(" ", type(c).__name__, getattr(c, "parameter", None), c)
            try:
                # EventTree remove if available
                if hasattr(c, "events"):
                    pass
            except Exception:
                pass
    except Exception as e:
        print("controllers list err:", e)

    # Contar eventos 226/227 (MIDI / Remote controller)
    ids = Counter(int(e.id) for e in project.events)
    print("MIDI ctrl events (226):", ids.get(226, 0))
    print("Remote ctrl events (227):", ids.get(227, 0))
    print("Formula (230):", ids.get(230, 0))

    # Borrar eventos de controller a bajo nivel
    # project.events es un EventTree / secuencia mutable en pyflp
    to_drop = {226, 227, 230}
    before = len(list(project.events))
    kept = []
    dropped = 0
    for e in list(project.events):
        eid = int(e.id)
        if eid in to_drop:
            dropped += 1
            # intentar remove oficial
            try:
                project.events.remove(e.id)  # type: ignore[arg-type]
            except Exception:
                try:
                    # algunos EventTree usan remove(id, pos)
                    project.__events__.remove(e)  # type: ignore[attr-defined]
                except Exception:
                    pass
        else:
            kept.append(e)
    print(f"events before={before} dropped_attempt={dropped}")

    # Si remove no funciono, reconstruir filtrando via API interna
    ids2 = Counter(int(e.id) for e in project.events)
    still = ids2.get(226, 0) + ids2.get(227, 0) + ids2.get(230, 0)
    print("still controller events:", still)

    if still:
        print("fallback: filtrar lista interna de eventos")
        # Acceso a la lista cruda
        raw = getattr(project, "_events", None) or getattr(project, "__dict__", {})
        # Buscar contenedor
        tree = project.events
        children = getattr(tree, "children", None) or getattr(tree, "_children", None) or getattr(tree, "events", None)
        print("tree type", type(tree), "children", type(children))
        if children is not None:
            # filtrar in-place
            if isinstance(children, list):
                new = [e for e in children if int(getattr(e, "id", -1)) not in to_drop]
                children[:] = new
                print("filtered list children ->", len(children))
            else:
                # intentar dict-like / EventTree
                try:
                    for eid in list(to_drop):
                        while True:
                            try:
                                tree.remove(eid)  # type: ignore[arg-type]
                            except Exception:
                                break
                except Exception as e:
                    print("tree.remove loop err", e)

    ids3 = Counter(int(e.id) for e in project.events)
    print(
        "after: 226=",
        ids3.get(226, 0),
        "227=",
        ids3.get(227, 0),
        "230=",
        ids3.get(230, 0),
    )

    # Reescribir sample paths a Audio\
    AUDIO.mkdir(parents=True, exist_ok=True)
    retargeted = 0
    missing = []
    for ch in project.channels:
        path = getattr(ch, "sample_path", None)
        if path is None:
            continue
        p = Path(str(path))
        leaf = p.name
        local = AUDIO / leaf
        if not local.exists() and p.exists():
            local.write_bytes(p.read_bytes())
        if local.exists():
            try:
                ch.sample_path = local  # type: ignore[assignment]
                retargeted += 1
                print("retarget", leaf)
            except Exception as e:
                print("cannot set sample_path", leaf, e)
        else:
            missing.append(str(path))

    print("retargeted:", retargeted, "missing:", len(missing))
    for m in missing:
        print("  MISS", m)

    print("save:", DST)
    pyflp.save(project, DST)
    print("OK bytes", DST.stat().st_size)
    return 0


if __name__ == "__main__":
    sys.exit(main())
