#include <stdint.h>

void vAssertCalled(const char *file, uint32_t line)
{
    (void)file;
    (void)line;
    while(1) {}
}
