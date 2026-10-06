# RTOS-Based Industrial Edge Gateway(made with STM32)
### Real-Time Telemetry Manager | STM32F103C8T6 + FreeRTOS

![Platform](https://img.shields.io/badge/Platform-STM32F103C8T6-blue)
![RTOS](https://img.shields.io/badge/RTOS-FreeRTOS-green)
![Memory](https://img.shields.io/badge/Heap%20Usage-0%20Bytes-brightgreen)
![Fault Tolerance](https://img.shields.io/badge/Fault%20Tolerance-IWDG%20%2B%20Recovery-purple)
![Simulator](https://img.shields.io/badge/Simulated-Wokwi-orange)
![Language](https://img.shields.io/badge/Language-C%2B%2B-red)

---

## What Is This Project?

This is a **real-time industrial telemetry manager** running on a bare-metal ARM Cortex-M3 microcontroller. It continuously collects data from four different types of industrial sensors, processes it through a deterministic queuing pipeline, and outputs live readings to both a serial terminal and a 16×2 LCD display — all simultaneously, without ever crashing or losing data.

The system is built using **FreeRTOS** — a real-time operating system designed for embedded systems — with one strict rule enforced throughout the entire codebase: **absolutely no dynamic memory allocation**. No `malloc`, no `new`, no heap. Every byte of memory the system uses is allocated at compile time and lives in static storage.

---

## Why Does This Project Exist? (The Problem It Solves)

In industrial embedded systems, sensors generate data continuously — temperature, vibration, pressure, serial frames — at rates ranging from 0.5 Hz to 100 Hz. A naive system might collect all this data in a single loop, storing readings in dynamically allocated buffers. This approach causes critical failure modes in production:

| Failure Mode | Cause | Consequence |
|---|---|---|
| **Heap Fragmentation Crash** | `malloc`/`new` called repeatedly over hours of runtime | System halts unpredictably, data lost |
| **Data Race / Corruption** | Two tasks writing the same buffer simultaneously | Sensor readings corrupted silently |
| **Priority Inversion** | Low-priority sensor task blocks high-priority output task | Real-time deadlines missed |
| **Task Starvation / Deadlock** | A single thread freezes while others keep running | Traditional watchdogs miss it, system hangs partially |
| **I2C Bus Lockup** | MCU resets mid-transmission, leaving peripheral in bad state | Peripheral holds SDA low permanently, killing the bus |

This project eliminates all these failure modes by design:

- **Heap fragmentation** → impossible: zero heap allocation, all memory is static.
- **Data race** → impossible: each sensor has its own dedicated thread-safe queue.
- **Priority inversion** → resolved: FreeRTOS mutex-protected shared resources with proper priority assignment.
- **Task starvation** → impossible: Multi-task Watchdog using Event Groups and hardware IWDG.
- **I2C Lockup** → resolved: Software bit-bang recovery sequence automatically resets locked peripherals.

---

## Advanced Fault Tolerance Features (Live Demo Showcase)

This project implements two advanced industrial-grade reliability features that act as a safety net against hardware and software failures. 

### 1. IWDG Task Health Monitoring (The "Deadlock Defender")
In a complex system, it's possible for one thread to freeze while the others keep running. A traditional watchdog only checks if the main CPU loop is running. 
* **The Solution:** An **Advanced Multi-Task Watchdog**. A dedicated, highest-priority Watchdog Task monitors a FreeRTOS Event Group. Every 1.5 seconds, it verifies that **all 8 background tasks** have checked in by setting their unique "health bit". If even a single task fails to report (indicating a freeze), the Watchdog refuses to pet the hardware Independent Watchdog (IWDG). The STM32 hardware then forcefully reboots the MCU within 2 seconds.
* **Live Demo:** Insert an infinite loop (`while(1) {}`) inside any sensor task. The system will run for 1.5s, log exactly which task died (`[FATAL] WATCHDOG TIMEOUT. Hung Mask: 0xXX`), and safely reboot itself.

### 2. I2C Bus Recovery (The "Hardware Healer")
I2C is vulnerable to a specific hardware glitch: if the STM32 resets mid-communication, the sensor might be left waiting for a clock pulse, holding the data line (`SDA`) LOW. This permanently locks the bus.
* **The Solution:** A **Software Bit-Bang Recovery**. If the STM32 detects a locked bus (NACK or timeout), it temporarily disconnects the hardware I2C controller. It takes manual control of the pins, bit-bangs up to 9 clock pulses on `SCL` to trick the sensor into finishing its transmission, generates a `STOP` condition, and reconnects hardware I2C.
* **Live Demo:** Briefly short the MPU6050 `SDA` pin to `GND`. The serial monitor will show an `ERR_I2C_NACK`. The software will instantly execute the 9-clock recovery sequence and resume streaming live accelerometer data within milliseconds, preventing a permanent crash.

---

## How It Works — System Architecture

The system is structured as **9 concurrent FreeRTOS tasks** communicating exclusively through **5 thread-safe static queues** and an **8-bit Event Group**.

```text
┌─────────────────────────────────────────────────────────────┐
│                    SENSOR LAYER (Priority 1)                │
│  [Task 1: ADC]      [Task 2: DHT22]   [Task 3: MPU6050]    │
│  PA0 @ 20Hz         PB0 @ 0.5Hz       I2C1 @ 100Hz         │
│                                                             │
│  [Task 4: UART Sensor]                                      │
│  USART2 @ 10Hz — framed serial protocol (0xAA+LEN+CHK)     │
└──────┬───────────────┬───────────────┬──────────┬───────────┘
       │               │               │          │
   [Q:ADC]        [Q:DHT22]      [Q:MPU6050]  [Q:UART]
  StaticQueue    StaticQueue     StaticQueue  StaticQueue
       │               │               │          │
       └───────────────┴───────────────┴──────────┘
                               │
┌──────────────────────────────▼──────────────────────────────┐
│               ROUTING LAYER (Priority 2)                    │
│  [Task 5: Queue Manager]                                    │
│  Round-robin drain of all 4 sensor queues                   │
│  Formats data into OutputPacket_t (LCD-ready strings)       │
└──────────────────────────────┬──────────────────────────────┘
                               │
                          [Q:Output]
┌──────────────────────────────▼──────────────────────────────┐
│               OUTPUT LAYER (Priority 2)                     │
│  [Task 6: Output Manager]                                   │
│  USART1 (115200) → Serial terminal log with timestamp       │
│  I2C LCD (0x27)  → Live 16×2 display of latest reading     │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│               SYSTEM LAYER (Priority 3)                    │
│  [Task 7: Error Handler]                                    │
│  Logs fault codes & checks stack high-watermarks            │
│                                                             │
│  [Task 8: Diagnostics]                                      │
│  Every 10s prints full system snapshot & validates 0 heap   │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│          FAULT TOLERANCE & HEALTH (Priority 4)             │
│  [Task 9: Watchdog]                                         │
│  Monitors Event Group. All 8 lower tasks must check in      │
│  every 1.5s, otherwise Watchdog triggers hardware MCU reset.│
└─────────────────────────────────────────────────────────────┘
```

---

## Memory Architecture — Why Zero Heap?

Every FreeRTOS object in this project uses its static variant:

| Object | Dynamic (typical) | This Project |
|---|---|---|
| Task creation | `xTaskCreate()` → heap | `xTaskCreateStatic()` → `.bss` |
| Queue creation | `xQueueCreate()` → heap | `xQueueCreateStatic()` → `.bss` |
| Mutex creation | `xSemaphoreCreateMutex()` → heap | `xSemaphoreCreateMutexStatic()` → `.bss` |
| Event Groups | `xEventGroupCreate()` → heap | `xEventGroupCreateStatic()` → `.bss` |

`FreeRTOSConfig.h` enforces this at the compiler level:
```c
#define configSUPPORT_DYNAMIC_ALLOCATION   0   // disabled entirely
#define configTOTAL_HEAP_SIZE              0U  // zero bytes allocated
```

---

## Hardware & Wiring

| Signal | MCU Pin | Connected To |
|---|---|---|
| ADC Input | PA0 | Potentiometer wiper |
| DHT22 Data | PB0 | DHT22 SDA (4.7kΩ pull-up) |
| I2C SCL | PB6 | MPU6050 SCL + LCD SCL |
| I2C SDA | PB7 | MPU6050 SDA + LCD SDA |
| USART1 TX | PA9 | USB-Serial adapter RX |
| USART1 RX | PA10 | USB-Serial adapter TX |
| USART2 TX | PA2 | UART sensor RX |
| USART2 RX | PA3 | UART sensor TX |
| Status LED | PC13 | Onboard LED (active low) |

---

## UART Serial Output Format

Every packet routed produces a timestamped log line on USART1 (115200 baud):

```text
╔══════════════════════════════════════════╗
║  RTOS Industrial Edge Gateway v1.0       ║
║  STM32F103C8 @ 72 MHz | FreeRTOS Static  ║
║  9 Tasks | 5 Queues | 0 Bytes Heap       ║
╚══════════════════════════════════════════╝

[    1250] SRC=0x01 | ADC:1647mV       | Raw: 2043
[    1251] SRC=0x03 | AX:  892 AY: -341 | GX:   12 GY:  -8
[    2000] SRC=0x02 | T: 25.4C         | H: 60.2%RH
[    2100] SRC=0x04 | UART[ 6B]        | CO2:4
════════════════════════════════════════
 DIAG SNAPSHOT #1 | Uptime: 10.000 s
════════════════════════════════════════
 QUEUES  ADC: 0/16  DHT22:0/8  MPU: 0/16  UART:0/8  OUT: 0/32
 ROUTED  Total: 342 packets
 ERRORS  Total: 0
 STACKS (min free words):
   ADC:187  DHT22:201  MPU:174  UART:219
   QMgr:241  Out:198  Err:223  Diag:---
 HEAP    Free: 0 B | MinEver: 0 B | Alloc: NONE [OK]
════════════════════════════════════════
```

---

## Project Structure

```text
rtos-edge-gateway/
│
├── src/
│   ├── main.cpp               ← System init, task spawn, IWDG init
│   ├── freertos_tasks.cpp     ← Tasks 1–4 (sensors), Task 6 (output), Task 9 (Watchdog)
│   ├── queue_manager.cpp      ← Task 5 + all 5 static queues
│   ├── error_handler.cpp      ← Task 7 + error ring buffer
│   ├── diagnostics.cpp        ← Task 8 + system health snapshot
│   ├── adc_driver.cpp         ← ADC1 low-level driver (PA0)
│   ├── dht22_driver.cpp       ← DHT22 bit-bang 1-wire + DWT timing
│   ├── mpu6050_driver.cpp     ← MPU6050 I2C driver + 9-clock bus recovery
│   ├── uart_driver.cpp        ← USART1 log output (thread-safe)
│   ├── uart_sensor_driver.cpp ← USART2 framed sensor input
│   └── lcd_driver.cpp         ← I2C LCD PCF8574 driver
│
├── include/
│   ├── main.h                 ← Structs, error codes, event bitmasks
│   ├── FreeRTOSConfig.h       ← RTOS tuning (static-only, stack checks)
│   └── (driver headers...)
│
├── wokwi/                     ← Wokwi simulation files
├── platformio.ini             ← Build system config
└── README.md
```

---

## Skills Demonstrated

- **Bare-metal C++ embedded programming** on ARM Cortex-M3.
- **Fault-Tolerant Architectures** — IWDG hardware watchdogs, Event Groups for deadlock prevention, and I2C bit-bang bus recovery.
- **FreeRTOS** — fully static task/queue/semaphore/event group architecture.
- **Low-level peripheral drivers** — direct hardware register manipulation alongside STM32 HAL.
- **Thread-safe concurrent design** — producer/consumer queuing with no shared state or mutex contention.
- **Deterministic real-time system design** — O(1) queuing, predictable latency, priority assignment.
- **Zero-heap enforcement** — eliminating memory fragmentation at compile time.

---

## Author

**Hamsavarthan**
B.Tech EEE — VIT Vellore (2024–2028)
[GitHub](https://github.com/) · [LinkedIn](https://linkedin.com/)
