#pragma once

#include <stdbool.h>

//#define DEBUG
//#define DEBUG_UART
//#define DEBUG_USB
//#define DEBUG_SPI
//#define DEBUG_FAULTS
//#define DEBUG_COMMS
//#define DEBUG_FAN

#define CAN_INIT_TIMEOUT_MS 500U
#define USBPACKET_MAX_SIZE 0x40U
#define USB_BULK_MULTIPACKET_TRANSFER_MAX_PACKETS 32UL
#define USB_BULK_MULTIPACKET_TRANSFER_SIZE (USBPACKET_MAX_SIZE * USB_BULK_MULTIPACKET_TRANSFER_MAX_PACKETS)
// A full ISO-TP record is a 2-byte length prefix plus up to 4095 bytes of payload.
#define ISOTP_USB_BULK_TRANSFER_SIZE 0x1001U
#define ISOTP_USB_BULK_TRANSFER_MAX_PACKETS 65UL
#define MAX_CAN_MSGS_PER_USB_BULK_TRANSFER 51U
#define MAX_CAN_MSGS_PER_SPI_BULK_TRANSFER 170U

// USB definitions
#define USB_VID 0x3801U

#ifdef PANDA_JUNGLE
  #ifdef BOOTSTUB
    #define USB_PID 0xDDEFU
  #else
    #define USB_PID 0xDDCFU
  #endif
#else
  #ifdef BOOTSTUB
    #define USB_PID 0xDDEEU
  #else
    #define USB_PID 0xDDCCU
  #endif
#endif

// platform includes
#ifdef STM32H7
  #include "board/stm32h7/stm32h7_config.h"
#else
  // TODO: uncomment this, cppcheck complains
  // building for tests
  //#include "fake_stm.h"
#endif
