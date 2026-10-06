---
name: bare-metal-memory-constraints
description: Explicitly bans dynamic memory allocation (new/malloc) in favor of static allocation.
---
# Bare-Metal Memory Constraints
This project enforces a strict ZERO-ALLOCATION policy.
1. **Forbidden Keywords**: Do not use `new`, `delete`, `malloc`, `calloc`, `realloc`, or `free`.
2. **Static Buffers**: All buffers, queues, tasks, and state variables must be allocated statically (e.g., global scope, `static` keyword in functions, or class members instantiated statically).
3. **Object Lifetimes**: Design objects to exist for the entire lifecycle of the application.
4. **Standard Containers**: Do not use dynamically resizing containers like `std::string` or `std::vector`.
