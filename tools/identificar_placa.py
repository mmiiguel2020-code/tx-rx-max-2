"""Identifica si un puerto COM es la placa TX (consola con tabla de botones).

Uso: python identificar_placa.py COM4 [segundos]

El TX imprime al arrancar la lista de GPIO y una linea por pulsacion.
El RX no expone consola: su USB nativo es el interfaz MIDI.
"""

import sys
import time

import serial

port = sys.argv[1] if len(sys.argv) > 1 else "COM4"
seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 6.0

try:
    ser = serial.Serial(port, 115200, timeout=0.2)
except Exception as e:
    print("NO SE PUDO ABRIR %s: %s" % (port, e))
    sys.exit(2)

with ser:
    # Reset por DTR/RTS para capturar el arranque
    ser.setDTR(False)
    ser.setRTS(True)
    time.sleep(0.15)
    ser.setRTS(False)
    time.sleep(0.15)
    ser.setDTR(True)

    end = time.time() + seconds
    buf = []
    while time.time() < end:
        chunk = ser.read(4096)
        if chunk:
            buf.append(chunk.decode("utf-8", "replace"))

text = "".join(buf)
print("----- salida de %s -----" % port)
print(text.strip() if text.strip() else "(sin datos)")
print("----- fin -----")

low = text.lower()
if "gpio" in low and "boton" in low:
    print("VEREDICTO: es la placa TX (consola de botones)")
elif text.strip():
    print("VEREDICTO: hay consola pero no es la tabla del TX")
else:
    print("VEREDICTO: sin consola (puede ser RX o placa sin ROLE_TX)")
