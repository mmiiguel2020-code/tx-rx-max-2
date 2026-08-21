# tx-rx-max-2 / esp32-live

Disparador MIDI universal de 23 botones (ESP32-S3).
TX inalámbrico (ESP-NOW) → RX USB MIDI. Sin dependencias de ningún DAW.

## Carpetas

| Carpeta | Contenido |
|---|---|
| `firmware/` | Código TX/RX (PlatformIO) |
| `docs/` | Mapa de botones, batería, esquemas |
| `tools/` | Identificar placa y flashear RX |

## Placas

- **TX**: botones + batería. ESP-NOW. MAC `DC:B4:D9:13:5F:F4`
- **RX**: USB al PC. Nombre MIDI: `ESP32-S3 MIDI Trigger`. MAC `FC:01:2C:CC:69:B8`

Al cambiar el mapa de botones, regrabar **las dos**.

## Botones → MIDI CC (canal 1)

| Grupo | Botones | CC | Comportamiento | LED |
|---|---|---|---|---|
| Excluyente A | 1–6 | CC 30–35 | Solo uno activo | Azul |
| Notas TX | 7–11 | FL C5 B4 C6 G6 A4 | Momentáneas On/Off | Verde |
| Pads RX | 24 GPIO | 12 notas 5ª/6ª + CC 81–92 | Notas momentáneas; CC latch (CC 88 momentáneo) | Cian |
| Excluyente D | 12–16 | CC 25, 70–73 | Solo uno activo | Cian |
| Toggle | 17–23 | CC 74–80 | Independientes | Morado |

Valor: **127** = encendido, **0** = apagado.

Batería (GPIO3 + divisor 100k): **no va por MIDI** (rompía MIDI Learn).
Ver carga en LED del TX (verde / amarillo / rojo cada ~10 s) o en Serial
del TX (`bat=…mV …%`). Pila baja: LED naranja en el RX. Detalle:
`docs/BATERIA_MIDI.txt`.

Detalle GPIO botones: `docs/MAPA_BOTONES.txt`

## Uso en cualquier DAW / host MIDI

El RX aparece como dispositivo MIDI USB estándar. En Ableton, Bitwig,
Reaper, Cubase, FL, etc.:

1. Activa **entrada** del dispositivo `ESP32-S3 MIDI Trigger`
2. Deja **salida** hacia esa placa **desactivada** (evita bucles)
3. Mapea CC a lo que quieras (MIDI Learn / Link)

No hace falta ningún script del host.

## Flashear

```powershell
cd firmware
pio run -e esp32s3_tx -t upload --upload-port COMx   # TX
python ..\tools\flashear_rx.py                        # RX (modo boot)
```

Tras flashear el RX, pulsa RESET. Windows debe mostrar el nuevo nombre MIDI.

## Herramientas

| Script | Uso |
|---|---|
| `tools/identificar_placa.py COMx` | Confirma TX por serie |
| `tools/flashear_rx.py` | Graba RX comprobando MAC |

Python con pyserial (el de PlatformIO vale):

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tools\identificar_placa.py COM4
```
