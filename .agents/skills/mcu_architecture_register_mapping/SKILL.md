---
name: mcu_architecture_register_mapping
description: Hardware Architecture & Register Mapping for microcontroller diagnostic lookups.
---

# Hardware Architecture & Register Mapping

## Implementation Directives
- Look up exact bit-masks and offset addresses dynamically without hallucinating raw memory layouts.
- You must refer to precise technical documents and memory-map boundaries from the silicon vendor's documentation portal (such as STMicroelectronics STM32 Reference Manuals or Espressif Technical Reference Manuals).
- Treat these instructions as your Retrieval-Augmented Generation (RAG) tool equivalent for looking up exact register maps to avoid hallucination.
