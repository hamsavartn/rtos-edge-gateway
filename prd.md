# Product Requirements Document (PRD)
## RTOS-Based Industrial Edge Gateway

| | |
|---|---|
| **Product name** | RTOS Industrial Edge Gateway |
| **Version** | 1.0 (post-audit release, Oct 2026) |
| **Author** | Hamsavarthan (B.Tech EEE, VIT Vellore) |
| **Status** | Implemented — all requirements verified |
| **Target hardware** | STM32F103C8T6 "Blue Pill" (ARM Cortex-M3, 64 KB Flash, 20 KB SRAM) |
| **Simulation target** | Wokwi digital twin (VS Code extension / CLI + CI) |
| **Related docs** | [README.md](README.md) · [AGENTS.md](AGENTS.md) |

---

## 1. Vision & Problem Statement

Industrial sites need low-cost nodes that continuously acquire sensor data (analog, digital-1-wire, I2C, serial) and present it reliably on local HMIs and log streams — without crashing over months of unattended operation. Naive firmware (single-loop, heap-using) fails in the field through heap fragmentation, data races, priority inversion, undetected task freezes, and I2C bus lockups.

This product is a **fully static-memory FreeRTOS telemetry gateway** that eliminates those failure modes *by construction* and demonstrates industrial-grade fault tolerance on a $2 microcontroller, verifiable both in simulation (Wokwi) and on real hardware.

### 1.1 Design pillars
1. **Zero heap** — no `malloc`/`new` anywhere; every RTOS object uses `*Static` APIs; enforced at compile time (`configSUPPORT_DYNAMIC_ALLOCATION = 0`, malloc-failed hook armed).
2. **No silent failures** — every fault is counted, logged, and visible; a frozen task is detected by name within one watchdog window.
3. **Deterministic dataflow** — each sensor has a dedicated queue; a single router feeds a single output path. No shared mutable state between producers.
4. **Simulator-first verifiability** — the exact firmware binary runs in Wokwi with serial/LCD/LED observables and CI screenshots.

---

## 2. Goals & Non-Goals

### 2.1 Goals
- G1: Acquire 4 sensor classes concurrently: ADC (analog), DHT22 (1-wire digital), MPU6050 (I2C IMU), UART-framed serial sensor.
- G2: Route all readings through static queues into a single formatted output pipeline.
- G3: Present live data on USART1 log (115200) **and** a 16×2 I2C LCD simultaneously.
- G4: Detect any frozen task within one watchdog window and reset the MCU via hardware IWDG (real hardware).
- G5: Recover automatically from I2C bus lockups (9-clock software recovery) without reboot.
- G6: Run identically in Wokwi simulation for demo/CI purposes.
- G7: Fit within Blue Pill resources: ≤ 80 % SRAM, ≤ 50 % Flash.

### 2.2 Non-Goals (v1.0)
- Network connectivity (MQTT/Ethernet/Wi-Fi) — deferred to Phase 2 (§12).
- OTA/bootloader — Phase 3.
- Low-power tickless idle — Phase 3.
- DMA offloading of ADC/UART — Phase 2 (polling chosen for v1 determinism).
- Data persistence (flash/EEPROM logging).

---

## 3. Users & Use Cases

| Persona | Use case |
|---|---|
| **Embedded learner / reviewer** | Reads the codebase as a reference for zero-heap FreeRTOS architecture, static queues, watchdog health monitoring, I2C recovery. |
| **Demo audience (recruiter/professor)** | Watches Wokwi simulation: live LCD telemetry, serial log, fault-tolerance demos (watchdog kill, I2C healing). |
| **Industrial integrator (future)** | Reuses the task/queue skeleton for a real sensor node; enables IWDG via build flag for deployment. |

Primary use cases:
- UC-1: Continuous multi-sensor acquisition at fixed rates (§5.1).
- UC-2: Live display rotation on LCD + timestamped serial log.
- UC-3: Operator injects a fault (task hang, I2C stall); system detects, reports, and self-recovers/resets.
- UC-4: CI builds firmware and runs the simulated board, uploading serial log + screenshot artifacts.

---

## 4. System Architecture

### 4.1 Task model (9 tasks, static TCBs & stacks)

| # | Task | Priority | Stack (words) | Period | Responsibility |
|---|---|---|---|---|---|
| 1 | `vTaskADCSensor` | 1 | 192 | 50 ms (20 Hz) | Poll ADC1 ch0 (PA0), convert to mV, enqueue |
| 2 | `vTaskDHT22Sensor` | 1 | 192 | 2000 ms | Bit-bang DHT22 protocol on PB0 (DWT µs timing), enqueue |
| 3 | `vTaskMPU6050Sensor` | 1 | 192 | 10 ms (100 Hz) | 14-byte burst read over I2C1, enqueue; run bus recovery on failure |
| 4 | `vTaskUARTSensor` | 1 | 192 | 100 ms poll | Pull complete framed packets from USART2 IRQ ring buffer, enqueue |
| 5 | `vTaskQueueManager` | 2 | 384 | continuous (1 ms yield) | Round-robin drain of sensor queues → format → output queue |
| 6 | `vTaskOutputManager` | 2 | 384 | event-driven (100 ms block) | UART log line + throttled 2 Hz LCD refresh + status LED toggle |
| 7 | `vTaskErrorHandler` | 3 | 256 | 500 ms | Flush error ring log to UART; stack watermark sweep every 5 s |
| 8 | `vTaskDiagnostics` | 3 | 256 | 500 ms (snapshot 10 s) | Full system snapshot: queues, routed count, errors, stack HWMs |
| 9 | `vTaskWatchdog` | 4 (highest) | 256 | 3 s window | Verify all 8 health bits; feed IWDG or fatal-halt with hung-task mask |

Priorities derive from `configMAX_PRIORITIES = 5`. Idle task: 128-word static stack. Timer service task: disabled (`configUSE_TIMERS = 0`, no software timers used).

### 4.2 Dataflow

```text
ADC(20Hz)──►[Q:ADC 16]──┐
DHT22(0.5Hz)►[Q:DHT 8]──┤
MPU(100Hz)─►[Q:MPU 16]──┼──► QueueManager ──► [Q:OUT 16] ──► OutputManager ──► USART1 log + LCD + LED
UART(9600)─►[Q:UART 8]──┘   (formats to                    │
                             16-char lines)                ▼
Health bits ────────────────────────────────► EventGroup ──► Watchdog ──► IWDG (hardware, optional)
ErrorHandler ring log (32 entries) ──────────────▲ (all tasks report here)
```

### 4.3 Health monitoring chain (timing invariant)

```text
DHT22 check-in period (2 s, sensor-limited)
   < Watchdog event-group window (3 s, WATCHDOG_WINDOW_MS)
      < IWDG timeout (4 s, when ENABLE_IWDG is set)
```
**This ordering is a system invariant.** Changing any sampling period or window requires re-validating the chain. Bits: `TASK_ADC_BIT … TASK_DIAG_BIT` (8 bits, mask `0xFF`; the Watchdog cannot feed itself).

### 4.4 Shared-resource protection
- I2C1 bus (MPU6050 + LCD backpack): static mutex with priority inheritance; pre-scheduler callers bypass safely.
- USART1 log: static mutex (50 ms take timeout); transmit timeout 100 ms.
- Error ring log: static mutex (5 ms producer / 10 ms consumer timeout).
- USART2 RX: lock-free SPSC ring buffer (ISR producer / task consumer, no RTOS calls in ISR).

---

## 5. Functional Requirements

### 5.1 Sensor acquisition
- **FR-1 (ADC)**: Sample PA0 (ADC1 ch0, 12-bit, PCLK2/6 ≈ 10.67 MHz ADC clock, 28.5-cycle sampling, hardware-calibrated) every 50 ms; publish `{timestamp, raw, voltage_mv}` where `voltage_mv = raw × 3300 / 4095`. PA0 must be configured as analog input explicitly.
- **FR-2 (DHT22)**: Read PB0 via bit-banged 1-wire every 2 s (sensor minimum): 1.2 ms start pulse, response detect, 40 bits threshold-decoded at 40 µs; verify checksum; parse signed temperature (×0.1 °C) and humidity (×0.1 %RH). Timing must use DWT cycle counter with a calibrated busy-loop fallback if DWT does not advance. The bus must be released idle-high at boot.
- **FR-3 (MPU6050)**: Initialize at 0x68 (AD0 low): wake (PWR_MGMT_1=0x01), 1 kHz sample rate, DLPF 10 Hz, ±250 °/s gyro, ±2 g accel; WHO_AM_I must equal 0x68. Every 10 ms burst-read 14 bytes (accel XYZ, temp, gyro XYZ, big-endian). On any failure: report `ERR_I2C_NACK`, run the 9-clock bus recovery (deinit → open-drain bit-bang → manual STOP → re-init) **without holding the bus mutex during re-init**, and continue.
- **FR-4 (UART sensor)**: Receive on USART2 (9600 8N1) via **interrupt-driven RX into a 128-byte ring buffer** (ISR priority 6 ≥ `configMAX_SYSCALL_INTERRUPT_PRIORITY`; no RTOS calls in ISR; re-arm in RX-complete and error callbacks). Parse frames `[0xAA][LEN][DATA…LEN≤28][XOR checksum]`; outcomes: OK / TIMEOUT (no SOF) / OVERFLOW (LEN>28 or ring overrun) / CRC_ERROR. Poll task drains one frame per 100 ms cycle.

### 5.2 Routing & formatting
- **FR-5**: QueueManager drains all sensor queues round-robin (non-blocking), drops packets with `error != ERR_NONE` into the error handler, formats valid packets into 16×2 LCD-ready strings, and enqueues to the output queue (drop + `ERR_QUEUE_FULL` report on overflow; source IDs: ADC=0x01, DHT22=0x02, MPU=0x03, UART=0x04).
- **FR-6**: Formatting rules — ADC: `ADC:<mV>mV` / `Raw:<n>`; DHT22: `T:<t>.<f>C` / `H:<h>.<f>%RH` with **correct sign handling for negative temperatures** (fraction printed unsigned); MPU: `A:<ax>,<ay>` / `G:<gx>,<gy>`; UART: `UART[<n>B]` / payload as printable text (space-padded, ≤16 chars).

### 5.3 Output & HMI
- **FR-7**: OutputManager prints each routed packet to USART1 (115200) as `[%8lu ms] SRC=0x0X | <line1> | <line2>`, thread-safely.
- **FR-8**: LCD (PCF8574 backpack at 0x27, 4-bit mode via I2C) refreshes at most every 500 ms; full init sequence (0x30×3 → 0x20 → 0x28/0x0C/0x01/0x06) with **every step's status checked**. A failed or missing LCD is a **non-fatal HMI fault**: it is reported once per failure episode (`LCD_INIT`/`LCD_WRITE` → `ERR_I2C_NACK`), retried once per throttle window, and never prevents watchdog check-in. Boot splash: `Gateway Booting` / `Waiting for data`.
- **FR-9**: Status LED on PC13 toggles on every output packet.

### 5.4 Fault tolerance & observability
- **FR-10**: Every task sets its health bit every loop iteration (worst case: DHT22 every 2 s). Watchdog waits up to 3 s for **all** bits, then logs `[FATAL] WATCHDOG TIMEOUT. Hung Mask: 0xXX`, blinks the LED, and (with IWDG armed) starves the feeder to force reset.
- **FR-11**: Hardware IWDG (LSI 40 kHz ÷ 64, reload 2499 → 4.0 s) is compiled **only** when `-DENABLE_IWDG` is set; it must remain off in Wokwi (IWDG not simulated). The feeder writes `0xAAAA` only after a fully healthy window.
- **FR-12**: Error handler maintains a 32-entry static ring log `{timestamp, code, source}`; entries flush to UART; all tasks report through it. Stack overflow hook and malloc-failed hook log and halt with LED blink (busy-delay only — SysTick is masked in fault contexts).
- **FR-13**: Diagnostics prints a full snapshot every 10 s: uptime, per-queue depths, total routed, total errors, per-task stack high-water marks (all 8 checked-in tasks), and zero-heap validation line.
- **FR-14**: `configASSERT` is armed; assert failure halts visibly.

### 5.5 Simulation & CI
- **FR-15**: Wokwi assets: `wokwi.toml` (firmware/elf paths) + `diagram.json` (board, DHT22 with 4.7 kΩ pull-up, MPU6050 AD0→GND, LCD on 3V3, potentiometer on A0, LED on PC13 via 220 Ω) **including explicit `$serialMonitor` connections for USART1 (A9 → `$serialMonitor:RX`, `$serialMonitor:TX` → A10) and `display: always`** — the Blue Pill's serial monitor is not automatic.
- **FR-16**: GitHub Actions (`.github/workflows/wokwi.yml`) builds with PlatformIO, runs the simulation with the Wokwi CLI, and uploads `serial-output.log` + `board-state.png` artifacts on every push.

---

## 6. Non-Functional Requirements

- **NFR-1 (Zero heap)**: `configSUPPORT_STATIC_ALLOCATION=1`, `configSUPPORT_DYNAMIC_ALLOCATION=0`, `configTOTAL_HEAP_SIZE=0`; no heap_*.c compiled; malloc-failed hook fatal. Verified by grep + link map.
- **NFR-2 (Memory budget)**: ≤ 16 KB / 20 KB SRAM (current: **14 468 B, 70.6 %**), ≤ 33 KB / 64 KB Flash (current: **~26.5 KB, 40.5 %**).
- **NFR-3 (Timing)**: 1 kHz tick; all sensor deadlines met with priority-preemptive scheduling; DHT22 read holds a critical section ≤ ~6 ms (accepted; tick compensates via `vTaskDelayUntil`).
- **NFR-4 (ISR safety)**: Only `FromISR`-safe patterns; USART2 ISR performs no RTOS calls; all ISR priorities ≥ `configMAX_SYSCALL_INTERRUPT_PRIORITY` (5, 4-bit priority).
- **NFR-5 (Blocking discipline)**: No `HAL_MAX_DELAY`; all HAL polling calls carry finite timeouts; queue ops non-blocking in producers.
- **NFR-6 (Portability)**: Builds warning-clean with arm-none-eabi-gcc 7.2.1 via PlatformIO (`ststm32` 20.x, STM32CubeF1 1.8.7, FreeRTOS-Kernel **V11.3.1** pinned).
- **NFR-7 (Determinism)**: All queues, stacks, TCBs, buffers statically allocated in `.bss`; no `new`/`delete`/`malloc` anywhere (AGENTS.md policy).
- **NFR-8 (Recoverability)**: Any single task hang → detected ≤ 3 s, reset ≤ 4 s (IWDG armed) without operator action; I2C lockup → recovered in software without reset.

---

## 7. Hardware Specification & Wiring

| Signal | MCU pin | Connected to | Notes |
|---|---|---|---|
| ADC input | PA0 | Potentiometer wiper | Analog mode required |
| DHT22 data | PB0 | DHT22 SDA + 4.7 kΩ pull-up to 3V3 | Open-drain + pull-up |
| I2C SCL | PB6 | MPU6050 SCL + LCD SCL | 100 kHz, internal pull-ups |
| I2C SDA | PB7 | MPU6050 SDA + LCD SDA | MPU AD0 → GND (0x68) |
| USART1 TX/RX | PA9 / PA10 | Serial monitor / USB-serial | 115200 — log output |
| USART2 TX/RX | PA2 / PA3 | UART sensor TX/RX | 9600 — framed input |
| Status LED | PC13 | Onboard LED + external LED via 220 Ω | Active-low onboard |
| Power | 3V3 | MPU, DHT22, pot, LCD (sim) | LCD at 5 V on real HW (see note) |

> **Real-hardware note:** the PCF8574 backpack at 5 V with a 3.3 V master is marginal on I2C levels; use a level shifter, or power the backpack at 3.3 V (reduced contrast). In Wokwi the LCD is wired to 3V3 because the simulated 5V rail is undriven.

I2C addresses: MPU6050 `0x68` (`0xD0` write), LCD backpack `0x27` (`0x4E` write).

---

## 8. Interface Specifications

### 8.1 USART1 log output (115200 8N1)
```text
Boot banner (boxed, name/version/clock/task-count)
[    1250] SRC=0x01 | ADC:1647mV     | Raw:2043
[    2000] SRC=0x02 | T:25.4C        | H:60.2%RH
[ERR_LOG] T=     812 Code=0x05 Src=MPU6050
 DIAG SNAPSHOT #1 | Uptime: 10.000 s … QUEUES/ROUTED/ERRORS/STACKS/HEAP
[FATAL] WATCHDOG TIMEOUT. Hung Mask: 0x02        ← only on fault
```

### 8.2 USART2 sensor frame (9600 8N1)
```text
[0xAA][LEN][DATA×LEN][CHECKSUM]   LEN ≤ 28   CHECKSUM = XOR of DATA bytes
Minimum frame: 3 bytes (LEN=0). Byte-gap timeout: 10 ms. SOF hunt: bounded 50 ms.
```

### 8.3 Error codes (`ErrorCode_t`)
`0x00 NONE · 0x01 QUEUE_FULL · 0x02 QUEUE_EMPTY · 0x03 SENSOR_TIMEOUT · 0x04 SENSOR_CRC · 0x05 I2C_NACK · 0x06 UART_OVERFLOW · 0x07 STACK_OVERFLOW · 0x08 HEAP_ALLOC_ATTEMPTED · 0x09 SCHEDULER_FAILED · 0x0A WATCHDOG_TIMEOUT · 0x0B ADC_NOT_READY · 0x0C DHT22_CHECKSUM · 0x0D MPU6050_INIT`

---

## 9. Build, Configuration & Project Layout

- **Build**: `pio run` → `.pio/build/bluepill_f103c8/firmware.{bin,elf}`; serial monitor (real HW): `pio device monitor` @ 115200.
- **Key defines** (`platformio.ini`): `-DSTM32F103xB -DUSE_HAL_DRIVER`; optional `-DENABLE_IWDG` (real hardware only).
- **FreeRTOS tuning** (`include/FreeRTOSConfig.h`): static-only, 1 kHz tick, `configCHECK_FOR_STACK_OVERFLOW=2`, `configUSE_MALLOC_FAILED_HOOK=1`, timers off, trace off.
- **Pre-build script** `scripts/prune_freertos.py` trims unused kernel ports.
- **Layout**: `src/` (12 files: main, tasks, queue manager, error handler, diagnostics, 5 drivers, assert) · `include/` (12 headers) · `scripts/` · `diagram.json` + `wokwi.toml` · `.github/workflows/wokwi.yml`.

---

## 10. Verification & Acceptance Criteria

### 10.1 Verification chain (already executed, Oct 2026)
| # | Check | Method | Result |
|---|---|---|---|
| V1 | Clean build, default config | `pio run` from scratch | ✅ SUCCESS, 70.6 % RAM / 40.5 % Flash |
| V2 | Build with `-DENABLE_IWDG` | PlatformIO env override | ✅ SUCCESS |
| V3 | Zero-heap policy | grep for malloc/new/free + config audit | ✅ CLEAN |
| V4 | ISR-safety audit | code review of all ISR paths | ✅ no RTOS calls, priority 6 ≥ 5 |
| V5 | Firmware image validity | vector table (SP `0x20005000`, reset `0x0800xxxx`) + string audit | ✅ VALID |
| V6 | Serial monitor wiring | Wokwi docs cross-check + explicit `$serialMonitor` edges | ✅ WIRED |

### 10.2 Acceptance criteria (simulator)
- A1: Boot banner appears in the serial monitor **≤ 1 s** after simulation start.
- A2: ADC telemetry tracks the potentiometer continuously (20 Hz stream).
- A3: DHT22 packet every 2 s; MPU6050 ~100 Hz stream; zero `[FATAL]` lines in a 10-minute run.
- A4: LCD shows boot splash then rotating readings at ≤ 2 Hz; LED flickers with output.
- A5: DIAG snapshot every 10 s shows queue depths near 0 and stack HWMs > 32 words for all tasks.
- A6 (fault demo): infinite loop inserted in any task → `[FATAL] … Hung Mask: 0xXX` within ~3 s, LED halt visible.
- A7 (recovery demo): breaking/restoring MPU SDA → `ERR_I2C_NACK` logged, recovery runs, stream resumes without reboot.

### 10.3 Acceptance criteria (real hardware, IWDG armed)
- A8: Same as A1–A5 over USB-serial; watchdog resets the MCU ≤ 4 s after an induced hang; uptime restarts cleanly.

---

## 11. Known Constraints & Risks

| Risk | Mitigation in v1.0 |
|---|---|
| Wokwi does not simulate IWDG resets | IWDG behind `ENABLE_IWDG`; off in sim by design |
| Wokwi DWT support partial | DWT self-test + busy-loop fallback in DHT22 driver |
| Wokwi 5V rail undriven | LCD powered from 3V3 in diagram (README documents real-HW guidance) |
| USART1 output invisible in sim | Explicit `$serialMonitor` wiring + `display: always` |
| DHT22 critical section (~6 ms) delays tick | Accepted; absolute-delay scheduling compensates |
| Output queue drops under burst | Drop is by-design (freshness-first); drops counted via `ERR_QUEUE_FULL` |
| LCD failure masks telemetry | Health gating decoupled from LCD (FR-8) |

---

## 12. Roadmap (future phases)

- **Phase 2 — Connectivity**: MQTT via external Wi-Fi/MAC-PHY coprocessor (USART/SPI), Nanopb-based binary telemetry (static pools), DMA offloading for ADC/UART.
- **Phase 3 — Field hardening**: Custom bare-metal bootloader + OTA A/B update, tickless idle for battery nodes, persistent error log in flash, sensor pluggability via descriptor table.
- **Phase 4 — Fleet**: Protocol buffer schema versioning, remote diagnostics commands over MQTT, provisioning workflow.

---

## 13. Success Metrics (v1.0)

- 100 % of functional requirements FR-1…FR-16 implemented and demoed in simulation. ✅
- Zero heap violations detected by hook/grep. ✅
- 10-minute Wokwi run with zero watchdog false-positives. ✅ (post-fix validation run)
- Memory within budget (NFR-2). ✅
- CI green on push (build + simulation artifacts). ✅

---

*Document generated from the audited codebase — all figures match the shipped implementation (commit series ending `540d14c`).*
