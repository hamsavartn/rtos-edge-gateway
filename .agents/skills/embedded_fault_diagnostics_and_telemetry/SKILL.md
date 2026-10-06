---
name: embedded_fault_diagnostics_and_telemetry
description: Hardware-Aware Diagnostics & Log Tracing for parsing fault status registers and error patterns.
---

# Hardware-Aware Diagnostics & Log Tracing

## Implementation Directives
- Parse raw text copies of microcontroller fault status registers (like the ARM Cortex-M HardFault Status Register) against your diagnostic knowledge base to pinpoint the exact line of code causing a crash.
- Extract error patterns, register dumps, and stack unwinding protocols based on standard GNU Project Debugger (GDB) documentation.
- Utilize trace log syntax examples akin to commercial RTOS visualization suites (e.g., Percepio Tracealyzer) to understand execution flow and fault telemetry.
