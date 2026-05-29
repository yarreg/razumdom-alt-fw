# Razumdom alternative firmware

Альтернативная прошивка для устройств Razumdom DDM845R и DDL84R:

| Прибор | Flash | Нагрузка | DI |
|---|---|---|---|
| **DDM845R** | 64 KiB | 4 × TRIAC, 220 V | DI0 (GPIO) + DI1–DI8 (MCP3208 ADC) |
| **DDL84R** | 32 KiB | 4 × LED PWM, 12–48 V | DI0 (GPIO) + DI1–DI8 (GPIO) |

Используемый стек: C11, libopencm3, GNU Make, без HAL и RTOS.

## Зачем эта прошивка

Штатная прошивка позволяет управлять диммерами, но часть практических настроек
либо требует ручной работы с регистрами, либо не дает удобной модели для
разных световых зон. Эта прошивка добавляет:

- для TRIAC LED-светильников, например "Центрсвет", можно задать точный
  полезный диапазон диммирования `min_level..max_level` и выходную кривую
  `output_curve`: проценты Modbus остаются `1..100`, а драйвер распределяет
  их внутри реально видимого диапазона яркости;
- независимые `min_level` и `max_level` для каждого канала: можно исключить
  неустойчивую область внизу и световую полку вверху для конкретного
  светильника;
- `night_level` для кнопки: нижнее нажатие выключенной клавиши может включать
  канал или группу на отдельной ночной яркости;
- автосвет для внешней автоматики, например Home Assistant: контроллер может
  временно включить свет от датчика присутствия на минимальную яркость, не
  перезаписывая запомненную ручную яркость; если пользователь вручную выключил
  такой свет, повторное авто-включение подавляется на настраиваемое время;
- настраиваемые `fade_on_ms` и `fade_off_ms` для каждого канала в диапазоне
  `0..5000` мс; они применяются одинаково к управлению с кнопок и через
  Modbus;
- настраиваемую скорость ручного диммирования при удержании кнопки
  (`hold_step` и `hold_period_ms`);
- привязки кнопок к отдельным каналам или ко всем четырем каналам сразу,
  например для RGBW-ленты;
- групповое включение, выключение и диммирование RGBW-ленты одной кнопкой
  или одной Modbus-командой с синхронным изменением четырех каналов;
- декларативную YAML-конфигурацию с записью, чтением и проверкой через Modbus,
  вместо ручной записи набора регистров;
- для DDM845R автоматическую оценку периода zero-cross: рабочий тайминг
  TRIAC-диммирования определяется драйвером и не требует отдельного
  установочного регистра;
- встроенный bootloader для обновления application по Modbus RTU как в устройствах WirenBoard.

## Flash layout

### DDM845R (64 KiB)

```text
0x08000000..0x08001FFF  bootloader, 8 KiB
0x08002000..0x0800F7FF  application, до 54 KiB
0x0800F800..0x0800FBFF  config slot 0, flash page 62, 1 KiB
0x0800FC00..0x0800FFFF  config slot 1, flash page 63, 1 KiB
```

### DDL84R (32 KiB)

```text
0x08000000..0x08001FFF  bootloader, 8 KiB
0x08002000..0x080077FF  application, до 22 KiB
0x08007800..0x08007BFF  config slot 0, flash page 30, 1 KiB
0x08007C00..0x08007FFF  config slot 1, flash page 31, 1 KiB
```

Linker scripts: `linker/<board>/bootloader.ld` и `linker/<board>/app.ld`. Сборка падает с `ASSERT`, если размер выходит за пределы.

Конфигурация обоих типов устройств хранится только во внутренней flash MCU:
два слота по 1 KiB с `sequence` и CRC32. При сохранении пишется следующий
слот, при загрузке выбирается валидный слот с большим `sequence`. Если оба
слота пустые или повреждены, application применяет заводские defaults; при
возврате из bootloader дополнительно использует `boot_request`, чтобы временно
сохранить текущий Modbus-адрес и порт для первичной записи YAML-конфигурации.

Application image header находится по offset `0x180` от начала application (`0x08002180`). Post-build tool `tools/patch_image.py` заполняет `image_size`, `image_crc32`, `build_version` и `device_id` в бинарном образе.

## Сборка

`libopencm3` лежит локально в `thirdparty/libopencm3`.

Целевой прибор выбирается переменной `BOARD` (по умолчанию `ddm845r`). `make` без цели выводит help.
Для сборки application обязательно указывать `BUILD_VERSION`, чтобы не получить новый binary со старым номером версии:

```sh
make BOARD=ddm845r BUILD_VERSION=0x00010005 app
make BOARD=ddl84r  BUILD_VERSION=0x00010005 size
```

Bootloader попадает в `build/<board>/bootloader/`, а versioned application image - в
`build/<board>/app/<BUILD_VERSION>/`. Это исключает повторное использование object-файлов,
собранных с другим номером версии.

Основные цели:

```sh
make help
make all [BOARD=...] BUILD_VERSION=0x00010005
make bootloader [BOARD=...]
make app [BOARD=...] BUILD_VERSION=0x00010005
make size [BOARD=...] BUILD_VERSION=0x00010005
make flash-bootloader [BOARD=...]
make flash-app [BOARD=...] BUILD_VERSION=0x00010005
make erase
make clean [BOARD=...]        # удалить build/<board>/
```

По умолчанию OpenOCD использует `interface/cmsis-dap.cfg` + `target/stm32f1x.cfg`:

```sh
make flash-app BOARD=ddl84r BUILD_VERSION=0x00010005 OPENOCD_INTERFACE=interface/stlink.cfg
```

## Защита от перекрёстной прошивки

В image header хранится `device_id` (DDM845R = 1, DDL84R = 2). Bootloader проверяет `device_id` при `verify` и отклоняет прошивку от другого прибора с ошибкой `UPDATE_STATUS_ERR_IMAGE (104)`.

## Аппаратная карта

### DDM845R

| Функция | Pin | Периферия | Комментарий |
|---|---:|---|---|
| RS-485 TX | PB6 | USART1 remap | Modbus RTU |
| RS-485 RX | PB7 | USART1 remap | Modbus RTU |
| RS-485 DE | PC13 | GPIO output | high = transmit |
| Zero-cross | PA0 | EXTI0 falling | сброс таймера полупериода |
| Output CH1 | PB1 | TIM3_CH4 | HR40 |
| Output CH2 | PB0 | TIM3_CH3 | HR41 |
| Output CH3 | PA7 | TIM3_CH2 | HR42 |
| Output CH4 | PA6 | TIM3_CH1 | HR43 |
| DI0 | PB13 | GPIO pull-up | active-low, кнопка на плате |
| DI1–DI8 | PA8 CS + PB3/4/5 | SPI1 remap + MCP3208 | active по ADC threshold |
| External EEPROM footprint | PB10/PB11 | I2C2, addr 0x50 | не используется прошивкой |
| Startup control pulse | PA5 | GPIO output | high ~100 ms; connected circuit unconfirmed |
| Status LED | PB12 | GPIO output | |

### DDL84R

| Функция | Pin | Периферия | Комментарий |
|---|---:|---|---|
| RS-485 TX | PA9 | USART1 | Modbus RTU |
| RS-485 RX | PA10 | USART1 | Modbus RTU |
| RS-485 DE | PA8 | GPIO output | high = transmit |
| RS-485 /RE | PB11 | GPIO output | через инвертор, high → /RE=low |
| Output CH1 | PA6 | TIM3_CH1 | HR40 |
| Output CH2 | PA7 | TIM3_CH2 | HR41 |
| Output CH3 | PB0 | TIM3_CH3 | HR42 |
| Output CH4 | PB1 | TIM3_CH4 | HR43 |
| DI0 | PA0 | GPIO pull-up | active-low |
| DI1–DI4 | PA2/PA3/PA4/PA5 | GPIO pull-up | active-low |
| DI5–DI8 | PB8/PB9/PB13/PB14 | GPIO pull-up | active-low |

## Modbus

Базовые параметры по умолчанию:

```text
slave address: 34
baud:          57600
format:        8N2
```

Application должна использовать адрес из `HR0` и формат из `HR1`. Bootloader наследует их через backup registers при штатном переходе из application. Если после обновления application сохраненная конфигурация не подходит к новой структуре, application временно стартует на адресе/параметрах, с которыми она была переведена в bootloader; это дает записать новую YAML-конфигурацию без отката на общий default address. При аварийном входе без backup request используется fallback `34`, `57600 8N2`.

Broadcast address `0` игнорируется, чтобы случайно не перевести всю шину в bootloader.

## Основные holding registers

| Register | Назначение | Диапазон | Persistent |
|---:|---|---|---|
| HR0 | Modbus slave address | 1..247, default 34 | да |
| HR1 | Modbus port config | default 57600 8N2 | да |
| HR40..HR43 | уровень CH1..CH4 | 0..1023 | нет |
| HR45..HR48 | уровень CH1..CH4 в процентах | 0..100 | нет |
| HR49..HR52 | on/off CH1..CH4 | 0 off, 1 on | нет |
| **HR53** | группа: уровень всех CH1..CH4 | 0..1023 | нет |
| **HR54** | группа: percent всех CH1..CH4 | 0..100 | нет |
| **HR55** | группа: on/off всех CH1..CH4 | 0 off, 1 on | нет |
| HR60..HR63 | автосвет: уровень CH1..CH4 | 0..1023 | нет |
| HR64..HR67 | автосвет: percent CH1..CH4 | 0..100 | нет |
| HR9000 | application version | uint16 | нет |
| HR9001 | bootloader version | uint16 | нет |
| HR9002 | device mode | 0 application, 1 bootloader | нет |
| HR9003..HR9004 | application build version hi/lo | uint32 | нет |

HR53–HR55 — групповые регистры. `HR53` задает общий `level`, `HR54` — общий `percent`, `HR55` — общий `on/off`. Все три записи идут через каналовый fade-профиль: запись `0` выключает все каналы, а ненулевые значения применяются к каждому каналу по его настройкам. Чтение `HR53/HR54` возвращает максимум среди всех каналов (0 если все выключены). В RGBW-режиме и сразу после группового write все каналы равны — читается то же значение, что было записано. Чтение `HR55` возвращает `1` только если все 4 канала включены, иначе `0`.

HR60–HR67 — временное управление автосветом для внешней автоматики. Запись
ненулевого значения в `HR60..HR63` или `HR64..HR67` включает канал, только если
он сейчас выключен или уже находится под управлением автосвета. Если канал
включен обычной кнопкой или обычными HR40..HR55, auto-запись игнорируется.
Запись `0` снимает автосвет и выключает канал только если он все еще активен;
после ручного вмешательства такая запись становится no-op. Auto-уровни не
обновляют `last_nonzero_level`, поэтому ночное включение на 1..5% не портит
следующее обычное включение. Если пользователь вручную выключил канал, который
был включен автосветом, повторное auto-включение игнорируется в течение
`HR5008` секунд; запись auto `0` сбрасывает эту задержку.

`hold_step` и `hold_period_ms` задают скорость диммирования при удержании кнопки. `default_level` и `night_level` задаются в логической шкале `0..1023`; проценты `HR45..HR48` и `HR54` тоже переводятся в эту шкалу. `min_level`, `max_level` и `output_curve` описывают только физическое отображение выхода: для DDM845R это фазовый уровень TRIAC, для DDL84R - duty PWM. Кнопки и обычные Modbus-записи меняют один и тот же runtime-уровень канала через общий channel API; auto-регистры используют отдельный временный режим поверх того же выходного слоя. Board-specific dimming применяет `fade_ms`, кривую и физический диапазон.

Поведение процентов:

- если канал выключен, `HR45..HR48` читаются как `0`;
- `HR49=0` выключает канал;
- `HR49=1` включает канал на логический `1023`, если текущий runtime level равен `0`, иначе повторно применяет текущий level.

## Конфигурация входов и кнопок

Глобальные регистры:

| Register | Назначение | Default |
|---:|---|---:|
| HR5000 | command: 0 none, 1 save, 2 reload, 3 factory defaults | 0 |
| HR5001 | config status | 0 |
| HR5002 | binding count | factory |
| HR5003 | DI ADC active threshold (DDM845R MCP3208; DDL84R ignores it) | 1000 |
| HR5004 | debounce ms | 30 |
| HR5005 | short press min ms | 50 |
| HR5006 | short press max ms | 700 |
| HR5007 | long press threshold ms | 800 |
| HR5008 | auto light suppress after manual off, s | 15 |

Профили 4 каналов, stride 16:

```text
base = HR5010 + (channel - 1) * 16
```

| Offset | Назначение |
|---:|---|
| +0 | min_level |
| +1 | max_level |
| +2 | default_level |
| +3 | night_level, 0 = ночной режим выключен |
| +4 | fade_on_ms |
| +5 | fade_off_ms |
| +6 | output_curve (см. таблицу ниже) |
| +7..+15 | reserved, must be 0 |

Кривые `output_curve`:

| Значение | Кривая | Когда использовать |
|---:|---|---|
| 0 | linear | Базовое линейное отображение логического уровня в физический выход |
| 1 | gamma 0.5 | TRIAC LED с медленным разгоном яркости внизу диапазона |
| 2 | gamma 0.7 | Более мягкая компенсация TRIAC LED, чем `gamma 0.5` |
| 3 | gamma 1.5 | PWM/LED, если низ диапазона кажется слишком резким |
| 4 | gamma 2.0 | Типовая perceptual-кривая для PWM/LED |
| 5 | gamma 2.5 | Более сильное сжатие нижней части диапазона |
| 6 | gamma 3.0 | Максимальное сжатие нижней части диапазона |

До 8 bindings, stride 32:

```text
base = HR5100 + binding_index * 32
```

| Offset | Назначение |
|---:|---|
| +0 | enable |
| +1 | type (см. ниже) |
| +2 | target_type: 1 channel, 2 group |
| +3 | target_id: channel 1..4 или group 1 (`rgbw/all`) |
| +4 | primary DI: 0=DI0, 1..8=DI1..DI8 |
| +5 | secondary DI для rocker DOWN, иначе 0xFFFF |
| +6 | hold_step |
| +7 | hold_period_ms |
| +8 | flags |
| +9..+31 | reserved, must be 0 |

Типы кнопок:

| Value | Тип | Описание |
|---:|---|---|
| 1 | `rocker` | две линии DI, UP поднимает, DOWN опускает; DOWN при выкл. включает на `night_level` (0 = не включать) |
| 2 | `latching` | active включает на прошлую яркость, inactive выключает |
| 3 | `momentary` | короткое нажатие toggle, удержание диммирует по кругу |

RGBW/all-режим задается не отдельным типом кнопки, а target: `target_type=2`, `target_id=1`. В YAML это записывается как `target: rgbw`.

## YAML-конфигурация через Modbus

Тулза `src/tools/modbus_configure.py` преобразует YAML в holding registers
конфигурации, записывает их в устройство и сохраняет во внутреннюю flash.
Команда `write` после записи сразу
считывает конфигурацию обратно и проверяет совпадение.

Зависимости:

```sh
python3 -m pip install pyserial pyyaml
```

Пример конфигурации TRIAC-диммера с четырьмя клавишами и ночным режимом:

```yaml
modbus:
  address: 101
  baud: 57600
  parity: N
  stop_bits: 2

inputs:
  di_adc_active_threshold: 1000
  debounce_ms: 30
  short_press_min_ms: 50
  short_press_max_ms: 700
  long_press_ms: 800

auto_light:
  suppress_s: 15

channels:
  1: {min_level: 190, max_level: 1023, default_level: 1023, night_level: 290, fade_on_ms: 300, fade_off_ms: 500, output_curve: 4}
  2: {min_level: 360, max_level: 1023, default_level: 1023, night_level: 460, fade_on_ms: 300, fade_off_ms: 500, output_curve: 4}
  3: {min_level: 290, max_level: 1023, default_level: 1023, night_level: 390, fade_on_ms: 300, fade_off_ms: 500, output_curve: 4}
  4: {min_level: 320, max_level: 1023, default_level: 1023, night_level: 420, fade_on_ms: 300, fade_off_ms: 500, output_curve: 4}

bindings:
  - {type: rocker, target: channel1, up: DI1, down: DI2, hold_step: 8, hold_period_ms: 20}
  - {type: rocker, target: channel2, up: DI3, down: DI4, hold_step: 8, hold_period_ms: 20}
  - {type: rocker, target: channel3, up: DI5, down: DI6, hold_step: 8, hold_period_ms: 20}
  - {type: rocker, target: channel4, up: DI7, down: DI8, hold_step: 8, hold_period_ms: 20}
```

Запись и проверка YAML-файла:

```sh
python3 src/tools/modbus_configure.py write \
  --port /dev/ttyRS485-1 --slave 101 --config path/to/ddm845r.yaml

python3 src/tools/modbus_configure.py verify \
  --port /dev/ttyRS485-1 --slave 101 --config path/to/ddm845r.yaml
```

Чтение текущей конфигурации устройства в YAML:

```sh
python3 src/tools/modbus_configure.py dump \
  --port /dev/ttyRS485-1 --slave 101 --output current-ddm845r.yaml
```

Готовые примеры находятся в `src/tools/examples/`: отдельные TRIAC-каналы,
четыре независимых PWM-канала и групповое управление RGBW-лентой. Для
Wiren Board перед прямым доступом к `/dev/ttyRS485-*` необходимо остановить
`wb-mqtt-serial`, чтобы порт не использовался одновременно:

```sh
service wb-mqtt-serial stop
```

## Dimming

### DDM845R — TRIAC, leading-edge

- Тип выхода определяется сборкой `BOARD=ddm845r`, а не Modbus-конфигурацией;
- Zero-cross на PA0/EXTI0 сбрасывает TIM3;
- ARR = 1023, активная полярность LOW;
- Период сети оценивается по принятым zero-cross; стартовый span таймера 10500 мкс и защитный запас 500 мкс находятся внутри драйвера, не в Modbus-конфиге;
- `level=0` выключает канал. Ненулевой логический уровень `1..1023`
  сначала проходит `output_curve`, затем масштабируется в физический диапазон
  `min_level..max_level`, после чего преобразуется в фазовый compare;
- `fade_on_ms` / `fade_off_ms` меняют логический уровень во времени; каждый
  промежуточный уровень проходит через `output_curve` и `min_level..max_level`,
  а физический compare обновляется синхронно на ближайшем zero-cross;
- если LED-светильник выходит на световую полку раньше `1023`, `max_level`
  нужно ставить в измеренную точку насыщения, иначе верхняя часть процентов
  будет визуально почти одинаковой;
- Логические каналы обратно отображаются на TIM3: CH1→TIM3_CH4/PB1, CH2→TIM3_CH3/PB0, CH3→TIM3_CH2/PA7, CH4→TIM3_CH1/PA6.

### DDL84R — LED, continuous PWM

- Тип выхода определяется сборкой `BOARD=ddl84r`, а не Modbus-конфигурацией;
- TIM3 работает непрерывно с параметрами штатной прошивки (`ARR=1022`, `PSC=120`, около 97 Гц при 12 МГц);
- `level=0` выключает канал. Ненулевой логический уровень `1..1023`
  проходит `output_curve` и масштабируется в физический диапазон
  `min_level..max_level`; при `output_curve=0`, `min_level=0`,
  `max_level=1023` получается линейный PWM;
- Прямой порядок каналов: CH1→TIM3_CH1/PA6, CH2→TIM3_CH2/PA7, CH3→TIM3_CH3/PB0, CH4→TIM3_CH4/PB1;
- CH1/CH2 используют leading pulse, CH3/CH4 - trailing pulse той же скважности, чтобы снизить синхронные броски тока RGBW при промежуточных уровнях;
- Плавность перехода применяется общим выходным слоем: кнопки и Modbus-записи используют каналовый `fade_on_ms` / `fade_off_ms`, а `dimming_poll(now)` двигает фактический уровень к target.

## Bootloader over Modbus

Вход в bootloader:

- application register `HR60000 = 0xB007`;
- удержание DI0 active-low при старте;
- невалидный application image.

`src/tools/modbus_update.py` обновляет application через уже установленный
bootloader. Первичную установку нашего bootloader на устройство со штатной
прошивкой необходимо выполнить через OpenOCD (SWD программатор).
 
Пример сборки и штатного обновления DDM845R с адресом `101`:

```sh
make -C src BOARD=ddm845r BUILD_VERSION=0x0001002e app

# На Wiren Board освободить RS-485 порт на время прямого доступа.
service wb-mqtt-serial stop

python3 src/tools/modbus_update.py \
  --port /dev/ttyRS485-1 --slave 101 \
  src/build/ddm845r/app/0x0001002e/app.bin
```

Тулза считывает режим устройства, переводит application в bootloader,
стирает только application-region, передает образ блоками, проверяет CRC и
`device_id`, затем запускает новую application. При штатном переходе
bootloader использует Modbus-адрес и параметры последовательного порта из
application.

Если устройство уже находится в bootloader после аварийного запуска или
невалидного образа, он отвечает на fallback-параметрах `34`, `57600 8N2`.
Для такого случая можно явно указать параметры bootloader:

```sh
python3 src/tools/modbus_update.py \
  --port /dev/ttyRS485-1 --slave 34 \
  --boot-baud 57600 --boot-parity N --boot-stop-bits 2 \
  src/build/ddm845r/app/0x0001002e/app.bin
```

Update-регистры:

| Register | Назначение |
|---:|---|
| HR60000 | update command / enter bootloader |
| HR60001 | update status |
| HR60002..HR60003 | image_size hi/lo |
| HR60004..HR60005 | image_crc32 hi/lo |
| HR60006..HR60007 | image build_version hi/lo |
| HR60010..HR60011 | image_offset hi/lo, zero-based |
| HR60012 | chunk_word_count |
| HR60100..HR60219 | chunk data words |

Commands:

| Value | Command |
|---:|---|
| 0 | idle |
| 1 | begin |
| 2 | erase application |
| 3 | write chunk |
| 4 | verify |
| 5 | run application |
| 6 | abort |
| 0xB007 | enter bootloader из application |

Status:

| Value | Status |
|---:|---|
| 0 | idle |
| 1 | ready |
| 2 | erasing |
| 3 | writing |
| 4 | verifying |
| 5 | valid |
| 100 | size error |
| 101 | bounds error |
| 102 | flash error |
| 103 | CRC error |
| 104 | image error (включая несовпадение device_id) |
| 105 | sequence error |
| 106 | command error |

## Wiren Board — шаблоны устройств

Готовые шаблоны устройства для wb-mqtt-serial находятся в директории `../wb-templates/`:

| Файл | Прибор |
|---|---|
| `config-ddm845r-alt.json` | DDM845R с альтернативной прошивкой |
| `config-ddl84r-alt.json` | DDL84R с альтернативной прошивкой |

Установка на контроллер:

```sh
scp wb-templates/config-ddm845r-alt.json root@<WB_IP>:/etc/wb-mqtt-serial.conf.d/templates/
scp wb-templates/config-ddl84r-alt.json  root@<WB_IP>:/etc/wb-mqtt-serial.conf.d/templates/
```

После копирования шаблона обновить устройство в `/etc/wb-mqtt-serial.conf`, очистить старые retained MQTT-топики этого устройства и перезапустить драйвер:

```sh
service wb-mqtt-serial stop
# очистить retained /devices/<ID>/# через mosquitto_pub -r -n
service wb-mqtt-serial start
```

Ошибки шаблона и чтения регистров появятся в журнале:

```sh
journalctl -u wb-mqtt-serial --since "2 min ago"
```

В интерфейсе выбирать тип устройства **DDM845R (alt firmware)** / **DDL84R (alt firmware)** и задать Modbus-адрес (default 34).

Шаблоны покрывают регистры, реализованные в альтернативной прошивке (HR40–HR55, HR60–HR67, HR5008, HR9000, HR9002). Оригинальные input-регистры DDM845R (состояние DI) не поддерживаются — в нашей прошивке DI обрабатываются только внутри bindings.


## Оригинальные прошивки

Оригинальные прошивки для DDM845R и DDL84R лежат в `origfw/`

* razumdom_ddl84R stm32f1x_flash_32k.bin
* razumdom_ddm845R_stm32f1x_flash_64k.bin
* razumdom_drm88r_stm32f10x_flash_64k.bin
