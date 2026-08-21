# -*- coding: utf-8 -*-
"""RX pad CC 88 momentaneo -> Play/Stop en FL (Space).

Pulsar CC88=127 -> Space (Play)
Soltar CC88=0   -> Space (Stop)
Envia Space DIRECTO a la ventana de FL (no hace falta foco).
"""

from __future__ import annotations

import ctypes
import sys
import time
from ctypes import wintypes

import mido

CC_PLAY = 88
PORT_SUBSTR = "MIDI Trigger"  # preferir Trigger; fallback ESP32
DEBOUNCE_S = 0.04

FL_CLASSES = ("TFruityLoopsMainForm",)
FL_EXES = ("FL64.exe", "FL.exe")

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32

WM_KEYDOWN = 0x0100
WM_KEYUP = 0x0101
VK_SPACE = 0x20


def _find_port() -> str:
    names = mido.get_input_names()
    for name in names:
        if PORT_SUBSTR.lower() in name.lower():
            return name
    for name in names:
        if "esp32" in name.lower():
            return name
    raise SystemExit(
        "No hay puerto MIDI ESP32.\n"
        f"Puertos: {names or '(ninguno)'}\n"
        "Si FL tiene el MIDI abierto en exclusiva, cierra FL un momento "
        "o desactiva el input del ESP32 en Options > MIDI, prueba el puente, "
        "luego reactiva."
    )


def _enum_fl_hwnds() -> list[int]:
    found: list[int] = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, wintypes.HWND, wintypes.LPARAM)
    def _cb(hwnd, _lparam):
        if not user32.IsWindowVisible(hwnd):
            return True
        buf = ctypes.create_unicode_buffer(256)
        user32.GetClassNameW(hwnd, buf, 256)
        if buf.value in FL_CLASSES:
            found.append(hwnd)
            return True
        pid = ctypes.c_ulong()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        h = kernel32.OpenProcess(0x1000, False, pid.value)
        if not h:
            return True
        try:
            size = ctypes.c_ulong(260)
            path = ctypes.create_unicode_buffer(260)
            if kernel32.QueryFullProcessImageNameW(h, 0, path, ctypes.byref(size)):
                if path.value.split("\\")[-1] in FL_EXES:
                    found.append(hwnd)
        finally:
            kernel32.CloseHandle(h)
        return True

    user32.EnumWindows(_cb, 0)
    return found


def _tap_space_to_fl() -> bool:
    hwnds = _enum_fl_hwnds()
    if not hwnds:
        print("  ! FL no encontrado (abre FL Studio)")
        return False
    for hwnd in hwnds:
        # PostMessage llega aunque FL no tenga el foco
        user32.PostMessageW(hwnd, WM_KEYDOWN, VK_SPACE, 0)
        user32.PostMessageW(hwnd, WM_KEYUP, VK_SPACE, 0)
    return True


def main() -> None:
    port_name = _find_port()
    print(f"Escuchando: {port_name}")
    print(f"CC {CC_PLAY}: pulsar=PLAY  soltar=STOP  -> ventana FL")
    print("Ctrl+C para salir")
    print("(Pulsa GPIO40 ahora; debe salir PLAY/STOP aqui)")

    pressed = False
    last_edge = 0.0

    with mido.open_input(port_name) as port:
        for msg in port:
            if msg.type != "control_change":
                continue
            # debug: cualquier CC del RX alto
            if msg.control >= 81:
                print(f"  midi {msg}")

            if msg.control != CC_PLAY:
                continue

            down = msg.value >= 64
            now = time.monotonic()
            if down == pressed:
                continue
            if now - last_edge < DEBOUNCE_S:
                continue

            pressed = down
            last_edge = now
            ok = _tap_space_to_fl()
            if ok:
                print("PLAY (mantener)" if down else "STOP (soltar)")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
