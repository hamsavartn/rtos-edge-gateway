import shutil, os
base = ".pio/libdeps/bluepill_f103c8/FreeRTOS-Kernel/portable"
keep_top = {"GCC", "MemMang", "Common"}

if os.path.isdir(base):
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
            if item != "heap_4.c":
                os.remove(os.path.join(heap_path, item))
