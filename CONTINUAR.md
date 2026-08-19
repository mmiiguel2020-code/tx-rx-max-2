CONTINUAR — MIDI universal OK (19 ago 2026 ~5:25)
=================================================

ESTADO
------
TX y RX reflasheados y funcionando (ambas LEDs parpadean).
RX USB aparece como: ESP32-S3 MIDI Trigger

FIRMWARE
--------
  Repo: https://github.com/mmiiguel2020-code/tx-rx-max-2
  Local: Documents\esp32-live
  TX MAC dc:b4:d9:13:5f:f4
  RX MAC fc:01:2c:cc:69:b8
  Bateria GPIO3 → MIDI CC 110 (%), 111 (cargando), 112 (low <20%)

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
