#include "usb_core.h"
#include "usb_hw.h"
#include "usb_debug.h"
#include "stm32h7xx.h"
#include "stm32h7xx_hal.h"

#include <string.h>

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

#define USB_VID 0x3801U
#define USB_PID 0xDDCCU

#define USB_REQ_GET_STATUS         0x00
#define USB_REQ_SET_ADDRESS        0x05
#define USB_REQ_GET_DESCRIPTOR     0x06
#define USB_REQ_SET_CONFIGURATION  0x09
#define USB_REQ_SET_INTERFACE      0x0B

#define USB_DESC_TYPE_DEVICE              0x01
#define USB_DESC_TYPE_CONFIGURATION       0x02
#define USB_DESC_TYPE_STRING              0x03
#define USB_DESC_TYPE_INTERFACE           0x04
#define USB_DESC_TYPE_DEVICE_QUALIFIER    0x06
#define USB_DESC_TYPE_BINARY_OBJECT_STORE 0x0f

#define STRING_OFFSET_LANGID          0x00
#define STRING_OFFSET_IMANUFACTURER   0x01
#define STRING_OFFSET_IPRODUCT        0x02
#define STRING_OFFSET_ISERIAL         0x03
#define STRING_OFFSET_ICONFIGURATION  0x04

#define MS_VENDOR_CODE     0x20
#define WEBUSB_VENDOR_CODE 0x30

#define WINUSB_REQ_GET_COMPATID_DESCRIPTOR 0x04
#define WINUSB_REQ_GET_EXT_PROPS_OS        0x05
#define WINUSB_REQ_GET_DESCRIPTOR          0x07

#define BINARY_OBJECT_STORE_DESCRIPTOR_LENGTH 0x05
#define BINARY_OBJECT_STORE_DESCRIPTOR        0x0F
#define WINUSB_PLATFORM_DESCRIPTOR_LENGTH     0x9E

#define TOUSBORDER(num) ((num) & 0xFFU), (((uint16_t)(num) >> 8) & 0xFFU)

#define STRING_DESCRIPTOR_HEADER(size) (((((size) * 2) + 2) & 0xFF) | 0x0300)

#define STS_DATA_UPDT  2
#define STS_SETUP_UPDT 6

#define DSCR_INTERFACE_LEN 9
#define DSCR_CONFIG_LEN    9
#define DSCR_DEVICE_LEN    18

typedef union {
  uint16_t w;
  struct BW {
    uint8_t msb;
    uint8_t lsb;
  } bw;
} uint16_t_uint8_t;

typedef union {
  uint32_t d8[2];
  struct {
    uint8_t           bmRequestType;
    uint8_t           bRequest;
    uint16_t_uint8_t  wValue;
    uint16_t_uint8_t  wIndex;
    uint16_t_uint8_t  wLength;
  } b;
} USB_Setup_TypeDef;

USB_OTG_GlobalTypeDef *USBx = USB_OTG_HS;

static uint8_t response[USBPACKET_MAX_SIZE];
static USB_Setup_TypeDef setup;
static uint8_t *ep0_txdata = NULL;
static uint16_t ep0_txlen = 0;

// --- Low-level packet I/O ---

static void *USB_ReadPacket(void *dest, uint16_t len) {
  uint32_t *dest_copy = (uint32_t *)dest;
  uint32_t count32b = ((uint32_t)len + 3U) / 4U;
  for (uint32_t i = 0; i < count32b; i++) {
    *dest_copy = USBx_DFIFO(0U);
    dest_copy++;
  }
  return ((void *)dest_copy);
}

static void USB_WritePacket(const void *src, uint16_t len, uint32_t ep) {
  uint32_t numpacket = ((uint32_t)len + (USBPACKET_MAX_SIZE - 1U)) / USBPACKET_MAX_SIZE;
  uint32_t count32b = ((uint32_t)len + 3U) / 4U;

  USBx_INEP(ep)->DIEPTSIZ = ((numpacket << 19) & USB_OTG_DIEPTSIZ_PKTCNT) |
                            (len               & USB_OTG_DIEPTSIZ_XFRSIZ);
  USBx_INEP(ep)->DIEPCTL |= (USB_OTG_DIEPCTL_CNAK | USB_OTG_DIEPCTL_EPENA);

  if (src != NULL) {
    const uint32_t *src_copy = (const uint32_t *)src;
    for (uint32_t i = 0; i < count32b; i++) {
      USBx_DFIFO(ep) = *src_copy;
      src_copy++;
    }
  }
}

static void USB_WritePacket_EP0(uint8_t *src, uint16_t len) {
  uint16_t wplen = MIN(len, 0x40);
  USB_WritePacket(src, wplen, 0);

  if (wplen < len) {
    ep0_txdata = &src[wplen];
    ep0_txlen = len - wplen;
    USBx_DEVICE->DIEPEMPMSK |= 1;
  } else {
    USBx_OUTEP(0U)->DOEPCTL |= USB_OTG_DOEPCTL_CNAK;
  }
}

// --- USB reset ---

static void usb_reset(void) {
  USBx_DEVICE->DAINT = 0xFFFFFFFFU;
  USBx_DEVICE->DAINTMSK = 0xFFFFFFFFU;
  USBx_DEVICE->DIEPMSK = 0xFFFFFFFFU;
  USBx_DEVICE->DOEPMSK = 0xFFFFFFFFU;

  USBx_INEP(0U)->DIEPINT = 0xFF;
  USBx_OUTEP(0U)->DOEPINT = 0xFF;

  USBx_DEVICE->DCFG &= ~USB_OTG_DCFG_DAD;

  // RX FIFO
  USBx->GRXFSIZ = 0x40;
  // EP0 TX FIFO
  USBx->DIEPTXF0_HNPTXFSIZ = (0x40UL << 16) | 0x40U;

  // flush TX fifo
  USBx->GRSTCTL = USB_OTG_GRSTCTL_TXFFLSH | USB_OTG_GRSTCTL_TXFNUM_4;
  while ((USBx->GRSTCTL & USB_OTG_GRSTCTL_TXFFLSH) == USB_OTG_GRSTCTL_TXFFLSH);
  // flush RX FIFO
  USBx->GRSTCTL = USB_OTG_GRSTCTL_RXFFLSH;
  while ((USBx->GRSTCTL & USB_OTG_GRSTCTL_RXFFLSH) == USB_OTG_GRSTCTL_RXFFLSH);

  // no global NAK
  USBx_DEVICE->DCTL |= USB_OTG_DCTL_CGINAK;

  // ready to receive setup packets
  USBx_OUTEP(0U)->DOEPTSIZ = USB_OTG_DOEPTSIZ_STUPCNT | (USB_OTG_DOEPTSIZ_PKTCNT & (1UL << 19)) | (3U << 3);
}

// --- Serial number helper ---

static char to_hex_char(uint8_t a) {
  if (a < 10U) return '0' + a;
  return 'a' + (a - 10U);
}

// --- USB setup handler ---

static void usb_setup(void) {
  static uint8_t device_desc[] = {
    DSCR_DEVICE_LEN, USB_DESC_TYPE_DEVICE,
    0x10, 0x02,       // bcdUSB 2.1
    0xFF, 0xFF, 0xFF, // Class, Subclass, Protocol (vendor-specific)
    0x40,             // Max Packet Size
    TOUSBORDER(USB_VID),
    TOUSBORDER(USB_PID),
    0x00, 0x00,       // bcdDevice
    0x01, 0x02,       // Manufacturer, Product
    0x03, 0x01        // Serial Number, Num Configurations
  };

  static uint8_t device_qualifier[] = {
    0x0a, USB_DESC_TYPE_DEVICE_QUALIFIER,
    0x10, 0x02,
    0xFF, 0xFF, 0xFF, 0x40,
    0x01, 0x00
  };

  // EP0-only configuration: 1 interface, 0 additional endpoints
  static uint8_t configuration_desc[] = {
    DSCR_CONFIG_LEN, USB_DESC_TYPE_CONFIGURATION,
    TOUSBORDER(0x0012U),   // Total Len (9 config + 9 interface = 18)
    0x01, 0x01,            // Num Interface, Config Value
    STRING_OFFSET_ICONFIGURATION,
    0xc0, 0x32,            // Attributes (self-powered), Max Power
    // interface 0
    DSCR_INTERFACE_LEN, USB_DESC_TYPE_INTERFACE,
    0x00, 0x00, 0x00,      // Index, Alt Index, Endpoint count (0)
    0xFF, 0xFF, 0xFF,      // Class, Subclass, Protocol (vendor)
    0x00,                  // Interface string
  };

  static uint16_t string_language_desc[] = {
    STRING_DESCRIPTOR_HEADER(1),
    0x0409
  };

  static uint16_t string_manufacturer_desc[] = {
    STRING_DESCRIPTOR_HEADER(8),
    'c', 'o', 'm', 'm', 'a', '.', 'a', 'i'
  };

  static uint16_t string_product_desc[] = {
    STRING_DESCRIPTOR_HEADER(6),
    'r', 'i', 'c', 'h', 'i', 'e'
  };

  static uint16_t string_configuration_desc[] = {
    STRING_DESCRIPTOR_HEADER(2),
    '0', '1'
  };

  // WCID descriptor for WinUSB auto-install
  static uint8_t string_238_desc[] = {
    0x12, USB_DESC_TYPE_STRING,
    'M',0, 'S',0, 'F',0, 'T',0, '1',0, '0',0, '0',0,
    MS_VENDOR_CODE, 0x00
  };

  static uint8_t winusb_ext_compatid_os_desc[] = {
    0x28, 0x00, 0x00, 0x00,
    0x00, 0x01,
    0x04, 0x00,
    0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00,
    0x00,
    'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  };

  static uint8_t winusb_ext_prop_os_desc[] = {
    0x8e, 0x00, 0x00, 0x00,
    0x00, 0x01,
    0x05, 0x00,
    0x01, 0x00,
    0x84, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00,
    0x28, 0x00,
    'D',0, 'e',0, 'v',0, 'i',0, 'c',0, 'e',0, 'I',0, 'n',0,
    't',0, 'e',0, 'r',0, 'f',0, 'a',0, 'c',0, 'e',0, 'G',0,
    'U',0, 'I',0, 'D',0, 0, 0,
    0x4e, 0x00, 0x00, 0x00,
    '{',0, 'c',0, 'c',0, 'e',0, '5',0, '2',0, '9',0, '1',0,
    'c',0, '-',0, 'a',0, '6',0, '9',0, 'f',0, '-',0, '4',0,
    '9',0, '9',0, '5',0, '-',0, 'a',0, '4',0, 'c',0, '2',0,
    '-',0, '2',0, 'a',0, 'e',0, '5',0, '7',0, 'a',0, '5',0,
    '1',0, 'a',0, 'd',0, 'e',0, '9',0, '}',0, 0, 0,
  };

  static uint8_t winusb_20_desc[WINUSB_PLATFORM_DESCRIPTOR_LENGTH] = {
    0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x06,
    WINUSB_PLATFORM_DESCRIPTOR_LENGTH, 0x00,
    0x14, 0x00, 0x03, 0x00,
    'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x00, 0x04, 0x00, 0x01, 0x00, 0x28, 0x00,
    'D', 0x00, 'e', 0x00, 'v', 0x00, 'i', 0x00, 'c', 0x00, 'e', 0x00, 'I', 0x00, 'n', 0x00,
    't', 0x00, 'e', 0x00, 'r', 0x00, 'f', 0x00, 'a', 0x00, 'c', 0x00, 'e', 0x00, 'G', 0x00,
    'U', 0x00, 'I', 0x00, 'D', 0x00, 0x00, 0x00,
    0x4E, 0x00,
    '{', 0x00, 'c', 0x00, 'c', 0x00, 'e', 0x00, '5', 0x00, '2', 0x00, '9', 0x00, '1', 0x00,
    'c', 0x00, '-', 0x00, 'a', 0x00, '6', 0x00, '9', 0x00, 'f', 0x00, '-', 0x00, '4', 0x00,
    '9', 0x00, '9', 0x00, '5', 0x00, '-', 0x00, 'a', 0x00, '4', 0x00, 'c', 0x00, '2', 0x00,
    '-', 0x00, '2', 0x00, 'a', 0x00, 'e', 0x00, '5', 0x00, '7', 0x00, 'a', 0x00, '5', 0x00,
    '1', 0x00, 'a', 0x00, 'd', 0x00, 'e', 0x00, '9', 0x00, '}', 0x00, 0x00, 0x00,
  };

  static uint8_t binary_object_store_desc[] = {
    BINARY_OBJECT_STORE_DESCRIPTOR_LENGTH,
    BINARY_OBJECT_STORE_DESCRIPTOR,
    0x39, 0x00,  // wTotalLength
    0x02,        // bNumDeviceCaps

    // WebUSB platform capability
    0x18, 0x10, 0x05, 0x00,
    0x38, 0xB6, 0x08, 0x34,
    0xA9, 0x09, 0xA0, 0x47,
    0x8B, 0xFD, 0xA0, 0x76,
    0x88, 0x15, 0xB6, 0x65,
    0x00, 0x01,
    WEBUSB_VENDOR_CODE,
    0x03,

    // WinUSB platform capability
    0x1C, 0x10, 0x05, 0x00,
    0xDF, 0x60, 0xDD, 0xD8,
    0x89, 0x45, 0xC7, 0x4C,
    0x9C, 0xD2, 0x65, 0x9D,
    0x9E, 0x64, 0x8A, 0x9F,
    0x00, 0x00, 0x03, 0x06,
    WINUSB_PLATFORM_DESCRIPTOR_LENGTH, 0x00,
    MS_VENDOR_CODE, 0x00
  };

  int resp_len;

  switch (setup.b.bRequest) {
    case USB_REQ_SET_CONFIGURATION:
      // EP0-only device, just ACK
      USB_WritePacket(0, 0, 0);
      USBx_OUTEP(0U)->DOEPCTL |= USB_OTG_DOEPCTL_CNAK;
      break;
    case USB_REQ_SET_ADDRESS:
      USBx_DEVICE->DCFG |= ((setup.b.wValue.w & 0x7fU) << 4);
      USB_WritePacket(0, 0, 0);
      USBx_OUTEP(0U)->DOEPCTL |= USB_OTG_DOEPCTL_CNAK;
      break;
    case USB_REQ_GET_DESCRIPTOR:
      switch (setup.b.wValue.bw.lsb) {
        case USB_DESC_TYPE_DEVICE:
          USB_WritePacket(device_desc, MIN(sizeof(device_desc), setup.b.wLength.w), 0);
          USBx_OUTEP(0U)->DOEPCTL |= USB_OTG_DOEPCTL_CNAK;
          break;
        case USB_DESC_TYPE_CONFIGURATION:
          USB_WritePacket(configuration_desc, MIN(sizeof(configuration_desc), setup.b.wLength.w), 0);
          USBx_OUTEP(0U)->DOEPCTL |= USB_OTG_DOEPCTL_CNAK;
          break;
        case USB_DESC_TYPE_DEVICE_QUALIFIER:
          USB_WritePacket(device_qualifier, MIN(sizeof(device_qualifier), setup.b.wLength.w), 0);
          USBx_OUTEP(0U)->DOEPCTL |= USB_OTG_DOEPCTL_CNAK;
          break;
        case USB_DESC_TYPE_STRING:
          switch (setup.b.wValue.bw.msb) {
            case STRING_OFFSET_LANGID:
              USB_WritePacket((uint8_t*)string_language_desc, MIN(sizeof(string_language_desc), setup.b.wLength.w), 0);
              break;
            case STRING_OFFSET_IMANUFACTURER:
              USB_WritePacket((uint8_t*)string_manufacturer_desc, MIN(sizeof(string_manufacturer_desc), setup.b.wLength.w), 0);
              break;
            case STRING_OFFSET_IPRODUCT:
              USB_WritePacket((uint8_t*)string_product_desc, MIN(sizeof(string_product_desc), setup.b.wLength.w), 0);
              break;
            case STRING_OFFSET_ISERIAL:
              response[0] = 0x02 + (12 * 4);
              response[1] = 0x03;
              for (int i = 0; i < 12; i++) {
                uint8_t cc = ((uint8_t *)UID_BASE)[i];
                response[2 + (i * 4)] = to_hex_char((cc >> 4) & 0xFU);
                response[2 + (i * 4) + 1] = '\0';
                response[2 + (i * 4) + 2] = to_hex_char((cc >> 0) & 0xFU);
                response[2 + (i * 4) + 3] = '\0';
              }
              USB_WritePacket(response, MIN(response[0], setup.b.wLength.w), 0);
              break;
            case STRING_OFFSET_ICONFIGURATION:
              USB_WritePacket((uint8_t*)string_configuration_desc, MIN(sizeof(string_configuration_desc), setup.b.wLength.w), 0);
              break;
            case 238:
              USB_WritePacket((uint8_t*)string_238_desc, MIN(sizeof(string_238_desc), setup.b.wLength.w), 0);
              break;
            default:
              USB_WritePacket(0, 0, 0);
              break;
          }
          USBx_OUTEP(0U)->DOEPCTL |= USB_OTG_DOEPCTL_CNAK;
          break;
        case USB_DESC_TYPE_BINARY_OBJECT_STORE:
          USB_WritePacket(binary_object_store_desc, MIN(sizeof(binary_object_store_desc), setup.b.wLength.w), 0);
          USBx_OUTEP(0U)->DOEPCTL |= USB_OTG_DOEPCTL_CNAK;
          break;
        default:
          USB_WritePacket(0, 0, 0);
          USBx_OUTEP(0U)->DOEPCTL |= USB_OTG_DOEPCTL_CNAK;
          break;
      }
      break;
    case USB_REQ_GET_STATUS:
      response[0] = 0;
      response[1] = 0;
      USB_WritePacket((void*)&response, 2, 0);
      USBx_OUTEP(0U)->DOEPCTL |= USB_OTG_DOEPCTL_CNAK;
      break;
    case USB_REQ_SET_INTERFACE:
      USB_WritePacket(0, 0, 0);
      USBx_OUTEP(0U)->DOEPCTL |= USB_OTG_DOEPCTL_CNAK;
      break;
    case WEBUSB_VENDOR_CODE:
      USB_WritePacket(0, 0, 0);
      USBx_OUTEP(0U)->DOEPCTL |= USB_OTG_DOEPCTL_CNAK;
      break;
    case MS_VENDOR_CODE:
      switch (setup.b.wIndex.w) {
        case WINUSB_REQ_GET_DESCRIPTOR:
          USB_WritePacket_EP0((uint8_t*)winusb_20_desc, MIN(sizeof(winusb_20_desc), setup.b.wLength.w));
          break;
        case WINUSB_REQ_GET_COMPATID_DESCRIPTOR:
          USB_WritePacket_EP0((uint8_t*)winusb_ext_compatid_os_desc, MIN(sizeof(winusb_ext_compatid_os_desc), setup.b.wLength.w));
          break;
        case WINUSB_REQ_GET_EXT_PROPS_OS:
          USB_WritePacket_EP0((uint8_t*)winusb_ext_prop_os_desc, MIN(sizeof(winusb_ext_prop_os_desc), setup.b.wLength.w));
          break;
        default:
          USB_WritePacket_EP0(0, 0);
          break;
      }
      break;
    default:
      // Vendor request: handle 0xe0 (debug serial read)
      resp_len = 0;
      if (setup.b.bRequest == 0xe0) {
        uint16_t req_length = MIN(setup.b.wLength.w, USBPACKET_MAX_SIZE);
        while (((uint16_t)resp_len < req_length) && get_char_debug((char*)&response[resp_len])) {
          resp_len++;
        }
      }
      USB_WritePacket(response, MIN(resp_len, setup.b.wLength.w), 0);
      USBx_OUTEP(0U)->DOEPCTL |= USB_OTG_DOEPCTL_CNAK;
      break;
  }
}

// --- USB IRQ handler ---

void usb_irqhandler(void) {
  unsigned int gintsts = USBx->GINTSTS;
  unsigned int gotgint = USBx->GOTGINT;
  unsigned int daint = USBx_DEVICE->DAINT;

  if ((gintsts & USB_OTG_GINTSTS_USBRST) != 0U) {
    usb_reset();
  }

  if ((gintsts & USB_OTG_GINTSTS_ENUMDNE) != 0U) {
    // enumeration done, nothing extra needed for EP0-only
  }

  // RX FIFO non-empty
  if ((gintsts & USB_OTG_GINTSTS_RXFLVL) != 0U) {
    volatile unsigned int rxst = USBx->GRXSTSP;
    int status = (rxst & USB_OTG_GRXSTSP_PKTSTS) >> 17;

    if (status == STS_SETUP_UPDT) {
      (void)USB_ReadPacket(&setup, 8);
    } else if (status == STS_DATA_UPDT) {
      int len = (rxst & USB_OTG_GRXSTSP_BCNT) >> 4;
      // drain any unexpected data from FIFO
      uint32_t tmp[16];
      (void)USB_ReadPacket(tmp, (uint16_t)len);
    }
  }

  if ((gintsts & USB_OTG_GINTSTS_BOUTNAKEFF) || (gintsts & USB_OTG_GINTSTS_GINAKEFF)) {
    USBx_DEVICE->DCTL |= USB_OTG_DCTL_CGONAK | USB_OTG_DCTL_CGINAK;
  }

  // OUT endpoint interrupt
  if ((gintsts & USB_OTG_GINTSTS_OEPINT) != 0U) {
    if ((USBx_OUTEP(0U)->DOEPINT & USB_OTG_DIEPINT_XFRC) != 0U) {
      USBx_OUTEP(0U)->DOEPTSIZ = USB_OTG_DOEPTSIZ_STUPCNT | (USB_OTG_DOEPTSIZ_PKTCNT & (1UL << 19)) | (1U << 3);
    }
    if ((USBx_OUTEP(0U)->DOEPINT & USB_OTG_DOEPINT_STUP) != 0U) {
      usb_setup();
    }
    USBx_OUTEP(0U)->DOEPINT = USBx_OUTEP(0U)->DOEPINT;
  }

  // IN endpoint interrupt (EP0 TX FIFO empty for multi-packet responses)
  if ((gintsts & USB_OTG_GINTSTS_IEPINT) != 0U) {
    if ((USBx_INEP(0U)->DIEPINT & USB_OTG_DIEPMSK_ITTXFEMSK) != 0U) {
      if ((ep0_txlen != 0U) && ((USBx_INEP(0U)->DTXFSTS & USB_OTG_DTXFSTS_INEPTFSAV) >= 0x40U)) {
        uint16_t len = MIN(ep0_txlen, 0x40);
        USB_WritePacket(ep0_txdata, len, 0);
        ep0_txdata = &ep0_txdata[len];
        ep0_txlen -= len;
        if (ep0_txlen == 0U) {
          ep0_txdata = NULL;
          USBx_DEVICE->DIEPEMPMSK &= ~1;
          USBx_OUTEP(0U)->DOEPCTL |= USB_OTG_DOEPCTL_CNAK;
        }
      }
    }
    USBx_INEP(0U)->DIEPINT = USBx_INEP(0U)->DIEPINT;
  }

  // Clear all handled interrupts
  USBx_DEVICE->DAINT = daint;
  USBx->GOTGINT = gotgint;
  USBx->GINTSTS = gintsts;
}

// --- USB init ---

void usb_init(void) {
  // Enable USB OTG HS clock
  __HAL_RCC_USB_OTG_HS_CLK_ENABLE();

  // Configure PA11 (DM) and PA12 (DP) as AF10 (OTG FS)
  __HAL_RCC_GPIOA_CLK_ENABLE();
  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = GPIO_PIN_11 | GPIO_PIN_12;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF10_OTG1_HS;
  HAL_GPIO_Init(GPIOA, &gpio);

  // Disable global interrupt
  USBx->GAHBCFG &= ~(USB_OTG_GAHBCFG_GINT);
  // Select FS Embedded PHY
  USBx->GUSBCFG |= USB_OTG_GUSBCFG_PHYSEL;
  // Force device mode
  USBx->GUSBCFG &= ~(USB_OTG_GUSBCFG_FHMOD | USB_OTG_GUSBCFG_FDMOD);
  USBx->GUSBCFG |= USB_OTG_GUSBCFG_FDMOD;

  HAL_Delay(25);

  // Wait for AHB master IDLE
  while ((USBx->GRSTCTL & USB_OTG_GRSTCTL_AHBIDL) == 0U);
  // Core Soft Reset
  USBx->GRSTCTL |= USB_OTG_GRSTCTL_CSRST;
  while ((USBx->GRSTCTL & USB_OTG_GRSTCTL_CSRST) == USB_OTG_GRSTCTL_CSRST);
  // Activate the USB Transceiver
  USBx->GCCFG |= USB_OTG_GCCFG_PWRDWN;

  for (uint8_t i = 0U; i < 15U; i++) {
    USBx->DIEPTXF[i] = 0U;
  }

  // VBUS Sensing setup
  USBx_DEVICE->DCTL |= USB_OTG_DCTL_SDIS;
  USBx->GCCFG &= ~(USB_OTG_GCCFG_VBDEN);
  USBx->GOTGCTL |= USB_OTG_GOTGCTL_BVALOEN;
  USBx->GOTGCTL |= USB_OTG_GOTGCTL_BVALOVAL;
  // Restart the Phy Clock
  USBx_PCGCCTL = 0U;
  // Device mode configuration
  USBx_DEVICE->DCFG |= DCFG_FRAME_INTERVAL_80;
  USBx_DEVICE->DCFG |= USB_OTG_SPEED_FULL | USB_OTG_DCFG_NZLSOHSK;

  // Flush FIFOs
  USBx->GRSTCTL = (USB_OTG_GRSTCTL_TXFFLSH | (0x10U << 6));
  while ((USBx->GRSTCTL & USB_OTG_GRSTCTL_TXFFLSH) == USB_OTG_GRSTCTL_TXFFLSH);
  USBx->GRSTCTL = USB_OTG_GRSTCTL_RXFFLSH;
  while ((USBx->GRSTCTL & USB_OTG_GRSTCTL_RXFFLSH) == USB_OTG_GRSTCTL_RXFFLSH);

  // Clear all pending Device Interrupts
  USBx_DEVICE->DIEPMSK = 0U;
  USBx_DEVICE->DOEPMSK = 0U;
  USBx_DEVICE->DAINTMSK = 0U;
  USBx_DEVICE->DIEPMSK &= ~(USB_OTG_DIEPMSK_TXFURM);

  // Disable all interrupts
  USBx->GINTMSK = 0U;
  USBx->GINTSTS = 0xBFFFFFFFU;
  // Enable device-mode interrupts
  USBx->GINTMSK = USB_OTG_GINTMSK_USBRST | USB_OTG_GINTMSK_ENUMDNEM |
                  USB_OTG_GINTMSK_RXFLVLM | USB_OTG_GINTMSK_GONAKEFFM |
                  USB_OTG_GINTMSK_GINAKEFFM | USB_OTG_GINTMSK_OEPINT |
                  USB_OTG_GINTMSK_IEPINT;

  // Set USB Turnaround time
  USBx->GUSBCFG |= ((USBD_FS_TRDT_VALUE << 10) & USB_OTG_GUSBCFG_TRDT);
  // Enable Global Int
  USBx->GAHBCFG |= USB_OTG_GAHBCFG_GINT;
  // Connect (soft disconnect disable)
  USBx_DEVICE->DCTL &= ~(USB_OTG_DCTL_SDIS);

  // Enable IRQ
  HAL_NVIC_SetPriority(OTG_HS_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(OTG_HS_IRQn);
}
