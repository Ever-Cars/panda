#pragma once

#include "board_declarations.h"

// ///////////////////////////// //
// Richie (Red Panda) + nRF9151  //
// ///////////////////////////// //

#define NUM_CAN_BUSES 2U

static void richie_enable_can_transceiver(uint8_t transceiver, bool enabled) {
  switch (transceiver) {
    case 1U:
      set_gpio_output(GPIOB, 7, !enabled); // Enable Pin
      set_gpio_output(GPIOB, 2, !enabled); // Standby Pin
      break;
    case 2U:
      set_gpio_output(GPIOB, 4, !enabled); // Enable Pin
      set_gpio_output(GPIOB, 3, !enabled); // Stanby Pin
      break;
    default:
      break;
  }
}

static void richie_set_led(uint8_t color, bool enabled) {
  switch (color) {
    case LED_RED:
      set_gpio_output(GPIOE, 4, !enabled);
      break;
     case LED_GREEN:
      set_gpio_output(GPIOE, 3, !enabled);
      break;
    case LED_BLUE:
      set_gpio_output(GPIOE, 2, !enabled);
      break;
    default:
      break;
  }
}

static void richie_set_can_mode(uint8_t mode) {
  richie_enable_can_transceiver(2U, false);
  switch (mode) {
    case CAN_MODE_NORMAL:
    case CAN_MODE_OBD_CAN2:
      // B5,B6: FDCAN2 mode
      set_gpio_pullup(GPIOB, 5, PULL_NONE);
      set_gpio_alternate(GPIOB, 5, GPIO_AF9_FDCAN2);

      set_gpio_pullup(GPIOB, 6, PULL_NONE);
      set_gpio_alternate(GPIOB, 6, GPIO_AF9_FDCAN2);
      richie_enable_can_transceiver(2U, true);
      break;
    default:
      break;
  }
}

static bool richie_check_ignition(void) {
  // ignition is checked through harness
  return harness_check_ignition();
}

static void richie_init(void) {
  common_init_gpio();

  // A6,B1: OBD_SBU1, OBD_SBU2
  set_gpio_pullup(GPIOA, 6, PULL_NONE);
  set_gpio_mode(GPIOA, 6, MODE_ANALOG);

  set_gpio_pullup(GPIOB, 1, PULL_NONE);
  set_gpio_mode(GPIOB, 1, MODE_ANALOG);

  // B2,B3: transceiver standby
  set_gpio_pullup(GPIOB, 2, PULL_NONE);
  set_gpio_mode(GPIOB, 2, MODE_OUTPUT);

  set_gpio_pullup(GPIOB, 3, PULL_NONE);
  set_gpio_mode(GPIOB, 3, MODE_OUTPUT);

  // B4,B7: transceiver enable
  set_gpio_pullup(GPIOB, 4, PULL_NONE);
  set_gpio_mode(GPIOB, 4, MODE_OUTPUT);

  set_gpio_pullup(GPIOB, 7, PULL_NONE);
  set_gpio_mode(GPIOB, 7, MODE_OUTPUT);

  // B13, A3, A5: nRF9151 gpios
  set_gpio_mode(GPIOB, 13, MODE_INPUT);
  set_gpio_mode(GPIOA, 3, MODE_INPUT);
  set_gpio_mode(GPIOA, 5, MODE_INPUT);

  // SPI init
  gpio_spi_init();
}

static harness_configuration richie_harness_config = {
  .has_harness = false,
  .GPIO_SBU1 = GPIOA,
  .GPIO_SBU2 = GPIOB,
  .pin_SBU1 = 6,
  .pin_SBU2 = 1,
  .adc_channel_SBU1 = 3, //ADC12_INP3
  .adc_channel_SBU2 = 5 //ADC1_INP5
};

board board_richie = {
  .set_bootkick = unused_set_bootkick,
  .harness_config = &richie_harness_config,
  .has_spi = true,
  .has_canfd = true,
  .fan_max_rpm = 0U,
  .fan_max_pwm = 100U,
  .avdd_mV = 3300U,
  .fan_stall_recovery = false,
  .fan_enable_cooldown_time = 0U,
  .init = richie_init,
  .init_bootloader = unused_init_bootloader,
  .enable_can_transceiver = richie_enable_can_transceiver,
  .set_led = richie_set_led,
  .set_can_mode = richie_set_can_mode,
  .check_ignition = richie_check_ignition,
  .read_voltage_mV = unused_read_voltage_mV,
  .read_current_mA = unused_read_current,
  .set_fan_enabled = unused_set_fan_enabled,
  .set_ir_power = unused_set_ir_power,
  .set_siren = unused_set_siren,
  .read_som_gpio = unused_read_som_gpio,
  .set_amp_enabled = unused_set_amp_enabled
};
