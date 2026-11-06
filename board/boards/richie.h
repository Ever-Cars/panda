#pragma once

#include "board_declarations.h"

// ///////////////////////////// //
// Richie (Red Panda) + nRF9151  //
// ///////////////////////////// //

static void richie_enable_can_transceiver(uint8_t transceiver, bool enabled) {
  switch (transceiver) {
    case 1U:
    case 2U:
      set_gpio_output(GPIOB, 7, !enabled); // CAN1 Enable Pin
      set_gpio_output(GPIOB, 5, !enabled); // CAN1 Standby Pin
      break;
    default:
      break;
  }
}

static void richie_set_doip_enabled(bool enabled) {
  set_gpio_output(GPIOB, 4, enabled);
}

static void richie_set_can_mode(uint8_t mode) {
  // Only CAN 1 (FDCAN1 on B8/B9). CAN_SEL on PB6 routes to normal/OBD.
  set_gpio_output(GPIOB, 6, (mode == CAN_MODE_OBD_CAN2) ? 1 : 0);
  richie_enable_can_transceiver(1U, true);
}

static uint32_t richie_read_voltage_mV(void) {
  // PA6 = ADC12_INP3, 12V sense, 220k/(220k+1M) = 0.1803... = 11/61
  return (adc_get_mV(&(const adc_signal_t) ADC_CHANNEL_DEFAULT(ADC1, 3)) * 61U) / 11U;
}

#ifdef HAS_DOIP
static void richie_init_eth_pin(GPIO_TypeDef *GPIO, unsigned int pin) {
  set_gpio_pullup(GPIO, pin, PULL_NONE);
  set_gpio_alternate(GPIO, pin, GPIO_AF11_ETH);
  register_set_bits(&(GPIO->OSPEEDR), 3UL << (pin * 2U));  // very high speed
}

static void richie_init_ethernet(void) {
  // LAN8742A RMII: REF_CLK A1, MDIO A2, CRS_DV A7, MDC C1, RXD0 C4, RXD1 C5,
  // RX_ER B10, TX_EN B11, TXD0 G13, TXD1 G14
  richie_init_eth_pin(GPIOA, 1);
  richie_init_eth_pin(GPIOA, 2);
  richie_init_eth_pin(GPIOA, 7);
  richie_init_eth_pin(GPIOC, 1);
  richie_init_eth_pin(GPIOC, 4);
  richie_init_eth_pin(GPIOC, 5);
  richie_init_eth_pin(GPIOB, 10);
  richie_init_eth_pin(GPIOB, 11);
  richie_init_eth_pin(GPIOG, 13);
  richie_init_eth_pin(GPIOG, 14);

  // E5: PHY power enable, E0: PHY reset (held low until eth_init releases it)
  set_gpio_pullup(GPIOE, 5, PULL_NONE);
  set_gpio_output(GPIOE, 5, true);
  set_gpio_pullup(GPIOE, 0, PULL_NONE);
  set_gpio_output(GPIOE, 0, false);

  // A8: MCO1 = HSE (25 MHz) clocks the PHY crystal input
  set_gpio_pullup(GPIOA, 8, PULL_NONE);
  set_gpio_alternate(GPIOA, 8, GPIO_AF0_MCO);
  register_set_bits(&(GPIOA->OSPEEDR), 3UL << (8U * 2U));
  register_set(&(RCC->CFGR), RCC_CFGR_MCO1_1, RCC_CFGR_MCO1 | RCC_CFGR_MCO1PRE);
}
#endif

static void richie_init(void) {
  common_init_gpio();

#ifdef HAS_DOIP
  richie_init_ethernet();
#endif

  // B5: CAN1 transceiver standby
  set_gpio_pullup(GPIOB, 5, PULL_NONE);
  set_gpio_mode(GPIOB, 5, MODE_OUTPUT);

  // B7: CAN1 transceiver enable
  set_gpio_pullup(GPIOB, 7, PULL_NONE);
  set_gpio_mode(GPIOB, 7, MODE_OUTPUT);

  // PB4: keep the DOIP_EN PFET disabled until explicitly enabled
  set_gpio_pullup(GPIOB, 4, PULL_NONE);
  richie_set_doip_enabled(false);

  // PB6: CAN select
  set_gpio_pullup(GPIOB, 6, PULL_NONE);
  set_gpio_mode(GPIOB, 6, MODE_OUTPUT);

  // PA6: 12V sense (ADC)
  set_gpio_pullup(GPIOA, 6, PULL_NONE);
  set_gpio_mode(GPIOA, 6, MODE_ANALOG);

  // A3, A5, B13: nRF9151 gpios
  // A3 will be used to synchronize SPI communication
  // A5 will be used to put panda in bootloader mode when held high on reset
  set_gpio_pullup(GPIOA, 3, PULL_NONE);
  set_gpio_mode(GPIOA, 3, MODE_OUTPUT);
  set_gpio_pullup(GPIOA, 5, PULL_DOWN);
  set_gpio_mode(GPIOA, 5, MODE_INPUT);
  set_gpio_mode(GPIOB, 13, MODE_INPUT);

  // SPI init
  gpio_spi_init();
}

board board_richie = {
  .set_bootkick = unused_set_bootkick,
  .harness_config = NULL,
  .has_spi = true,
  .has_fan = false,
  .avdd_mV = 3300U,
  .fan_enable_cooldown_time = 0U,
  .init = richie_init,
  .init_bootloader = unused_init_bootloader,
  .enable_can_transceiver = richie_enable_can_transceiver,
  .led_GPIO = {GPIOE, GPIOE, GPIOE},
  .led_pin = {4, 3, 2},
  .set_can_mode = richie_set_can_mode,
  .read_voltage_mV = richie_read_voltage_mV,
  .read_current_mA = unused_read_current,
  .set_fan_enabled = unused_set_fan_enabled,
  .set_ir_power = unused_set_ir_power,
  .set_siren = unused_set_siren,
  .read_som_gpio = unused_read_som_gpio,
  .set_amp_enabled = unused_set_amp_enabled
};
