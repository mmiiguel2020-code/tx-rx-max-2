CONTINUAR — instalacion limpia MIDI universal (19 ago 2026)
===========================================================

OBJETIVO
--------
ESP32 = disparador MIDI universal (CC). Sin scripts de host.
Sin acoplamiento a FL Studio en el repo.

FIRMWARE
--------
  Repo: https://github.com/mmiiguel2020-code/tx-rx-max-2
  Local: Documents\esp32-live
  Producto USB RX: "ESP32-S3 MIDI Trigger"  (hay que reflash RX)
  TX MAC dc:b4:d9:13:5f:f4
  RX MAC fc:01:2c:cc:69:b8
  Bateria GPIO3 → CC 110/111/112

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

LIMPIEZA HECHA
--------------
  Quitados del repo: tools FL (limpiar_flp, blindar, esp32_sin_scripts)
  Docs FL checklist vivo eliminado
  Scripts Grupos5/Cycle5 fuera de Hardware FL
