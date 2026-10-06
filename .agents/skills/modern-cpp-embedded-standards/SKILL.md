---
name: modern-cpp-embedded-standards
description: Enforces C++17/C++20 standards for embedded systems, disabling exceptions and RTTI.
---
# Modern C++ for Microcontrollers
When writing C++ code for this project:
1. **No Exceptions**: Assume `-fno-exceptions` is enabled. Never use `throw`, `try`, or `catch`. Use error codes or `std::optional` for error handling.
2. **No RTTI**: Assume `-fno-rtti` is enabled. Do not use `dynamic_cast` or `typeid`.
3. **Compile-Time Evaluation**: Use `constexpr` heavily for constants, lookup tables, and configuration parameters to save flash and RAM.
4. **Standard Library**: Prefer `std::array` over raw C-arrays. Avoid `std::vector` or `std::string` unless using a custom static allocator.
