# Debug Research — Verified Findings & Sources

Project: RTOS-Based Industrial Edge Gateway (STM32F103C8T6 + FreeRTOS + PlatformIO + Wokwi)
Compiled: 2026-10-24 (access dates below). Sources are listed only if actually consulted this session; claims are scoped to what each source actually supports.

---

## R1. Wokwi serial monitor requires explicit wiring for the Blue Pill

- **Exact claim:** On Wokwi, the Serial Monitor is not automatically connected for all boards; non-Arduino-style boards (such as the STM32 Blue Pill) need the UART pins connected to the virtual `$serialMonitor` endpoints in `diagram.json`, and the monitor's display behavior can be forced via the `serialMonitor` config section.
- **Source:** Wokwi Docs — "The Serial Monitor" guide — https://docs.wokwi.com/guides/serial-monitor (accessed 2026-10-24, fetched full page).
- **Relevance:** Explains why a correctly running firmware produced no visible output: `diagram.json` had no `$serialMonitor` connection for USART1 (PA9/PA10).
- **Caveat:** The guide's examples are Arduino-centric; the `$serialMonitor` endpoint syntax is documented generically for `connections`. Verified working by the simulator accepting the added edges (valid-diagram check on sim start).

## R2. Wokwi Blue Pill peripheral support matrix

- **Exact claim:** For the simulated Blue Pill: USART ✔ implemented; **IWDG ✗ not implemented**; **DWT ✔ implemented**; DMA ✗; RCC ✔; ADC1 basic ✔.
- **Source:** Wokwi Docs — "STM32 Blue Pill" part page — https://docs.wokwi.com/parts/board-stm32-bluepill (accessed 2026-10-24, fetched full page).
- **Relevance:**
  - Justifies keeping the hardware IWDG behind the `-DENABLE_IWDG` build flag and off in simulation (FR-11, PRD §11).
  - Confirms DWT-based microsecond timing (DHT22 driver) is supported in simulation; the code's busy-loop fallback remains as defense for other environments.

## R3. FreeRTOS-Kernel versioning

- **Exact claim:** Latest FreeRTOS-Kernel releases: **V11.3.1** (2026-08-21), V11.3.0 (2026-03-30), V11.2.0 (2025-03-04). The ARM_CM3 GCC port remains present in the V11 line.
- **Source:** https://github.com/FreeRTOS/FreeRTOS-Kernel/releases (accessed 2026-10-24).
- **Relevance:** Project was pinned to V11.1.0; bumped to V11.3.1 and verified by a from-scratch build (`portable/GCC/ARM_CM3/port.o` compiled, firmware links).

## R4. Wokwi CLI asset naming and token requirement

- **Exact claim:** wokwi-cli latest release v0.28.1 publishes `wokwi-cli-win-x64.exe` (a bare `.exe`, not a `.zip`); the CLI refuses to run without `WOKWI_CLI_TOKEN`.
- **Source:** GitHub API `repos/wokwi/wokwi-cli/releases/latest` (accessed 2026-10-24); CLI `--version` executed locally (0.28.1).
- **Relevance:** (a) A stray 9-byte `wokwi-cli` file containing "Not Found" in the repo root was the residue of a download using a wrong asset name — deleted. (b) Documents why full simulation runs require the user's token (CI secret) or the VS Code extension.

## R5. STM32F103 engineering limits (RM0008-based)

- **Claims used (authoritative reference: ST RM0008, STM32F103x8/xB reference manual; also STM32F103C8 datasheet):**
  - ADC input clock must be ≤ 14 MHz. Reset default ADCPRE = PCLK2/2 = 32 MHz at 64 MHz PCLK2 → out of spec; fixed to PCLK2/6 ≈ 10.67 MHz.
  - IWDG runs on the ~40 kHz LSI (with manufacturing tolerance); timeout = (prescaler/reload) derived per RM0008. PR=4 (÷64), RLR=2499 → ≈ 4.0 s.
  - USART baud rate = fPCLK / (USARTDIV×16) for oversampling-16; USART1 is on APB2 (PCLK2 = 64 MHz here), USART2 on APB1 (PCLK1 = 32 MHz). HAL computes BRR from the bus clock — mismatched clock assumptions are a classic silent-baud bug; the clock tree here sets HSI/2 ×16 = 64 MHz SYSCLK, APB1 ÷2 = 32 MHz, APB2 ×1 = 64 MHz.
- **Scope note:** cited as standard reference-manual constraints applied to this clock tree; exact section numbers intentionally not quoted to avoid citing from memory inaccurately.

## R6. Newlib-nano heap pull-in via snprintf (verified locally from the linker map)

- **Exact claim:** The project's `snprintf` usage causes `libc_nano.a`'s `nano-svfprintf.o` to be linked, which references `_malloc_r`; consequently `_malloc_r`, `_sbrk`, `__malloc_lock` and a `_Min_Heap_Size` of 0x200 (512 B) exist in the ELF **even though the application never calls them**.
- **Evidence (this repository, 2026-10-24 build):**
  - Map: `.isr_vector 0x08000000 0x10c` (vector table exactly at flash start).
  - Map: `_malloc_r` pull-in chain — `libc_nano.a(lib_a-nano-svfprintf.o) (_malloc_r)`.
  - `arm-none-eabi-nm firmware.elf`: `_malloc_r/_free_r/_sbrk/__malloc_*` present (C library), but **`ucHeap` (FreeRTOS heap_4) absent** — the kernel heap is provably not linked.
- **Conclusion:** The **FreeRTOS-level zero-heap invariant is fully enforced** (`configSUPPORT_DYNAMIC_ALLOCATION=0`, heap_*.c pruned, no `ucHeap` in the image). The **C-library level** retains unreachable-but-linked malloc machinery; nano-vfprintf only mallocs on specific format paths (float/wide/dynamic-width), none used by this codebase. Optional hardening (not a defect): provide a project `syscalls.c` overriding `_sbrk` to return failure, making any future heap use fail loudly instead of silently succeeding.

## R7. Local toolchain verification facts

- PlatformIO Core 6.1.19; platform `ststm32` 20.0.0; `framework-stm32cubef1` 1.8.7; `toolchain-gccarmnoneeabi` 7.2.1 (all resolved from the actual build output, 2026-10-24).
- Board definition: `bluepill_f103c8` — 64 KB Flash / 20 KB SRAM (official spec; unofficial 128 KB clone flash **not** assumed).
- FreeRTOS-Kernel resolved as `V11.3.1` from the pinned git tag (dependency graph output).
