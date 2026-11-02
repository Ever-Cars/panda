#pragma once

#include <stdint.h>
#include <stdbool.h>

#define CORE_CLOCK_HZ 240000000U // in Hz

// Configure clocks used only by Ethernet. Panda remains the system-clock owner.
void lwip_clock_init(void);

// Initialize lwIP, the RMII network interface, and the demo TCP service.
bool lwip_stack_init(void);

// Poll Ethernet RX and service lwIP software timers.
void lwip_poll(void);
