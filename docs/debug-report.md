# Forensic Debug Report — RTOS Industrial Edge Gateway

**Date:** 2026-10-24 · **Scope:** full repository audit, repair, and verification
**Artifacts:** `docs/debug-research.md` (sources) · `docs/verification-matrix.md` (requirement matrix) · `prd.md` (requirements)

---

## 1. Executive Summary

The firmware was **not failing to boot** — it was failing to be *observable*. The investigation established three independent, confirmed root causes that together produced "no useful output, no LCD": a watchdog timing bug that halted a healthy system ~3 s after boot, a missing Wokwi serial-monitor connection that sent all UART output to nothing, and health-reporting logic that let one dark LCD starve the entire gateway. Ten further latent logic defects were found and repaired. All fixes build clean from scratch; the final observed-runtime confirmation in Wokwi and all physical-hardware criteria remain **explicitly unverified** (see §9).

## 2. Environment Tested

- OS: Windows 10.0.26300 x64, Git Bash; VS Code + PlatformIO IDE.
- PlatformIO Core 6.1.19; `ststm32` 20.0.0; `framework-stm32cubef1` 1.8.7; `toolchain-gccarmnoneeabi` 7.2.1.
- FreeRTOS-Kernel **V11.3.1** (pinned git tag; was V11.1.0).
- Wokwi CLI 0.28.1 (Windows) executed locally — blocked by missing `WOKWI_CLI_TOKEN` (user secret); VS Code extension used by the user for interactive runs.
- Physical hardware: **not available** in this environment.

## 3. Original Symptoms

1. "No useful output" when running via PlatformIO/Wokwi.
2. LCD never visibly turns on.
3. No indication whether the MCU booted or the scheduler started.

## 4. Confirmed Root Causes

### RC-1 — Watchdog guaranteed false-trip on a healthy system (system-killing)
- **Symptom evidence:** code inspection; consistent with "nothing works" after ~2–3 s of sim runtime.
- **Mechanism:** `vTaskWatchdog` waited only **1.5 s** for all 8 health bits, but the DHT22 task can check in at most every **2 s** (sensor's minimum conversion interval). On timeout the wait clears all delivered bits, so the system repeatedly fatal-halted in a loop.
- **File:** `src/freertos_tasks.cpp`, `include/main.h`.
- **Fix:** window raised to `WATCHDOG_WINDOW_MS` = 3000 ms > DHT22 2 s; documented invariant DHT22 2 s < window 3 s < IWDG 4 s. Fatal spin now also blinks the LED for visibility.
- **Test:** rebuild ✅; a full 10-minute sim observation remains user-side (§9).

### RC-2 — Wokwi serial monitor never connected (the "no output" root cause)
- **Symptom evidence:** user screenshots: sim running at 99 % for minutes, terminal tabs empty; firmware verified bootable (vector table, strings) yet zero output anywhere.
- **Mechanism:** unlike Arduino-style boards, the Wokwi Blue Pill does not auto-route UART to the serial monitor; `diagram.json` contained **no** `$serialMonitor` connection, so USART1 (PA9) transmitted into nothing.
- **File:** `diagram.json`.
- **Fix:** added `["mcu:A9", "$serialMonitor:RX"]`, `["$serialMonitor:TX", "mcu:A10"]`, and `"serialMonitor": { "display": "always" }` so the monitor opens with the simulation.
- **Source:** docs.wokwi.com/guides/serial-monitor (see `docs/debug-research.md` R1).

### RC-3 — LCD failure starved the watchdog (single-point-of-failure design)
- **Symptom evidence:** LCD blank in every user screenshot while sim time advanced.
- **Mechanism:** `vTaskOutputManager` only set its health bit when LCD writes succeeded — one dark/absent LCD froze the whole gateway by design.
- **File:** `src/freertos_tasks.cpp`.
- **Fix:** LCD failure is now a **non-fatal HMI fault**: reported once per episode (`LCD_INIT`/`LCD_WRITE` → `ERR_I2C_NACK`), retried each 500 ms window, health bit always set by the live task. LCD init sequence also now checks every command's status.

### RC-4 — LCD unpowered in simulation
- **Symptom evidence:** LCD was the only part on the Blue Pill's 5 V rail; pot/MPU/DHT22 (3V3) behaved, LCD never did.
- **Mechanism:** the simulated board's 5 V rail is undriven without a USB source.
- **File:** `diagram.json`.
- **Fix:** LCD backpack moved to 3V3 (standard for 3.3 V MCU sims); real-hardware guidance documented in README/PRD.

## 5. Additional Defects Found and Repaired

| # | Defect | Mechanism | Fix (file) |
|---|---|---|---|
| D1 | USART2 frame loss by design | Task polled RX every 100 ms; F103 UART has no FIFO → overrun of any off-schedule frame | IRQ-driven 128-byte SPSC ring buffer, ISR prio 6, re-arm on RX-complete **and** error callbacks (`src/uart_sensor_driver.cpp`) |
| D2 | I2C recovery could never succeed | `MPU6050_Recover()` held the non-recursive I2C mutex while re-running `MPU6050_Init()`, whose helpers re-take it → every transfer timed out | Mutex released before re-init (`src/mpu6050_driver.cpp`) |
| D3 | Fault handlers hung silently | `HAL_Delay()` inside `taskDISABLE_INTERRUPTS()` — HAL tick never increments → first delay call never returns | Busy-loop `fault_delay()` in fatal/overflow paths (`src/error_handler.cpp`) |
| D4 | ADC out of spec + floating input | PA0 never set to analog mode; ADCCLK left at reset default PCLK2/2 = 32 MHz (> 14 MHz limit) | PA0 `GPIO_MODE_ANALOG`; `RCC_ADCPCLK2_DIV6` ≈ 10.67 MHz (`src/adc_driver.cpp`, `src/main.cpp`) |
| D5 | DHT22 could freeze the whole RTOS | `delay_us` spun on DWT CYCCNT inside a critical section; a dead counter = infinite loop with interrupts masked | One-time DWT self-test + calibrated busy-loop fallback (`src/dht22_driver.cpp`) |
| D6 | DHT22 bus held low at boot | ODR defaults low; open-drain config = permanent start signal | Pin released idle-high in `GPIO_Init` (`src/main.cpp`) |
| D7 | Negative temperatures printed `T:-5.-2C` | `/10` and `%10` of a negative fixed-point value printed a negative fraction | Sign split from fraction (`src/queue_manager.cpp`) |
| D8 | Silent output-queue overflow | `xQueueSend(hOutputQueue, …, 0)` unchecked | Drop + `ErrorHandler_Report(ERR_QUEUE_FULL, "OUT_Q")` |
| D9 | printf format/argument mismatches | `%lu`/`%u` with `uint32_t`/`uint16_t`/enum args on ARM EABI | Explicit casts at every call site (4 files) |
| D10 | Stale/wrong docs and config drift | Banner "72 MHz" (actual 64), README queue depth 32 (actual 16), stale `.gitattributes`, deprecated CI `checkout@v3` | Corrected throughout; FreeRTOS bumped V11.1.0→V11.3.1; unused timer service disabled (saves ~1.4 KB RAM); `monitor_speed` added |

## 6. Files Modified (cumulative, commits `8570104`, `540d14c`, `b364747`)

`include/main.h`, `include/FreeRTOSConfig.h`, `src/main.cpp`, `src/freertos_tasks.cpp`, `src/queue_manager.cpp`, `src/error_handler.cpp`, `src/uart_driver.cpp`, `src/uart_sensor_driver.cpp` (rewritten), `src/adc_driver.cpp`, `src/dht22_driver.cpp`, `src/mpu6050_driver.cpp`, `src/lcd_driver.cpp`, `src/diagnostics.cpp`, `diagram.json`, `platformio.ini`, `README.md`, `.gitignore`, `.gitattributes`, `.github/workflows/wokwi.yml`; deleted junk (`wokwi-cli` "Not Found" file, `.playwright-mcp/`); removed accidental nested-repo gitlink. Docs added: `prd.md`, `docs/*`.

## 7. Tests Performed and Actual Outcomes

| Test | Command / method | Outcome |
|---|---|---|
| Clean build (default) | delete `.pio/build` → `pio run` | ✅ SUCCESS — RAM 14 468 B (70.6 %), Flash 26 532 B (40.5 %) |
| Build with `-DENABLE_IWDG` | `PLATFORMIO_BUILD_FLAGS="-DENABLE_IWDG" pio run` | ✅ SUCCESS |
| Map-file regeneration & audit | `-Wl,-Map=…` link | ✅ `.isr_vector 0x08000000 0x10c`; `ucHeap` absent; `_malloc_r` pull-in traced to `libc_nano.a(nano-svfprintf.o)` via `snprintf` |
| Symbol audit | `arm-none-eabi-nm firmware.elf` | ✅ `Reset_Handler`/`SysTick_Handler`/`USART2_IRQHandler`/`xTaskCreateStatic`/`vTaskStartScheduler` all linked; `_malloc_r/_sbrk` present but no application call path |
| Zero-heap policy | grep + config audit + map | ✅ kernel-level enforced (see R6 for C-library nuance) |
| Firmware freshness & content | `grep -ac` for banner/diag strings; timestamps | ✅ `.bin` newer than all sources; all fixed strings present |
| Wokwi simulation **launch** | user-run VS Code extension | ✅ sim runs (timer advances, 99 % speed, no diagram errors) |
| Wokwi serial output **observation** | serial monitor after `$serialMonitor` fix | ⏳ **PENDING user re-run** — fix committed, not yet observed |
| LCD rendering in sim | canvas inspection | ⏳ **PENDING** (RC-4 fix applied, unobserved) |
| Watchdog fault demo | controlled `while(1)` injection | ⏳ UNVERIFIED runtime (code path reviewed) |
| Physical hardware criteria | ST-Link + USB-UART | ❌ **NOT PERFORMED** — no hardware available |

## 8. Commands to Reproduce

```bash
# Build (Path A — PlatformIO)
pio run                                   # → .pio/build/bluepill_f103c8/firmware.{bin,elf}

# Wokwi (Path B — interactive)
# VS Code → F1 → "Wokwi: Start Simulation" → serial monitor opens automatically

# Wokwi (Path B — CLI, requires WOKWI_CLI_TOKEN)
wokwi-cli --timeout 15000 --serial-log-file serial-output.log --screenshot-file board-state.png

# Real hardware (requires ST-Link + USB-UART adapter on PA9/PA10, GND common)
# 1) uncomment -DENABLE_IWDG in platformio.ini   2) pio run -t upload   3) pio device monitor
```

## 9. Remaining Limitations

1. **Post-fix Wokwi runtime observation** (serial banner + LCD) — requires the user to restart the simulation; all pre-requisites verified.
2. **Physical-hardware criteria** (upload, IWDG reset behavior, DHT22 on real silicon, USB-serial) — UNVERIFIED; manual procedures in `docs/verification-matrix.md`.
3. **C-library malloc machinery** linked-but-unreachable (R6) — optional `_sbrk` trap recommended for absolute enforcement.
4. Wokwi cannot emulate IWDG resets or DMA — structural simulator limits, documented.

## 10. Manual Steps for Untestable Hardware Behavior

1. Wire USB-UART adapter: PA9→RX, PA10→TX, GND↔GND (3.3 V levels only).
2. Flash: `pio run -t upload` (ST-Link). Uncomment `-DENABLE_IWDG` first for watchdog validation.
3. Observe 115200 8N1 monitor for banner/telemetry (PRD A1–A5 equivalents).
4. Watchdog test: add `while(1){}` in any sensor task → expect `[FATAL] WATCHDOG TIMEOUT. Hung Mask: 0xXX` within ~3 s and MCU reset within ~4 s.
5. I2C recovery test: ground MPU SDA briefly → expect `ERR_I2C_NACK`, recovery, stream resumption without reset.
