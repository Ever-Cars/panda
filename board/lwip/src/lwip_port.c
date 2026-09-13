#include "stm32h7xx_hal.h"

extern uint32_t microsecond_timer_get(void);   // panda, drivers/timers.h (TIM at 1MHz, wraps ~71.6min)
// referenced by linked-but-unused hal_rcc.c code (no --gc-sections)
uint32_t SystemCoreClock = 240000000U;
uint32_t SystemD2Clock  = 120000000U;
const uint8_t D1CorePrescTable[16] = {0,0,0,0,1,2,3,4,1,2,3,4,6,7,8,9};
uint32_t uwTickPrio = 0U;

// wrap-safe ms tick; called only from the main-loop polling context.
// Works before enable_interrupts(): TIM2 free-runs, it is not interrupt-driven.
uint32_t HAL_GetTick(void) {
  return microsecond_timer_get() / 1000U;
}

void HAL_Delay(uint32_t d) {
  uint32_t s = HAL_GetTick();
  while ((HAL_GetTick() - s) < (d + 1U));
}

HAL_StatusTypeDef HAL_InitTick(uint32_t prio) {
  (void)prio;
  return HAL_OK;
}