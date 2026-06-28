﻿# типовые проблемы apo

## apo пока не пишет

значит ни один реальный apo process callback не открыл shared memory и не вызвал cdm.

причины:

```text
- sysvad/swapapo не установлен
- установлен не тот inf
- apo не привязан к нужному endpoint
- cdm вызов не добавлен в process callback
- audiosrv не перезапущен
- gui и apo используют разные версии shared memory
```

## есть звук, но cdm не влияет

скорее всего, ты обрабатываешь копию буфера или input buffer, а не output buffer.

нужно обрабатывать именно тот buffer, который apo возвращает дальше в audio engine.

## нет звука после установки

нажми `b` в gui для bypass.

если bypass не помогает — проблема в apo glue/format, а не в dsp.

## краш audiodg.exe

почти всегда:

```text
- неверный pointer
- неверный frame_count/channel_count
- buffer не float32, а ты кастуешь его в float*
- аллокации/исключения/COM внутри realtime callback
```

в realtime callback нельзя делать тяжелую херню. только обработка буфера.

## щелчки

причины:

```text
- слишком резкие коэффициенты gain
- клиппинг
- несоответствие формата
- запись за границы буфера
```

## задержка

сам cdm core не добавляет lookahead. если появилась задержка:

```text
- большой shared-mode buffer
- virtual cable
- не тот path
- delay apo/sample path
- устройство работает через дополнительные enhancements
```
