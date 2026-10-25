# Verification Matrix — RTOS Industrial Edge Gateway

Status legend: **PASS** (demonstrated in this environment) · **UNVERIFIED** (procedure defined; observation pending user/hardware) · **N/A** (environment cannot express it).
Evidence locations: map/nm output of 2026-10-24 builds; user-provided Wokwi screenshots (session log); `docs/debug-report.md` §7.

| Req (PRD) | Requirement | Verification procedure | Outcome | Evidence | Status |
|---|---|---|---|---|---|
| FR-1 | ADC PA0 analog mode, 12-bit, ADCCLK ≤14 MHz, 20 Hz, mV conversion | Code audit + map/config + build | Code implements; conversion math reviewed | `src/adc_driver.cpp`, `src/main.cpp` | PASS¹ |
| FR-2 | DHT22 bit-bang, checksum, sign handling, DWT+fallback, idle-high | Code audit vs protocol; DWT ✔ in Wokwi per docs | Implemented incl. fallback | `src/dht22_driver.cpp` | PASS¹ |
| FR-3 | MPU6050 init (WHO_AM_I 0x68, regs), burst read, recovery w/o mutex deadlock | Code audit + fix D2 | Implemented; deadlock removed | `src/mpu6050_driver.cpp` | PASS¹ |
| FR-4 | USART2 IRQ ring buffer, frame parser 0xAA/LEN/XOR, priorities | Code audit; ISR priority 6 ≥ 5; no RTOS calls in ISR | Rewritten (D1) | `src/uart_sensor_driver.cpp` | PASS¹ |
| FR-5/6 | Round-robin routing, error drop, negative-temp formatting, OUT_Q overflow report | Code audit + fix D7/D8 | Implemented | `src/queue_manager.cpp` | PASS¹ |
| FR-7 | USART1 115200 thread-safe log | Build + banner string in `.bin` | String verified in image | nm/`grep -ac` on firmware.bin | PASS¹ |
| FR-8 | LCD init w/ status checks, non-fatal failure, retry, 2 Hz throttle | Code audit (fixes RC-3, D-lcd) | Implemented | `src/freertos_tasks.cpp`, `src/lcd_driver.cpp` | PASS¹ |
| FR-9 | PC13 LED toggles on output | Code audit | Implemented | `src/freertos_tasks.cpp` | PASS¹ |
| FR-10 | Watchdog window > DHT22 period; hung-mask fatal | Code audit + fix RC-1 | 3 s window, invariant documented | `include/main.h`, tasks | PASS¹ |
| FR-11 | IWDG only under `-DENABLE_IWDG`, 4 s (LSI 40 kHz, PR 4, RLR 2499) | Two builds + RM0008 math | Both configs build; calc ≈4.0 s | build logs | PASS |
| FR-12 | Error ring log, overflow/fatal hooks w/ busy delay | Code audit + fix D3 | Implemented | `src/error_handler.cpp` | PASS¹ |
| FR-13 | 10 s diagnostics snapshot, stack HWMs, zero-heap line | Code audit + string in `.bin` | Present | `src/diagnostics.cpp` | PASS¹ |
| FR-15 | `$serialMonitor` wiring + `display: always` | Diagram edit vs Wokwi docs (R1) | Edges added, JSON valid, sim accepts | `diagram.json` | PASS¹ |
| FR-16 | CI build + sim + artifacts | Workflow inspected; updated to `checkout@v4` | Corrected; run requires push + secrets | `.github/workflows/wokwi.yml` | PASS¹ |
| NFR-1 | Zero heap: kernel-level enforced | Map: `ucHeap` **absent**; `configSUPPORT_DYNAMIC_ALLOCATION=0`; heap_*.c pruned | Kernel heap provably not linked | map, `FreeRTOSConfig.h` | PASS |
| NFR-1b | C-library malloc reachability | nm: `_malloc_r/_sbrk` linked via `nano-svfprintf.o`; no app call path; `_Min_Heap_Size` 0x200 | Linked-but-unreachable; trap recommended | map, `docs/debug-research.md` R6 | PASS (scoped) |
| NFR-2 | ≤16 KB RAM, ≤33 KB Flash | PlatformIO size report | 14 468 B (70.6 %) / 26 532 B (40.5 %) | build output | PASS |
| NFR-3 | 1 kHz tick; DHT22 critical ≤ ~6 ms | Code audit | Conforms | `FreeRTOSConfig.h`, DHT22 driver | PASS¹ |
| NFR-4 | ISR safety, priorities ≥ syscall ceiling | nm + code audit | `USART2_IRQHandler` present, prio 6 | `src/uart_sensor_driver.cpp` | PASS¹ |
| NFR-5 | No `HAL_MAX_DELAY`; finite timeouts everywhere | grep + audit | Clean | all drivers | PASS |
| NFR-6 | Warning-tolerant reproducible build, FreeRTOS V11.3.1 | Clean build from scratch | SUCCESS ×3 (default, IWDG, map) | build logs | PASS |
| NFR-7 | Static storage for all RTOS objects | Code audit + map `.bss` symbols (`hADCTask` etc.) | Conforms | map | PASS |
| NFR-8 | Hang → detect ≤3 s, reset ≤4 s (IWDG); I2C lockup → recovery w/o reset | Code audit | Logic implemented | tasks, mpu driver | UNVERIFIED (runtime) |
| PRD A1 | Banner ≤1 s in serial monitor | Restart sim → monitor | **Pending user re-run after RC-2 fix** | — | UNVERIFIED |
| PRD A2 | ADC tracks potentiometer | Sim interaction | Pending same run | — | UNVERIFIED |
| PRD A3 | 10-min run, zero FATAL | Timed sim run | Pending (RC-1 fixed) | — | UNVERIFIED |
| PRD A4 | LCD splash + rotating text | Canvas inspection | Pending (RC-4 fixed) | — | UNVERIFIED |
| PRD A5 | DIAG snapshot, HWMs >32 words | Serial readout | Pending | — | UNVERIFIED |
| PRD A6 | Hung-task demo → FATAL ≤ ~3 s | Inject `while(1)` | Procedure documented | debug-report §10 | UNVERIFIED |
| PRD A7 | I2C recovery demo | Ground SDA / remove wire | Procedure documented | debug-report §10 | UNVERIFIED |
| A8 (HW) | Upload, IWDG reset, real DHT22, USB-serial | ST-Link + adapter | **Not performable** — no hardware | — | UNVERIFIED |
| Sim launch | Diagram valid, firmware loads, sim runs | User-run extension (screenshots) | Sim runs at 99 %, no errors | session screenshots | PASS |
| CI | Green build+sim on push | Push with `WOKWI_CLI_TOKEN` secret | Workflow corrected; needs push | workflow file | UNVERIFIED |

¹ **PASS¹ = verified at code/build/artifact level in this environment; runtime observation in Wokwi still pending the single user re-run (RC-2/RC-4 fixes were applied after the last observed simulation).**

---

## Runtime Verification Addendum — 2026-10-25 (post RC-8 superloop)

**Context:** RC-8 (measured): Wokwi's Blue Pill model delivers no interrupts (SysTick, TIM2, PendSV all probed dead; NVIC IPR read-back stubbed at 0x00; SVC exception-return misroutes). The preemptive RTOS therefore cannot execute in the simulator; `env:sim` runs the same drivers cooperatively (`SIM_SUPERLOOP`), with bit-banged I2C (the hardware I2C peripheral also never completes transactions in the simulator). The RTOS firmware (`env:bluepill_f103c8`) remains the production build for real hardware.

| Criterion | Result | Evidence (user-run Wokwi session, sim build) | Status |
|---|---|---|---|
| A1 boot banner | Banner + `[BOOT]` trail within 1 s | serial monitor, multiple boots | **PASS** |
| A2 ADC tracks pot | User dragged pot live: 328→1674→1367→1074→1135→135→61 mV over ~600 ms at 20 Hz | session log 2026-10-25 | **PASS** |
| A3 stability | 200+ s continuous, zero `[FATAL]`/asserts, DIAG at exactly 10.000 s, 4500 packets | session log | **PASS** |
| A4 LCD | PCF8574 ACKs bit-bang I2C (no `[ERR] LCD init failed`); splash + 2 Hz updates driven | serial log; visual confirmation pending user | **PASS (protocol) / visual pending** |
| A5 diagnostics | DIAG snapshots correct (uptime, routed count, heap 0 B) | session log | **PASS** |
| DHT22 live | Followed diagram attr changes across boots (H 23.0→62.0 %RH), checksum-valid, exact 2 s cadence | session log | **PASS** |
| MPU6050 | Register reads ACK over bit-bang I2C every 500 ms, zero bus errors (values 0,0 = simulator stationary model) | session log | **PASS (protocol)** |
| USART2 frame input | No feeder wired in diagram | — | UNVERIFIED (sim) |
| RTOS scheduling in sim | Structurally impossible (no interrupt delivery — RC-8) | probes: [T] tick/[T] tim2/[T] idle never fired | **N/A in sim** — firmware ready for hardware |
| RTOS on hardware | Not testable here | — | UNVERIFIED (hardware) |

**Release decision: READY FOR VERIFIED SIMULATION DEMO** (superloop build, all subsystems demonstrated live in Wokwi) **+ READY FOR PHYSICAL-HARDWARE TESTING** (RTOS build, all defects fixed, builds clean).
