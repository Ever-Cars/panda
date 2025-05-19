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

static void richie_enable_can_transceivers(bool enabled) {
  uint8_t main_bus = (harness.status == HARNESS_STATUS_FLIPPED) ? 3U : 1U;
  for (uint8_t i = 1U; i <= NUM_CAN_BUSES; i++) {
    // Leave main CAN always on for CAN-based ignition detection
    if (i == main_bus)
      richie_enable_can_transceiver(i, true);
    else
      richie_enable_can_transceiver(i, enabled);
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

static uint32_t richie_read_voltage_mV(void){
  return adc_get_mV(2) * 11U; // TODO: is this correct?
}

static void richie_init(void) {
  common_init_gpio();

  //C10,C11 : OBD_SBU1_RELAY, OBD_SBU2_RELAY
  set_gpio_output_type(GPIOC, 10, OUTPUT_TYPE_OPEN_DRAIN);
  set_gpio_pullup(GPIOC, 10, PULL_NONE);
  set_gpio_mode(GPIOC, 10, MODE_OUTPUT);
  set_gpio_output(GPIOC, 10, 1);

  set_gpio_output_type(GPIOC, 11, OUTPUT_TYPE_OPEN_DRAIN);
  set_gpio_pullup(GPIOC, 11, PULL_NONE);
  set_gpio_mode(GPIOC, 11, MODE_OUTPUT);
  set_gpio_output(GPIOC, 11, 1);

  // G11,B3,D7,B4: transceiver enable
  set_gpio_pullup(GPIOG, 11, PULL_NONE);
  set_gpio_mode(GPIOG, 11, MODE_OUTPUT);

  set_gpio_pullup(GPIOB, 3, PULL_NONE);
  set_gpio_mode(GPIOB, 3, MODE_OUTPUT);

  set_gpio_pullup(GPIOD, 7, PULL_NONE);
  set_gpio_mode(GPIOD, 7, MODE_OUTPUT);

  set_gpio_pullup(GPIOB, 4, PULL_NONE);
  set_gpio_mode(GPIOB, 4, MODE_OUTPUT);

  //B1: 5VOUT_S
  set_gpio_pullup(GPIOB, 1, PULL_NONE);
  set_gpio_mode(GPIOB, 1, MODE_ANALOG);

  // B14: usb load switch, enabled by pull resistor on board, obsolete for red panda
  set_gpio_output_type(GPIOB, 14, OUTPUT_TYPE_OPEN_DRAIN);
  set_gpio_pullup(GPIOB, 14, PULL_UP);
  set_gpio_mode(GPIOB, 14, MODE_OUTPUT);
  set_gpio_output(GPIOB, 14, 1);

  // Initialize harness
  harness_init();


  // Enable CAN transceivers
  richie_enable_can_transceivers(true);

  // Disable LEDs
  richie_set_led(LED_RED, false);
#ifndef HW_RICHIE_REV1
  richie_set_led(LED_GREEN, false);
#endif
  richie_set_led(LED_BLUE, false);

  // Set normal CAN mode
  richie_set_can_mode(CAN_MODE_NORMAL);

  // SPI init
  gpio_spi_init();
}

static harness_configuration richie_harness_config = {
  .has_harness = true,
  .GPIO_SBU1 = GPIOC,
  .GPIO_SBU2 = GPIOA,
  .GPIO_relay_SBU1 = GPIOC,
  .GPIO_relay_SBU2 = GPIOC,
  .pin_SBU1 = 4,
  .pin_SBU2 = 1,
  .pin_relay_SBU1 = 10,
  .pin_relay_SBU2 = 11,
  .adc_channel_SBU1 = 4, //ADC12_INP4
  .adc_channel_SBU2 = 17 //ADC1_INP17
};

board board_richie = {
  .set_bootkick = unused_set_bootkick,
  .harness_config = &richie_harness_config,
  .has_obd = true,
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
  .enable_can_transceivers = richie_enable_can_transceivers,
  .set_led = richie_set_led,
  .set_can_mode = richie_set_can_mode,
  .check_ignition = richie_check_ignition,
  .read_voltage_mV = richie_read_voltage_mV,
  .read_current_mA = unused_read_current,
  .set_fan_enabled = unused_set_fan_enabled,
  .set_ir_power = unused_set_ir_power,
  .set_siren = unused_set_siren,
  .read_som_gpio = unused_read_som_gpio,
  .set_amp_enabled = unused_set_amp_enabled
};
