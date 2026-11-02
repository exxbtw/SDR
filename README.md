# Программно-определяемое радио на базе ADALM-PLUTO

Лабораторные работы по SDR-платформе **Analog Devices ADALM-PLUTO (PlutoSDR)**: от сборки приёмника в GNU Radio до собственной программы на C/C++ на SoapySDR, передачи сигнала между двумя платами по радиоканалу и цифровой модуляции (BPSK/QPSK).

Репозиторий ведётся по неделям: код и материалы лежат в общих папках, а описание работ собрано в этом README. Подробный отчёт по каждой неделе вынесен в `reports/`.

**ФИО:** Кузеванов Юрий Сергеевич  
**Группа:** ИКС-431

---

## Содержание

1. [Структура репозитория](#структура-репозитория)
2. [Требования и сборка](#требования-и-сборка)
3. [Отчёты по неделям](#отчёты-по-неделям)
4. [Разработка по неделям](#разработка-по-неделям)
   - [Неделя 1: GNU Radio. Построение FM-радиоприёмника](#неделя-1-gnu-radio-построение-fm-радиоприёмника)
   - [Неделя 3: Adalm Pluto SDR и временные метки (timestamp)](#неделя-3-adalm-pluto-sdr-и-временные-метки-timestamp)

---

## Структура репозитория

```text
├── README.md              # этот файл: описание проекта и сводка по неделям
├── CMakeLists.txt         # сборка проекта (исполняемый файл main из src/)
├── src/                   # исходный код программ на C++
│   └── main.cpp
├── scripts/               # скрипты построения графиков (Python)
│   └── plot_txdata.py
├── reports/               # подробные отчёты по каждой неделе
├── screens/               # иллюстрации, схемы, графики
└── .gitignore             # build/, *.pcm и прочий мусор
```

Структура дополняется по мере добавления недель.

---

## Требования и сборка

### 1. Установка зависимостей (Ubuntu / Debian)

```bash
sudo apt-get update
sudo apt-get install -y cmake g++ libpython3-dev python3-pip python3-setuptools python3-numpy swig python3-matplotlib \
    libxml2 libxml2-dev bison flex libcdk5-dev libusb-1.0-0-dev libaio-dev pkg-config \
    libavahi-common-dev libavahi-client-dev
```

> Проект собирался и запускался на macOS (MacBook Air). Команды выше для Linux; на macOS пакеты ставятся через `brew`, а `nproc` заменяется на `sysctl -n hw.ncpu`, `ldconfig` не нужен.

### 2. Сборка драйверов и библиотек

Для работы с аппаратными таймстемпами используются специализированные ветки библиотек.

**SoapySDR**
```bash
git clone --branch soapy-sdr-0.8.1 https://github.com/TelecomDep/SoapySDR.git
cd SoapySDR
mkdir build && cd build
cmake ../
make -j`nproc`       # nproc - количество потоков, например make -j16
sudo make install
sudo ldconfig
cd ../..
```

**libiio**
```bash
git clone --branch v0.24 https://github.com/TelecomDep/libiio.git
cd libiio
mkdir build && cd build
cmake ../
make -j`nproc`
sudo make install
cd ../..
```

**libad9361-iio**
```bash
git clone --branch v0.3 https://github.com/TelecomDep/libad9361-iio.git
cd libad9361-iio
mkdir build && cd build
cmake ../
make -j`nproc`
sudo make install
sudo ldconfig
cd ../..
```

**SoapyPlutoSDR** (модуль поддержки PlutoSDR с таймстемпами)
```bash
git clone --branch sdr_gadget_timestamping https://github.com/TelecomDep/SoapyPlutoSDR.git
cd SoapyPlutoSDR
mkdir build && cd build
cmake ../
make -j`nproc`
sudo make install
sudo ldconfig
cd ../..
```

### 3. Сборка проекта

Сборка выполняется вне исходников, в `build/` (она в `.gitignore`):

```bash
mkdir build && cd build
cmake ..
make
```

### 4. Запуск

Запуск программы недели 3 (адрес платы смотрится командой `iio_info -s`, по умолчанию `usb:1.3.5`):

```bash
./main [uri]
```

Принятые сэмплы пишутся в `txdata.pcm` в текущей папке (из `build/`). График строится скриптом:

```bash
python3 ../scripts/plot_txdata.py txdata.pcm
```

Нужны `numpy` и `matplotlib`.

---

## Отчёты по неделям

| Неделя | Тема | Отчёт |
|:---:|---|---|
| 1 | GNU Radio. Построение FM-радиоприёмника | [reports/week1.md](reports/week1.md) |
| 3 | Adalm Pluto SDR и временные метки (timestamp) | [reports/week3.md](reports/week3.md) |

---

## Разработка по неделям

### Неделя 1: GNU Radio. Построение FM-радиоприёмника

**Цель.** Собрать в GNU Radio Companion FM-радиоприёмник на базе Adalm-Pluto, найти частоту реальной радиостанции и получить на выходе слышимый аудиосигнал.

**Что сделано.** Собрана схема: `PlutoSDR Source` (LO 101 МГц, частота дискретизации 2.4 МГц) → `Low Pass Filter` (срез 100 кГц, децимация 5) → `WBFM Receive` (децимация звука 10) → `Audio Sink` (48 кГц), плюс `QT GUI Frequency Sink` и `Time Sink` для контроля спектра и формы сигнала. Частота дискретизации снижается по цепочке 2.4 МГц → 480 кГц → 48 кГц.

![Схема приёмника в GNU Radio Companion](screens/week1_scheme.png)

**Результат.** Приёмник запущен, на спектре виден выраженный пик на частоте около 101.1 МГц.

![Спектр принимаемого сигнала](screens/week1_spectrum.png)

Подробнее: [reports/week1.md](reports/week1.md).

---

### Неделя 3: Adalm Pluto SDR и временные метки (timestamp)

**Цель.** Освоить работу с ADALM-PLUTO через библиотеку SoapySDR/libiio: настроить потоки RX/TX, передать IQ-сэмплы с временными метками и проанализировать принятый сигнал.

**Что сделано.** Установлены и собраны SoapySDR, libiio и модуль SoapyPlutoSDR с поддержкой timestamp. Написана программа на C++ (сборка через CMake): устройство открывается с параметрами `direct=1` и `timestamp_every=1920`, частота дискретизации 1 МГц, несущая 800 МГц. Передача выполняется между двумя антеннами одной платы: TX-буфер отправляется в эфир, а RX-поток читается и записывается в файл `txdata.pcm`. Затем вместо константного заполнения TX-буфера сформирован сигнал заданной формы (сглаженная косинусоида `0.5·(1 − cos 2πt)`, один период).

**Результат.** Временные метки соседних RX-буферов отличаются на ~1 920 002 нс, что соответствует 1920 сэмплам при частоте 1 МГц: режим timestamp работает корректно.

![Лог запуска: timestamp-режим включён](screens/week3_zapusk.png)

Переданный буфер виден в принятом сигнале; после замены константы на сглаженную косинусоиду на графике видна плавная форма с одним выбросом в середине буфера.

![Принятый сигнал с заданной формой](screens/week3_grafik_cos.png)

**Код:** [`src/main.cpp`](src/main.cpp), скрипт графика: [`scripts/plot_txdata.py`](scripts/plot_txdata.py).

Подробнее: [reports/week3.md](reports/week3.md).