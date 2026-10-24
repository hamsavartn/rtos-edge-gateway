import shutil, os, glob

# Prune + patch every environment's FreeRTOS-Kernel copy (pre-script runs per
# env, and libdeps live under .pio/libdeps/<env>/).
for base in glob.glob(".pio/libdeps/*/FreeRTOS-Kernel/portable"):
    keep_top = {"GCC", "MemMang", "Common"}

    for item in os.listdir(base):
        if item not in keep_top:
            path = os.path.join(base, item)
            if os.path.isdir(path):
                shutil.rmtree(path)

    gcc_path = os.path.join(base, "GCC")
    if os.path.isdir(gcc_path):
        for item in os.listdir(gcc_path):
            if item != "ARM_CM3":
                path = os.path.join(gcc_path, item)
                if os.path.isdir(path):
                    shutil.rmtree(path)

    heap_path = os.path.join(base, "MemMang")
    if os.path.isdir(heap_path):
        for item in os.listdir(heap_path):
            file_path = os.path.join(heap_path, item)
            if os.path.isfile(file_path):
                os.remove(file_path)

    # -----------------------------------------------------------------------
    # Simulator compatibility patch (FreeRTOS-Kernel V11.3.1, ARM_CM3 port.c).
    #
    # xPortStartScheduler() probes the NVIC by writing 0xFF to an interrupt
    # priority register and reading it back. Real STM32F103 silicon implements
    # 4 bits (reads 0xF0). Wokwi's CPU model leaves the register stubbed, so
    # the read-back is 0x00 and the kernel's sanity assert fires at scheduler
    # start — every simulation halts before the first task runs.
    #
    # Degenerate read-back (0) falls back to an 8-bit layout. Conforming
    # silicon never reads back 0, so real hardware is unchanged; the checked
    # configuration is enforced by compile-time guards in FreeRTOSConfig.h.
    # -----------------------------------------------------------------------
    port_c = os.path.join(base, "GCC", "ARM_CM3", "port.c")
    probe_marker = "ucMaxPriorityValue = *pucFirstUserPriorityRegister;"
    sim_compat_patch = (
        probe_marker
        + "\n"
        + "\n"
        + "        /* [rtos-edge-gateway sim-compat] A stubbed NVIC priority register\n"
        + "         * (read-back 0) would fail the sanity asserts below and halt the\n"
        + "         * scheduler. Treat a degenerate read-back as an 8-bit layout so\n"
        + "         * simulation keeps running; conforming silicon (reads e.g. 0xF0\n"
        + "         * on STM32F103) never takes this path. Compile-time guards in\n"
        + "         * FreeRTOSConfig.h preserve the real-hardware configuration check. */\n"
        + "        if( ucMaxPriorityValue == 0U )\n"
        + "        {\n"
        + "            ucMaxPriorityValue = 0xFFU;\n"
        + "        }"
    )

    if os.path.isfile(port_c):
        with open(port_c, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()
        if "sim-compat" not in src and probe_marker in src:
            src = src.replace(probe_marker, sim_compat_patch, 1)
            with open(port_c, "w", encoding="utf-8", newline="") as f:
                f.write(src)
            print("[prune_freertos] applied simulator-compat patch to " + port_c)
        elif "sim-compat" in src:
            print("[prune_freertos] simulator-compat patch already present: " + port_c)
        else:
            print("[prune_freertos] WARNING: probe marker not found in " + port_c)
