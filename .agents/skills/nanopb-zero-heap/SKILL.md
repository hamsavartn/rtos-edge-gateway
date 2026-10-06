---
name: nanopb-zero-heap
description: Best practices for implementing Nanopb (Protocol Buffers) without dynamic memory allocation.
---
# Nanopb Zero-Heap Serialization
1. **Definition**: Define `.proto` files with explicit `max_size` and `max_count` for all strings/bytes/repeated fields. This forces nanopb to generate fixed-size C structs.
2. **Allocation**: Declare instances of the generated structs locally on task stacks or statically in `.bss`.
3. **Encoding**: Use `pb_ostream_from_buffer` pointing to a statically allocated `uint8_t` array.
4. **Routing**: Send the encoded byte buffer and its length over a static FreeRTOS queue to the network/UART task. No `malloc` is required.
