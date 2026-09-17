#include <stdint.h>
#include <stdio.h>

static uint32_t fake_ms = 0;

void lte_hw_init(void)
{
    fake_ms = 0;
    printf("[TEST HW INIT]\n");
}

uint32_t lte_hw_ms(void)
{
    return fake_ms;
}

void lte_hw_delay_ms(uint32_t ms)
{
    fake_ms += ms;
}
