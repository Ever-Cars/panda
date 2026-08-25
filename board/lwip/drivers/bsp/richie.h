/**
  ******************************************************************************
  * @file    richie.h
  * @author  MCD Application Team
  * @brief   This file contains definitions for richie:
  *          LEDs
  *          hardware resources.
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef STM32H735G_DK_H
#define STM32H735G_DK_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "richie_conf.h"
#include "richie_errno.h"

typedef enum
{
  LED1 = 0U,
  LED_BLUE = LED1,
  LED2 = 1U,
  LED_RED = LED2,
  LED3 = 2U,
  LED_GREEN = LED3,
  LEDn
} Led_TypeDef;


typedef enum
{
  BUTTON_USER = 0U,
  BUTTONn
} Button_TypeDef;

typedef enum
{
  BUTTON_MODE_GPIO = 0U,
  BUTTON_MODE_EXTI = 1U
} ButtonMode_TypeDef;

/**
  * @brief STM32H735G_DK BSP Driver version number
   */
#define STM32H735G_DK_BSP_VERSION_MAIN   (uint32_t)(0x01) /*!< [31:24] main version */
#define STM32H735G_DK_BSP_VERSION_SUB1   (uint32_t)(0x02) /*!< [23:16] sub1 version */
#define STM32H735G_DK_BSP_VERSION_SUB2   (uint32_t)(0x03) /*!< [15:8]  sub2 version */
#define STM32H735G_DK_BSP_VERSION_RC     (uint32_t)(0x00) /*!< [7:0]  release candidate */
#define STM32H735G_DK_BSP_VERSION        ((STM32H735G_DK_BSP_VERSION_MAIN << 24)\
                                          |(STM32H735G_DK_BSP_VERSION_SUB1 << 16)\
                                          |(STM32H735G_DK_BSP_VERSION_SUB2 << 8 )\
                                          |(STM32H735G_DK_BSP_VERSION_RC))


// Blue LED
#define LED1_GPIO_PORT                   GPIOE
#define LED1_GPIO_CLK_ENABLE()           __HAL_RCC_GPIOE_CLK_ENABLE()
#define LED1_GPIO_CLK_DISABLE()          __HAL_RCC_GPIOE_CLK_DISABLE()
#define LED1_PIN                         GPIO_PIN_2

// Red LED
#define LED2_GPIO_PORT                   GPIOE
#define LED2_GPIO_CLK_ENABLE()           __HAL_RCC_GPIOE_CLK_ENABLE()
#define LED2_GPIO_CLK_DISABLE()          __HAL_RCC_GPIOE_CLK_DISABLE()
#define LED2_PIN                         GPIO_PIN_4

// Green LED
#define LED3_GPIO_PORT                   GPIOE
#define LED3_GPIO_CLK_ENABLE()           __HAL_RCC_GPIOE_CLK_ENABLE()
#define LED3_GPIO_CLK_DISABLE()          __HAL_RCC_GPIOE_CLK_DISABLE()
#define LED3_PIN                         GPIO_PIN_3

extern EXTI_HandleTypeDef hpb_exti[];

int32_t  BSP_GetVersion(void);
int32_t  BSP_LED_Init(Led_TypeDef Led);
int32_t  BSP_LED_Green_Init();
void 	 BSP_LEDs_Init(void);
void 	 BSP_MCO1_Init(void);
int32_t  BSP_LED_DeInit(Led_TypeDef Led);
int32_t  BSP_LED_On(Led_TypeDef Led);
int32_t  BSP_LED_Off(Led_TypeDef Led);
int32_t  BSP_LED_Toggle(Led_TypeDef Led);
int32_t  BSP_LED_GetState(Led_TypeDef Led);

#ifdef __cplusplus
}
#endif
#endif /* STM32H735G_DK_H */
