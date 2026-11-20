#include "timebase.h"
#include "board_config.h"

static volatile uint32_t g_millis;

void timebase_init(void)
{
    g_millis = 0U;
    (void)SysTick_Config(SystemCoreClock / SYSTICK_HZ);
}

void timebase_systick_irq(void)
{
    ++g_millis;
}

uint32_t timebase_millis(void)
{
    return g_millis;
}

void timebase_delay_ms(uint32_t ms)
{
    uint32_t start = timebase_millis();
    while ((uint32_t)(timebase_millis() - start) < ms) {
        __NOP();
    }
}

uint8_t timebase_expired(uint32_t now, uint32_t start, uint32_t timeout_ms)
{
    return ((uint32_t)(now - start) >= timeout_ms) ? 1U : 0U;
}
