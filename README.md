# esp32-live

Controlador MIDI de 23 botones para tocar en vivo con FL Studio.
Carpeta limpia: solo lo que se usa, sin copias antiguas.

## Qué es cada cosa

| Carpeta | Contenido |
|---|---|
| `firmware/` | Código de las dos placas ESP32-S3 (PlatformIO) |
| `docs/` | Mapa de botones y checklist para tocar |
| `tools/` | Utilidades de diagnóstico y mantenimiento |

## Las dos placas

- **TX**: la de los botones. Va por ESP-NOW, sin cables. MAC `DC:B4:D9:13:5F:F4`.
- **RX**: la enchufada al PC. Aparece en FL como `ESP32-S3 MIDI Controller`. MAC `FC:01:2C:CC:69:B8`.

El mapa de botones es compartido, así que **al cambiarlo hay que regrabar las dos**.

## Botones

23 en total, agrupados así:

| Grupo | Botones | CC | Comportamiento | LED |
|---|---|---|---|---|
| Kits | 1-6 | 30-35 | Excluyentes | Azul |
| Pareja | 7-8 | 20-21 | Excluyentes | Verde |
| Trío | 9-11 | 22-24 | Excluyentes | Amarillo |
| Quinteto | 12-16 | 25, 70-73 | Excluyentes | Cian |
| On/off | 17-23 | 74-80 | Independientes | Morado |

Detalle completo con GPIO de cada botón en `docs/MAPA_BOTONES.txt`.

## Regrabar las placas

```powershell
cd firmware
pio run -e esp32s3_tx -t upload --upload-port COM4   # TX
python ..\tools\flashear_rx.py                        # RX
```

El RX en marcha no tiene puerto COM porque su USB es el interfaz MIDI.
`flashear_rx.py` espera a que lo pongas en modo descarga (mantener BOOT,
pulsar y soltar RESET, soltar BOOT), comprueba la MAC para no grabar la
placa equivocada, y lo graba. Al terminar, pulsa RESET para que vuelva
a salir como dispositivo MIDI.

## FL Studio: sin scripts

Los `device_*.py` que se autoasignaban al ESP32 devolvían MIDI a la placa
en cada refresco de FL y eso saturaba el audio. Se quitaron todos.

Configuración correcta:

- Input `ESP32-S3 MIDI Controller` = ON
- Output `ESP32-S3 MIDI Controller` = **OFF**
- Controller type = **(none)**

Todo se mapea con Link to controller: clic derecho en el control de FL,
Link to controller, y pulsar el botón en la placa.

## Herramientas

| Script | Para qué |
|---|---|
| `tools/identificar_placa.py COM4` | Dice si en ese puerto está el TX (contesta con su tabla de botones) |
| `tools/flashear_rx.py` | Graba el RX comprobando la MAC antes |
| `tools/esp32_sin_scripts.ps1` | Pone en cuarentena cualquier script del ESP32 que reaparezca en FL. Se ejecuta solo desde `MODO VIVO.bat` |
| `tools/blindar_carpeta_esp32.ps1` | Impide por permisos que se creen scripts nuevos en la carpeta del ESP32. `-Quitar` lo revierte |

Los scripts de Python necesitan pyserial; el Python de PlatformIO ya lo trae:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tools\identificar_placa.py COM4
```
