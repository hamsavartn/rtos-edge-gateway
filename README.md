# RTOS-Based Industrial Edge Gateway
### Real-Time Telemetry Manager | STM32F103C8T6 + FreeRTOS

![Platform](https://img.shields.io/badge/Platform-STM32F103C8T6-blue)
![RTOS](https://img.shields.io/badge/RTOS-FreeRTOS-green)
![Memory](https://img.shields.io/badge/Heap%20Usage-0%20Bytes-brightgreen)
![Simulator](https://img.shields.io/badge/Simulated-Wokwi-orange)
![Language](https://img.shields.io/badge/Language-C%2B%2B-red)

---

## What Is This Project?

This is a **real-time industrial telemetry manager** running on a bare-metal ARM Cortex-M3 microcontroller. It continuously collects data from four different types of industrial sensors, processes it through a deterministic queuing pipeline, and outputs live readings to both a serial terminal and a 16×2 LCD display — all simultaneously, without ever crashing or losing data.

The system is built using **FreeRTOS** — a real-time operating system designed for embedded systems — with one strict rule enforced throughout the entire codebase: **absolutely no dynamic memory allocation**. No `malloc`, no `new`, no heap. Every byte of memory the system uses is allocated at compile time and lives in static storage.

---

## Why Does This Project Exist? (The Problem It Solves)

In industrial embedded systems, sensors generate data continuously — temperature, vibration, pressure, serial frames — at rates ranging from 0.5 Hz to 100 Hz. A naive system might collect all this data in a single loop, storing readings in dynamically allocated buffers. This approach causes three critical failure modes in production:

| Failure Mode | Cause | Consequence |
|---|---|---|
| **Heap Fragmentation Crash** | `malloc`/`new` called repeatedly over hours of runtime | System halts unpredictably, data lost |
| **Data Race / Corruption** | Two tasks writing the same buffer simultaneously | Sensor readings corrupted silently |
| **Priority Inversion** | Low-priority sensor task blocks high-priority output task | Real-time deadlines missed |

This project eliminates all three failure modes by design:

- **Heap fragmentation** → impossible: zero heap allocation, all memory is static
- **Data race** → impossible: each sensor has its own dedicated thread-safe queue
- **Priority inversion** → resolved: FreeRTOS mutex-protected shared resources with proper priority assignment

---

## How It Works — System Architecture

The system is structured as **8 concurrent FreeRTOS tasks** communicating exclusively through **5 thread-safe static queues**. No task ever shares memory directly with another.

```
┌─────────────────────────────────────────────────────────────┐
│                    SENSOR LAYER (Priority 2)                │
│                                                             │
│  [Task 1: ADC]      [Task 2: DHT22]   [Task 3: MPU6050]    │
│  PA0 @ 20Hz         PB0 @ 0.5Hz       I2C1 @ 100Hz         │
│  12-bit analog       1-Wire temp/       Accel + Gyro        │
│  potentiometer       humidity           6-axis IMU          │
│                                                             │
│  [Task 4: UART Sensor]                                      │
│  USART2 @ 10Hz — framed serial protocol (0xAA+LEN+CHK)     │
└──────┬───────────────┬───────────────┬──────────┬───────────┘
       │               │               │          │
   [Q:ADC]        [Q:DHT22]      [Q:MPU6050]  [Q:UART]
  StaticQueue    StaticQueue     StaticQueue  StaticQueue
  depth=16        depth=8         depth=16     depth=8
       │               │               │          │
       └───────────────┴───────────────┴──────────┘
                               │
┌──────────────────────────────▼──────────────────────────────┐
│               ROUTING LAYER (Priority 3)                    │
│                                                             │
│  [Task 5: Queue Manager]                                    │
│  Round-robin drain of all 4 sensor queues                   │
│  Formats data into OutputPacket_t (LCD-ready strings)       │
│  Error packets discarded + reported to ErrorHandler         │
└──────────────────────────────┬──────────────────────────────┘
                               │
                          [Q:Output]
                         StaticQueue
                          depth=32
                               │
┌──────────────────────────────▼──────────────────────────────┐
│               OUTPUT LAYER (Priority 3)                     │
│                                                             │
│  [Task 6: Output Manager]                                   │
│  USART1 (115200) → Serial terminal log with timestamp       │
│  I2C LCD (0x27)  → Live 16×2 display of latest reading     │
│  Status LED (PC13) toggles on every successful output       │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│               SYSTEM LAYER (Priority 4 — Highest)          │
│                                                             │
│  [Task 7: Error Handler]                                    │
│  Static ring buffer (32 entries) logs all fault codes      │
│  Checks stack high-watermarks every 5 seconds              │
│  Fatal faults halt system + rapid LED blink                 │
│                                                             │
│  [Task 8: Diagnostics]                                      │
│  Every 10 seconds prints full system snapshot:             │
│  queue depths, packets routed, stack watermarks,            │
│  error count, heap usage (must always read 0 bytes)        │
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
| Idle task stack | allocated at startup | `vApplicationGetIdleTaskMemory()` hook |
| Timer task stack | allocated at startup | `vApplicationGetTimerTaskMemory()` hook |

`FreeRTOSConfig.h` enforces this at the compiler level:
```c
#define configSUPPORT_DYNAMIC_ALLOCATION   0   // disabled entirely
#define configTOTAL_HEAP_SIZE              0U  // zero bytes allocated
#define configUSE_MALLOC_FAILED_HOOK       1   // catches violations
```

If any code anywhere accidentally calls `malloc` or `new`, the `vApplicationMallocFailedHook` fires immediately, logs the violation, and halts safely.

---

## Queue Design — O(1) Deterministic Latency

Each queue is a fixed-size ring buffer of fixed-size structs. Because:
- **No linked lists** — no pointer chasing
- **Fixed item size** — compiler-known offset arithmetic
- **Static storage** — no allocation overhead

Both `xQueueSend()` and `xQueueReceive()` execute in **O(1) constant time** regardless of queue depth or system load. This is critical for the MPU6050 task sampling at 100 Hz — it cannot afford variable-latency memory operations.

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

## Performance Metrics

| Metric | Value |
|---|---|
| System Clock | 72 MHz (HSE × PLL ×9) |
| RTOS Tick Resolution | 1 ms |
| Queue Latency | O(1) deterministic |
| Heap Memory Used | **0 bytes** |
| Runtime Heap Allocations | **0** |
| Stack Overflow Detection | FreeRTOS Pattern Check (mode 2) |
| ADC Sample Rate | 20 Hz |
| MPU6050 Sample Rate | 100 Hz |
| DHT22 Sample Rate | 0.5 Hz |
| UART Sensor Poll Rate | 10 Hz |
| Diagnostic Snapshot Interval | 10 seconds |

---

## UART Serial Output Format

Every packet routed produces a timestamped log line on USART1 (115200 baud):

```
╔══════════════════════════════════════════╗
║  RTOS Industrial Edge Gateway v1.0       ║
║  STM32F103C8 @ 72 MHz | FreeRTOS Static  ║
║  8 Tasks | 5 Queues | 0 Bytes Heap       ║
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

## Simulation — Wokwi Digital Twin

The complete system is simulated on **Wokwi** — no physical hardware required. The simulation includes:

- STM32F103 running at full 72 MHz with accurate peripheral timing
- DHT22 with configurable temperature/humidity values
- MPU6050 with I2C communication
- Potentiometer mapped to ADC PA0
- 16×2 LCD with I2C backpack
- UART terminal for serial log output

Open `wokwi/diagram.json` in the Wokwi VS Code extension or at [wokwi.com](https://wokwi.com).

---

## Project Structure

```
rtos-edge-gateway/
│
├── src/
│   ├── main.cpp               ← System init, task spawn, FreeRTOS hooks
│   ├── freertos_tasks.cpp     ← Tasks 1–4 (sensors) + Task 6 (output)
│   ├── queue_manager.cpp      ← Task 5 + all 5 static queues
│   ├── error_handler.cpp      ← Task 7 + error ring buffer
│   ├── diagnostics.cpp        ← Task 8 + system health snapshot
│   ├── adc_driver.cpp         ← ADC1 low-level driver (PA0)
│   ├── dht22_driver.cpp       ← DHT22 bit-bang 1-wire + DWT timing
│   ├── mpu6050_driver.cpp     ← MPU6050 I2C register driver
│   ├── uart_driver.cpp        ← USART1 log output (thread-safe)
│   ├── uart_sensor_driver.cpp ← USART2 framed sensor input
│   └── lcd_driver.cpp         ← I2C LCD PCF8574 4-bit driver
│
├── include/
│   ├── main.h                 ← All structs, error codes, config constants
│   ├── FreeRTOSConfig.h       ← RTOS tuning (static-only, stack checks)
│   ├── freertos_tasks.h
│   ├── queue_manager.h
│   ├── error_handler.h
│   ├── diagnostics.h
│   ├── adc_driver.h
│   ├── dht22_driver.h
│   ├── mpu6050_driver.h
│   ├── uart_driver.h
│   ├── uart_sensor_driver.h
│   └── lcd_driver.h
│
├── wokwi/
│   ├── diagram.json           ← Full circuit schematic
│   └── wokwi.toml             ← Simulator build target
│
├── platformio.ini             ← Build system config
├── .gitignore
└── README.md
```

---

## Skills Demonstrated

- **Bare-metal C++ embedded programming** on ARM Cortex-M3
- **FreeRTOS** — static task/queue/semaphore architecture
- **Low-level peripheral drivers** — ADC, I2C, USART, GPIO, DWT timer
- **Bit-bang protocol implementation** — DHT22 1-wire with cycle-accurate timing
- **I2C multi-device bus** — MPU6050 and LCD on shared bus
- **Thread-safe concurrent design** — producer/consumer queuing with no shared state
- **Deterministic real-time system design** — O(1) queuing, predictable latency
- **Fault-tolerant embedded architecture** — error logging, stack monitoring, zero-heap enforcement
- **Digital twin simulation** — full hardware validation on Wokwi without physical components

---

## Author

**Hamsavarthan**
B.Tech EEE — VIT Vellore (2024–2028)
[GitHub](https://github.com/) · [LinkedIn](https://linkedin.com/)
