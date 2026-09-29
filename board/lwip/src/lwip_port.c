#include "stm32h7xx_hal.h"

// referenced by linked-but-unused hal_rcc.c code (no --gc-sections)
uint32_t SystemCoreClock = 240000000U;
uint32_t SystemD2Clock  = 120000000U;
const uint8_t D1CorePrescTable[16] = {0,0,0,0,1,2,3,4,1,2,3,4,6,7,8,9};
uint32_t uwTickPrio = 0U;
volatile uint32_t systick = 0U; // used by HAL_GetTick() and HAL_Delay() to provide a millisecond tick

void SysTick_Handler(void) {
  systick++;
}

uint32_t HAL_GetTick(void) {
  return systick;
}

void HAL_Delay(uint32_t d) {
  uint32_t s = HAL_GetTick();
  while ((HAL_GetTick() - s) < (d + 1U));
}

HAL_StatusTypeDef HAL_InitTick(uint32_t prio) {
  (void)prio;
  return HAL_OK;
}
