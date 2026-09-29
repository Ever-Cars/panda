#pragma once

#include <stdint.h>
#include <stdbool.h>

#define CORE_CLOCK_HZ 240000000U // in Hz

/*Static IP ADDRESS: IP_ADDR0.IP_ADDR1.IP_ADDR2.IP_ADDR3 */
#define IP_ADDR0   ((uint8_t) 192U)
#define IP_ADDR1   ((uint8_t) 168U)
#define IP_ADDR2   ((uint8_t) 0U)
#define IP_ADDR3   ((uint8_t) 10U)

/*NETMASK*/
#define NETMASK_ADDR0   ((uint8_t) 255U)
#define NETMASK_ADDR1   ((uint8_t) 255U)
#define NETMASK_ADDR2   ((uint8_t) 255U)
#define NETMASK_ADDR3   ((uint8_t) 0U)

/*Gateway Address*/
#define GW_ADDR0   ((uint8_t) 192U)
#define GW_ADDR1   ((uint8_t) 168U)
#define GW_ADDR2   ((uint8_t) 0U)
#define GW_ADDR3   ((uint8_t) 1U)

// Configure clocks used only by Ethernet. Panda remains the system-clock owner.
void lwip_clock_init(void);

// Initialize lwIP, the RMII network interface, and the demo TCP service.
bool lwip_stack_init(void);

// Poll Ethernet RX and service lwIP software timers.
void lwip_poll(void);
