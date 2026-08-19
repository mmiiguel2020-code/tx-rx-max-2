"""Espera a que la placa RX entre en modo descarga y la graba.

Uso: python flashear_rx.py [segundos_espera]

El RX en marcha no tiene puerto serie: su USB nativo es el interfaz MIDI.
Para grabarlo hay que ponerlo en modo descarga:
    manten pulsado BOOT -> pulsa y suelta RESET -> suelta BOOT

Este script vigila los puertos, y en cuanto aparece uno nuevo comprueba
la MAC. Si es la del TX aborta, para no grabar la placa equivocada.
"""

import glob
import os
import subprocess
import sys
import time

from serial.tools import list_ports

PROYECTO = r"C:\Users\MIGUEL\Documents\esp32-live\firmware"
PIO = os.path.expanduser(r"~\.platformio\penv\Scripts\platformio.exe")
PYTHON = sys.executable
MAC_TX = "dc:b4:d9:13:5f:f4"

espera = float(sys.argv[1]) if len(sys.argv) > 1 else 120.0


def puertos():
    return {p.device for p in list_ports.comports()}


def esptool_path():
    patron = os.path.expanduser(r"~\.platformio\packages\tool-esptoolpy*\esptool.py")
    encontrados = sorted(glob.glob(patron))
    return encontrados[-1] if encontrados else None


def leer_mac(port):
    tool = esptool_path()
    if not tool:
        return None
    try:
        out = subprocess.run(
            [PYTHON, tool, "--port", port, "--no-stub", "read_mac"],
            capture_output=True, text=True, timeout=40,
        ).stdout
    except Exception as e:
        print("No se pudo leer MAC:", e)
        return None
    for linea in out.splitlines():
        if linea.upper().startswith("MAC:"):
            return linea.split(":", 1)[1].strip().lower()
    return None


iniciales = puertos()
print("Puertos ahora:", ", ".join(sorted(iniciales)) or "(ninguno)")
print()
print("PON LA PLACA RX EN MODO DESCARGA:")
print("  1) manten pulsado BOOT")
print("  2) pulsa y suelta RESET")
print("  3) suelta BOOT")
print()
print("Esperando puerto nuevo (%.0f s)..." % espera)

fin = time.time() + espera
nuevo = None
while time.time() < fin:
    actuales = puertos()
    extra = actuales - iniciales
    if extra:
        nuevo = sorted(extra)[0]
        break
    time.sleep(0.5)

if not nuevo:
    print("TIMEOUT: no aparecio ningun puerto nuevo.")
    sys.exit(1)

print("Puerto nuevo detectado:", nuevo)
time.sleep(1.5)

mac = leer_mac(nuevo)
print("MAC leida:", mac or "(desconocida)")
if mac == MAC_TX:
    print("ABORTADO: esa es la placa TX, no la RX. No se graba nada.")
    sys.exit(2)

print("Grabando firmware RX en", nuevo)
r = subprocess.run(
    [PIO, "run", "-e", "esp32s3_rx", "-t", "upload", "--upload-port", nuevo],
    cwd=PROYECTO,
)
sys.exit(r.returncode)
