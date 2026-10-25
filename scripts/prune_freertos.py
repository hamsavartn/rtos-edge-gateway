import shutil, os, glob

# Prune + patch every environment's FreeRTOS-Kernel copy (pre-script runs per
# env, and libdeps live under .pio/libdeps/<env>/).
for base in glob.glob(".pio/libdeps/*/FreeRTOS-Kernel/portable"):
    keep_top = {"GCC", "MemMang", "Common"}

    def rmtree_safe(path):
        # Pruning is a size optimization only — never fail the build over a
        # locked or read-only folder (Windows denies access on .github etc.).
        try:
            if os.path.isdir(path):
                shutil.rmtree(path, ignore_errors=True)
        except OSError as exc:
            print("[prune_freertos] WARNING: could not remove %s (%s)" % (path, exc))

    for item in os.listdir(base):
        if item not in keep_top:
            rmtree_safe(os.path.join(base, item))

    gcc_path = os.path.join(base, "GCC")
    if os.path.isdir(gcc_path):
        for item in os.listdir(gcc_path):
            if item != "ARM_CM3":
                rmtree_safe(os.path.join(gcc_path, item))

    heap_path = os.path.join(base, "MemMang")
    if os.path.isdir(heap_path):
        for item in os.listdir(heap_path):
            file_path = os.path.join(heap_path, item)
            if os.path.isfile(file_path):
                try:
                    os.remove(file_path)
                except OSError as exc:
                    print("[prune_freertos] WARNING: could not remove %s (%s)" % (file_path, exc))

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
        patched = False
        if "sim-compat" not in src and probe_marker in src:
            src = src.replace(probe_marker, sim_compat_patch, 1)
            patched = True
            print("[prune_freertos] applied simulator-compat patch (IPR probe) to " + port_c)

        # -------------------------------------------------------------------
        # Sim-compat patch #2: SVC-free first task start.
        # The simulator mishandles the SVC exception-return sequence
        # (bx EXC_RETURN to thread mode/PSP): control lands on the task's
        # return address (prvTaskExitError) instead of the task function.
        # Replace prvPortStartFirstTask's `svc 0` with a manual thread-mode
        # restore: r4-r11 from the frame, PSP = frame base, basepri unmasked,
        # then R0/LR/PC loaded and branch into the task (Thumb bit set).
        # R1/R2/R3/R12 are left as garbage — identical to real silicon, where
        # those frame slots are stale filler at task entry.
        # -------------------------------------------------------------------
        svc_line_key = '" svc 0'
        svc_compat_lines = [
            '                /* [rtos-edge-gateway sim-compat2] manual first-task start:',
            '                 * the simulator\'s SVC exception return lands on the task',
            '                 * return address instead of the task function. */',
            '                " ldr r3, =pxCurrentTCB             \\n"',
            '                " ldr r1, [r3]                      \\n"',
            '                " ldr r0, [r1]                      \\n" /* pxTopOfStack (R4 slot)   */',
            '                " ldmia r0!, {r4-r11}               \\n" /* restore r4-r11           */',
            '                " msr psp, r0                       \\n" /* PSP = frame base (R0)    */',
            '                " mov r2, r0                        \\n" /* frame base (scratch)     */',
            '                " movs r0, #0                       \\n"',
            '                " msr basepri, r0                   \\n" /* unmask interrupts        */',
            '                " ldr r1, [r2, #24]                 \\n" /* task PC (bit0 cleared)   */',
            '                " adds r1, r1, #1                   \\n" /* Thumb interworking       */',
            '                " ldr lr, [r2, #20]                 \\n" /* task LR                  */',
            '                " ldr r0, [r2, #0]                  \\n" /* task R0 = pvParameters   */',
            '                " bx r1                             \\n" /* enter the task           */',
        ]
        if "sim-compat2" not in src and svc_line_key in src:
            out_lines = []
            for line in src.splitlines():
                if svc_line_key in line and "System call" in line:
                    out_lines.extend(svc_compat_lines)
                else:
                    out_lines.append(line)
            src = "\n".join(out_lines) + "\n"
            patched = True
            print("[prune_freertos] applied simulator-compat2 patch (SVC-free start) to " + port_c)
        elif "sim-compat2" in src:
            print("[prune_freertos] sim-compat2 already present: " + port_c)

        if "sim-compat" in src:
            with open(port_c, "w", encoding="utf-8", newline="") as f:
                f.write(src)
