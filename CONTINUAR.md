CONTINUAR — MIDI universal OK (19 ago 2026 ~6:03)
=================================================

ESTADO
------
TX y RX reflasheados y funcionando.
RX USB: ESP32-S3 MIDI Trigger
Bateria sin CC MIDI (Learn limpio); carga por LED/Serial.

FIRMWARE
--------
  Repo: https://github.com/mmiiguel2020-code/tx-rx-max-2
  Local: Documents\esp32-live
  TX MAC dc:b4:d9:13:5f:f4
  RX MAC fc:01:2c:cc:69:b8
  Bateria GPIO3 → LED TX (verde/amarillo/rojo) + Serial; NO MIDI CC
  (CC 110 rompia Link to controller / MIDI Learn)

HOST MIDI (cualquier DAW)
-------------------------
  Input del ESP32 = ON
  Output hacia ESP32 = OFF
  Sin controller scripts
  MIDI Learn / mapear CC

ALIMENTACION TX
---------------
  TP4056+boost ~5V → switch → pin 5V
  B+ --100k-- GPIO3 --100k-- GND
  docs\BATERIA_TX.txt / SOLDAR_PASO_A_PASO.txt

FLASH
-----
  RX: python tools\flashear_rx.py  (BOOT→RESET→suelta BOOT)
  TX: pio run -e esp32s3_tx -t upload --upload-port COMx

PENDIENTE
---------
  [ ] Probar CC110 en el DAW (MIDI Learn)
  [ ] GPIO46 CHRG cuando se cablee
  [ ] 5 pilas en paralelo cuando las tenga
