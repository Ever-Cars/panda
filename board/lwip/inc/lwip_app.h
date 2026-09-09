#pragma once

#include <stdbool.h>

// Configure clocks used only by Ethernet. Panda remains the system-clock owner.
void lwip_platform_clock_init(bool enable_phy_mco, bool enable_io_compensation);

// Initialize and verify the linker-placed Ethernet DMA memory.
bool lwip_dma_memory_init(void);

// Initialize lwIP, the RMII network interface, and the demo TCP service.
bool lwip_stack_init(void);

// Poll Ethernet RX and service lwIP software timers.
void lwip_poll(void);
