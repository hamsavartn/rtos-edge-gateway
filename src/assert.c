#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

// Loud assert: a silent spin here would masquerade as any other boot hang.
// UART_Driver_PrintRaw is mutex-free and works with interrupts masked.
extern void UART_Driver_PrintRaw(const char *s);
extern void LED_FatalBlink(void);

void vAssertCalled(const char *file, uint32_t line)
{
    UART_Driver_PrintRaw("\r\n[FATAL] ASSERT: ");
    UART_Driver_PrintRaw(file);
    UART_Driver_PrintRaw(":");
    // line as decimal (avoid stdio here)
    char buf[11];
    int i = 10;
    buf[10] = '\0';
    if (line == 0) { buf[i--] = '0'; }
    while (line > 0 && i >= 0) { buf[i--] = (char)('0' + (line % 10)); line /= 10; }
    UART_Driver_PrintRaw(&buf[i + 1]);
    UART_Driver_PrintRaw("\r\n");

    // Name the asserting task when the scheduler is up (prvTaskExitError
    // asserts reveal which task function returned instead of looping).
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        UART_Driver_PrintRaw("[FATAL] ASSERT in task: ");
        UART_Driver_PrintRaw(pcTaskGetName(NULL));
        UART_Driver_PrintRaw("\r\n");
    }

    LED_FatalBlink();
}
