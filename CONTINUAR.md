CONTINUAR — 19 ago 2026 (guardado local ~4:57)
===============================================

FIRMWARE
--------
TX y RX flasheados con firmware v4 (bateria GPIO3).
TX MAC dc:b4:d9:13:5f:f4 (a bateria)
RX MAC fc:01:2c:cc:69:b8 (USB PC = ESP32-S3 MIDI Controller)

MIDI bateria canal 1:
  CC 110 = % 0-127
  CC 111 = cargando (0 por ahora; CHRG no cableado)
  CC 112 = alerta <20%

Hardware TX:
  B+ --100k-- GPIO3 --100k-- GND  (YA instalado segun chat 4:05)
  Alimentacion: TP4056+boost ~5V → switch → pin 5V
  Placa del humo: NO usar. MT3608 = repuesto.

FL
--
  Audio: UMC ASIO Driver
  Input ESP32 ON, Output OFF, type (none)
  Link to controller → CC110 (y 111/112 si quieres)
  Proyecto reciente: Desktop\batx 18ago\batx 18 ago 2.flp

DOCS / RECUPERACION
-------------------
  docs\BATERIA_MIDI.txt
  docs\BATERIA_TX.txt
  Desktop\RECUPERADO_chat_firmware_bateria_19ago.txt  ← chat perdido 3:54-4:19
  Desktop\NOTA_CONVERSACION_ESP32_FL_18ago.txt

GIT
---
  https://github.com/mmiiguel2020-code/tx-rx-max-2
  Local dirty: main.cpp + CONTINUAR + docs bateria (no push aun)

PENDIENTE
---------
  [ ] Probar CC110 en FL (Link to controller)
  [ ] Commit/push firmware v4 a tx-rx-max-2
  [ ] GPIO46 CHRG cuando se cablee
  [ ] 5 pilas en paralelo cuando las tenga
