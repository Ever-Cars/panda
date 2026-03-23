#pragma once

#define ISOTP_MSG_MAX_LEN 4095U
#define ISOTP_N_WFTMAX 10U
#define ISOTP_DEFAULT_RX_STMIN 0U
#define ISOTP_DEFAULT_TX_MESSAGE_TIMEOUT_MS 1000U
#define ISOTP_DEFAULT_TX_TRANSFER_TIMEOUT_MS 10000U
#define ISOTP_CAN_FRAME_LEN 8U
#define ISOTP_CAN_PAD_BYTE 0xCCU

#define ISOTP_TX_QUEUE_STORAGE_BYTES 16384U
#define ISOTP_RX_QUEUE_STORAGE_BYTES 16384U
#define ISOTP_BULK_MAX_RECORD_SIZE (ISOTP_MSG_MAX_LEN + 2U)

// Host-facing ISO-TP bulk framing is [len_lo, len_hi, payload], so one fully
// serialized max-length record is ISOTP_BULK_MAX_RECORD_SIZE bytes.
//
// isotp_write_staging_buffer must be larger than one record because
// comms_isotp_write()
// appends the entire incoming USB/SPI chunk before draining complete records
// into the fixed-slot queue. In the worst case the staging buffer already holds
// ISOTP_BULK_MAX_RECORD_SIZE - 1 bytes of an unfinished max-length record and
// then receives one full max-length record, so it needs
// (2 * ISOTP_BULK_MAX_RECORD_SIZE) - 1 bytes total.
//
// Do not shrink this without reworking comms_isotp_write(),
// isotp_drain_write_staging_buffer(), and the transport backpressure checks
// that decide when USB/SPI may hand us another chunk.
#define ISOTP_WRITE_STAGING_BUFFER_SIZE ((2U * ISOTP_BULK_MAX_RECORD_SIZE) - 1U)

// Each queue slot stores one complete ISO-TP payload. This trades some packing
// efficiency for much simpler queue logic than the old byte-oriented ring.
#define ISOTP_TX_QUEUE_DEPTH (ISOTP_TX_QUEUE_STORAGE_BYTES / ISOTP_BULK_MAX_RECORD_SIZE)
#define ISOTP_RX_QUEUE_DEPTH (ISOTP_RX_QUEUE_STORAGE_BYTES / ISOTP_BULK_MAX_RECORD_SIZE)

enum isotp_tx_state {
  ISOTP_TX_IDLE = 0,
  ISOTP_TX_WAIT_FC,
  ISOTP_TX_WAIT_STMIN,
};

enum isotp_rx_state {
  ISOTP_RX_IDLE = 0,
  ISOTP_RX_WAIT_CF,
};

enum isotp_frame_type {
  ISOTP_FRAME_INVALID = 0,
  ISOTP_FRAME_SINGLE,
  ISOTP_FRAME_FIRST,
  ISOTP_FRAME_CONSECUTIVE,
  ISOTP_FRAME_FLOW_CONTROL,
};

typedef struct {
  // Payload length stored in this slot.
  uint16_t len;
  // Storage for one complete ISO-TP payload.
  uint8_t data[ISOTP_MSG_MAX_LEN];
} isotp_queue_entry_t;

typedef struct {
  // Index of the next slot producers will fill.
  volatile uint32_t w_ptr;
  // Index of the oldest unread slot.
  volatile uint32_t r_ptr;
  // Number of complete payloads currently queued.
  volatile uint32_t count;
  // Total number of slots in elems.
  uint32_t fifo_size;
  // Backing storage for the circular payload queue.
  isotp_queue_entry_t *elems;
} isotp_msg_queue_t;

typedef struct {
  // Number of bytes currently staged in data for the USB/SPI reader.
  // This buffer can hold a partial dequeue so comms_isotp_read() can satisfy
  // arbitrarily sized host reads without losing record boundaries internally.
  // The buffer stores the serialized host-facing form [len_lo, len_hi, payload].
  uint16_t len;
  uint8_t data[ISOTP_BULK_MAX_RECORD_SIZE];
} isotp_read_staging_buffer_t;

typedef struct {
  // Number of bytes accumulated from USB/SPI writes that have not yet been
  // turned into complete queue records.
  uint32_t len;
  uint8_t data[ISOTP_WRITE_STAGING_BUFFER_SIZE];
} isotp_write_staging_buffer_t;

typedef struct {
  enum isotp_frame_type type;
  const uint8_t *data;
  uint8_t len;
  uint16_t total_len;
  uint8_t flow_status;
  uint8_t block_size;
  uint8_t stmin_raw;
  uint8_t sn;
} isotp_parsed_frame_t;

typedef struct {
  bool configured;
  bool bus_set;
  bool tx_id_set;
  bool rx_id_set;
  uint8_t bus;

  uint32_t tx_id;
  uint32_t rx_id;
  bool tx_id_extended;
  bool rx_id_extended;

  bool tx_ext_addr_enabled;
  uint8_t tx_ext_addr;
  bool rx_ext_addr_enabled;
  uint8_t rx_ext_addr;
  uint16_t tx_message_timeout_ms;
  uint16_t tx_transfer_timeout_ms;

  struct {
    uint8_t buf[ISOTP_MSG_MAX_LEN];
    uint16_t len;
    uint16_t offset;
    uint8_t next_sn;
    uint8_t block_size;
    uint8_t block_cf_sent;
    uint8_t wait_fc_count;
    uint32_t stmin_us;
    uint32_t deadline_us;
    uint32_t transfer_deadline_us;
    uint32_t next_cf_us;
    enum isotp_tx_state state;
  } tx;

  struct {
    uint8_t buf[ISOTP_MSG_MAX_LEN];
    uint16_t expected_len;
    uint16_t offset;
    uint8_t next_sn;
    enum isotp_rx_state state;
  } rx;
} isotp_session_t;

#ifdef STM32H7
  #define ISOTP_QUEUE_STORAGE_ATTR __attribute__((section(".axisram")))
  #define ISOTP_STAGING_BUFFER_ATTR __attribute__((section(".sram12")))
#else
  #define ISOTP_QUEUE_STORAGE_ATTR
  #define ISOTP_STAGING_BUFFER_ATTR
#endif

ISOTP_QUEUE_STORAGE_ATTR static isotp_queue_entry_t elems_isotp_tx_q[ISOTP_TX_QUEUE_DEPTH];
static isotp_msg_queue_t isotp_tx_q = {
  .w_ptr = 0U,
  .r_ptr = 0U,
  .count = 0U,
  .fifo_size = ISOTP_TX_QUEUE_DEPTH,
  .elems = elems_isotp_tx_q,
};
ISOTP_QUEUE_STORAGE_ATTR static isotp_queue_entry_t elems_isotp_rx_q[ISOTP_RX_QUEUE_DEPTH];
static isotp_msg_queue_t isotp_rx_q = {
  .w_ptr = 0U,
  .r_ptr = 0U,
  .count = 0U,
  .fifo_size = ISOTP_RX_QUEUE_DEPTH,
  .elems = elems_isotp_rx_q,
};
ISOTP_STAGING_BUFFER_ATTR static isotp_read_staging_buffer_t isotp_read_staging_buffer = {0};
ISOTP_STAGING_BUFFER_ATTR static isotp_write_staging_buffer_t isotp_write_staging_buffer = {0};

static isotp_session_t isotp_session = {
  .configured = false,
  .bus_set = false,
  .tx_id_set = false,
  .rx_id_set = false,
  .tx_message_timeout_ms = ISOTP_DEFAULT_TX_MESSAGE_TIMEOUT_MS,
  .tx_transfer_timeout_ms = ISOTP_DEFAULT_TX_TRANSFER_TIMEOUT_MS,
  .tx = {
    .state = ISOTP_TX_IDLE,
  },
  .rx = {
    .state = ISOTP_RX_IDLE,
  },
};

static void isotp_abort_tx(void);
static void isotp_abort_rx(void);
static void isotp_kick(uint32_t now_us);
int comms_isotp_read(uint8_t *data, uint32_t max_len);
void comms_isotp_write(const uint8_t *data, uint32_t len);
void comms_isotp_reset(void);
bool comms_isotp_can_write_usb(void);
bool comms_isotp_can_write_spi(uint32_t len);

static bool isotp_payload_len_valid(uint16_t len) {
  return (len > 0U) && (len <= ISOTP_MSG_MAX_LEN);
}

// Fixed-slot queue contract:
//  - Each slot holds exactly one ISO-TP payload plus its payload length.
//  - Queue storage is internal only; USB/SPI framing still uses
//    [len_lo, len_hi, payload...] in the staging buffers.
//  - Producers publish whole payloads, consumers pop whole payloads.
//  - count tracks fullness explicitly, so all allocated slots are usable.
//  - Any impossible stored length is treated as corruption and clears the
//    queue instead of attempting partial recovery.
static void isotp_msg_queue_clear(isotp_msg_queue_t *q) {
  ENTER_CRITICAL();
  q->w_ptr = 0U;
  q->r_ptr = 0U;
  q->count = 0U;
  EXIT_CRITICAL();
}

static uint32_t isotp_queue_next_ptr(uint32_t ptr, uint32_t fifo_size) {
  return ((ptr + 1U) == fifo_size) ? 0U : (ptr + 1U);
}

// Queue one complete ISO-TP payload. This is used both by received CAN traffic
// entering the RX queue and by host writes that have already been reassembled
// into a complete bulk-transfer record in isotp_write_staging_buffer.
static bool isotp_msg_queue_push(isotp_msg_queue_t *q, const uint8_t *payload, uint16_t len) {
  bool ret = false;

  if (isotp_payload_len_valid(len)) {
    ENTER_CRITICAL();
    if (q->count < q->fifo_size) {
      q->elems[q->w_ptr].len = len;
      (void)memcpy(q->elems[q->w_ptr].data, payload, len);
      q->w_ptr = isotp_queue_next_ptr(q->w_ptr, q->fifo_size);
      q->count += 1U;
      ret = true;
    }
    EXIT_CRITICAL();
  }

  return ret;
}

// Pop one complete ISO-TP payload. The caller receives only the payload bytes;
// USB/SPI framing is re-added later if the data is headed back to the host.
static bool isotp_msg_queue_pop(isotp_msg_queue_t *q, uint8_t *payload, uint16_t *payload_len) {
  bool ret = false;

  ENTER_CRITICAL();
  if (q->count > 0U) {
    uint16_t len = q->elems[q->r_ptr].len;
    if (!isotp_payload_len_valid(len)) {
      q->w_ptr = 0U;
      q->r_ptr = 0U;
      q->count = 0U;
    } else {
      *payload_len = len;
      (void)memcpy(payload, q->elems[q->r_ptr].data, len);
      q->r_ptr = isotp_queue_next_ptr(q->r_ptr, q->fifo_size);
      q->count -= 1U;
      ret = true;
    }
  }
  EXIT_CRITICAL();

  return ret;
}

static void refresh_isotp_tx_slots_available(void) {
  if (comms_isotp_can_write_usb()) {
    isotp_tx_comms_resume_usb();
  }
  if (comms_isotp_can_write_spi(1U)) {
    isotp_tx_comms_resume_spi();
  }
}

static void isotp_shift_write_staging_buffer_left(uint32_t shift) {
  if ((shift != 0U) && (shift <= isotp_write_staging_buffer.len)) {
    // The host write staging buffer is a linear staging area, not a ring. Once one
    // complete record has been pushed into isotp_tx_q, compact any trailing
    // partial record to the front so future writes can continue appending.
    uint32_t remaining = isotp_write_staging_buffer.len - shift;
    for (uint32_t i = 0U; i < remaining; i++) {
      isotp_write_staging_buffer.data[i] = isotp_write_staging_buffer.data[i + shift];
    }
    isotp_write_staging_buffer.len = remaining;
  }
}

static void isotp_drain_write_staging_buffer(void) {
  bool done = false;

  while ((isotp_write_staging_buffer.len >= 2U) && !done) {
    // The staging buffer may contain zero, one, or many host-facing records.
    // Only move complete payloads into the fixed-slot TX queue; leave any tail
    // fragment in place until more USB/SPI data arrives.
    uint16_t payload_len = isotp_write_staging_buffer.data[0] | ((uint16_t)isotp_write_staging_buffer.data[1] << 8);
    uint32_t record_len = (uint32_t)payload_len + 2U;

    if (!isotp_payload_len_valid(payload_len)) {
      // Drop the entire staging buffer on malformed framing. There is no safe
      // way to find the next real record boundary once the prefix is bogus.
      isotp_write_staging_buffer.len = 0U;
      done = true;
    } else if (isotp_write_staging_buffer.len < record_len) {
      // Stop on a short record and wait for the next host write to finish it.
      done = true;
    } else if (!isotp_msg_queue_push(&isotp_tx_q, &isotp_write_staging_buffer.data[2U], payload_len)) {
      // The queue is full. Leave the bytes staged so they can be retried later.
      done = true;
    } else {
      isotp_shift_write_staging_buffer_left(record_len);
    }
  }

  refresh_isotp_tx_slots_available();
}

int comms_isotp_read(uint8_t *data, uint32_t max_len) {
  uint32_t pos = 0U;

  while (pos < max_len) {
    if (isotp_read_staging_buffer.len == 0U) {
      uint16_t payload_len;

      // Refill the linear read staging buffer with one host-facing serialized
      // record. The queue stores payloads only, so this step re-adds the
      // length prefix expected on the USB/SPI wire.
      if (!isotp_msg_queue_pop(&isotp_rx_q, &isotp_read_staging_buffer.data[2U], &payload_len)) {
        break;
      }
      isotp_read_staging_buffer.data[0] = payload_len & 0xFFU;
      isotp_read_staging_buffer.data[1] = (payload_len >> 8) & 0xFFU;
      isotp_read_staging_buffer.len = payload_len + 2U;
    }

    uint32_t copy_len = MIN(max_len - pos, (uint32_t)isotp_read_staging_buffer.len);
    (void)memcpy(&data[pos], isotp_read_staging_buffer.data, copy_len);
    pos += copy_len;

    uint32_t remaining = (uint32_t)isotp_read_staging_buffer.len - copy_len;
    for (uint32_t i = 0U; i < remaining; i++) {
      // Compact the unread tail to the front so the next comms_isotp_read()
      // call resumes where this one stopped.
      isotp_read_staging_buffer.data[i] = isotp_read_staging_buffer.data[i + copy_len];
    }
    isotp_read_staging_buffer.len = remaining;
  }

  return pos;
}

void comms_isotp_write(const uint8_t *data, uint32_t len) {
  uint32_t free_bytes = (uint32_t)(sizeof(isotp_write_staging_buffer.data) - isotp_write_staging_buffer.len);

  if ((len > 0U) && (len <= free_bytes)) {
    // Append raw bulk-transfer bytes into the staging buffer first. Records may
    // be split across USB/SPI transactions, so they cannot go straight into the
    // TX ring until at least the two-byte length prefix says a full record is
    // available.
    (void)memcpy(&isotp_write_staging_buffer.data[isotp_write_staging_buffer.len], data, len);
    isotp_write_staging_buffer.len += len;
    isotp_drain_write_staging_buffer();
  }
}

bool comms_isotp_can_write_usb(void) {
  return ((sizeof(isotp_write_staging_buffer.data) - isotp_write_staging_buffer.len) >= ISOTP_USB_BULK_TRANSFER_SIZE);
}

bool comms_isotp_can_write_spi(uint32_t len) {
  return ((sizeof(isotp_write_staging_buffer.data) - isotp_write_staging_buffer.len) >= len);
}

static void isotp_reset_tx_state(void) {
  isotp_session.tx.len = 0U;
  isotp_session.tx.offset = 0U;
  isotp_session.tx.next_sn = 0U;
  isotp_session.tx.block_size = 0U;
  isotp_session.tx.block_cf_sent = 0U;
  isotp_session.tx.wait_fc_count = 0U;
  isotp_session.tx.stmin_us = 0U;
  isotp_session.tx.deadline_us = 0U;
  isotp_session.tx.transfer_deadline_us = 0U;
  isotp_session.tx.next_cf_us = 0U;
  isotp_session.tx.state = ISOTP_TX_IDLE;
}

static void isotp_reset_rx_state(void) {
  isotp_session.rx.expected_len = 0U;
  isotp_session.rx.offset = 0U;
  isotp_session.rx.next_sn = 0U;
  isotp_session.rx.state = ISOTP_RX_IDLE;
}

static void isotp_abort_tx(void) {
  isotp_reset_tx_state();
}

static void isotp_abort_rx(void) {
  isotp_reset_rx_state();
}

void comms_isotp_reset(void) {
  isotp_write_staging_buffer.len = 0U;
  isotp_read_staging_buffer.len = 0U;
  isotp_msg_queue_clear(&isotp_tx_q);
  isotp_msg_queue_clear(&isotp_rx_q);
  isotp_abort_tx();
  isotp_abort_rx();
  refresh_isotp_tx_slots_available();
}

static inline void isotp_update_configured(void) {
  isotp_session.configured = isotp_session.bus_set && isotp_session.tx_id_set && isotp_session.rx_id_set;
}

static uint32_t isotp_deadline_from_ms(uint32_t now_us, uint16_t timeout_ms) {
  return now_us + ((uint32_t)timeout_ms * 1000U);
}

static bool isotp_time_reached(uint32_t now_us, uint32_t target_us) {
  return (get_ts_elapsed(now_us, target_us) < 0x80000000U);
}

static uint8_t isotp_single_frame_capacity(bool ext_addr_enabled) {
  return ext_addr_enabled ? 6U : 7U;
}

static uint8_t isotp_first_frame_capacity(bool ext_addr_enabled) {
  return ext_addr_enabled ? 5U : 6U;
}

static uint8_t isotp_consecutive_frame_capacity(bool ext_addr_enabled) {
  return ext_addr_enabled ? 6U : 7U;
}

static inline bool isotp_parse_packed_arb_id(uint32_t packed_id, uint32_t *arb_id, bool *extended) {
  bool ret = ((packed_id & 0x60000000U) == 0U);
  bool ext = (packed_id & 0x80000000U) != 0U;
  uint32_t raw_id = packed_id & 0x1FFFFFFFU;

  if ((!ext) && ((raw_id & ~0x7FFU) != 0U)) {
    ret = false;
  }

  if (ret) {
    *arb_id = raw_id;
    *extended = ext;
  }

  return ret;
}

static inline void isotp_set_bus(uint8_t bus) {
  isotp_session.bus = bus;
  isotp_session.bus_set = true;
  isotp_update_configured();
  comms_isotp_reset();
}

static inline bool isotp_set_tx_arb_id(uint32_t packed_id) {
  uint32_t arb_id;
  bool extended;
  bool ret = isotp_parse_packed_arb_id(packed_id, &arb_id, &extended);

  if (ret) {
    isotp_session.tx_id = arb_id;
    isotp_session.tx_id_extended = extended;
    isotp_session.tx_id_set = true;
    isotp_update_configured();
    comms_isotp_reset();
  }

  return ret;
}

static inline bool isotp_set_rx_arb_id(uint32_t packed_id) {
  uint32_t arb_id;
  bool extended;
  bool ret = isotp_parse_packed_arb_id(packed_id, &arb_id, &extended);

  if (ret) {
    isotp_session.rx_id = arb_id;
    isotp_session.rx_id_extended = extended;
    isotp_session.rx_id_set = true;
    isotp_update_configured();
    comms_isotp_reset();
  }

  return ret;
}

static inline void isotp_set_ext_addr(uint16_t tx_cfg, uint16_t rx_cfg) {
  isotp_session.tx_ext_addr = tx_cfg & 0xFFU;
  isotp_session.tx_ext_addr_enabled = ((tx_cfg >> 8U) & 0x1U) != 0U;
  isotp_session.rx_ext_addr = rx_cfg & 0xFFU;
  isotp_session.rx_ext_addr_enabled = ((rx_cfg >> 8U) & 0x1U) != 0U;
  comms_isotp_reset();
}

static inline void isotp_set_tx_timeouts(uint16_t message_timeout_ms, uint16_t transfer_timeout_ms) {
  isotp_session.tx_message_timeout_ms = message_timeout_ms;
  isotp_session.tx_transfer_timeout_ms = transfer_timeout_ms;
}

static bool isotp_send_can_frame(const uint8_t *payload, uint8_t payload_len) {
  CANPacket_t pkt = {0};
  uint8_t data_offset = isotp_session.tx_ext_addr_enabled ? 1U : 0U;
  uint8_t frame_len = payload_len + data_offset;
  bool send_ok = false;

  if (frame_len <= ISOTP_CAN_FRAME_LEN) {
    pkt.fd = 0U;
    pkt.returned = 0U;
    pkt.rejected = 0U;
    pkt.extended = isotp_session.tx_id_extended;
    pkt.addr = isotp_session.tx_id;
    pkt.bus = isotp_session.bus;
    pkt.data_len_code = ISOTP_CAN_FRAME_LEN;
    (void)memset(pkt.data, ISOTP_CAN_PAD_BYTE, ISOTP_CAN_FRAME_LEN);

    if (isotp_session.tx_ext_addr_enabled) {
      pkt.data[0] = isotp_session.tx_ext_addr;
    }

    (void)memcpy(&pkt.data[data_offset], payload, payload_len);
    can_set_checksum(&pkt);
    can_send(&pkt, isotp_session.bus, false);

    send_ok = (pkt.rejected == 0U);
  }

  return send_ok;
}

static uint32_t isotp_decode_stmin_us(uint8_t stmin_raw) {
  uint32_t ret = 0U;

  if (stmin_raw <= 0x7FU) {
    ret = ((uint32_t)stmin_raw * 1000U);
  } else if ((stmin_raw >= 0xF1U) && (stmin_raw <= 0xF9U)) {
    ret = 1000U;
  } else {
    ret = 127000U;
  }

  return ret;
}

static bool isotp_match_rx_frame(const CANPacket_t *msg) {
  bool msg_extended = msg->extended != 0U;

  return (msg->fd == 0U) &&
         (msg->returned == 0U) &&
         (msg->rejected == 0U) &&
         (msg->bus == isotp_session.bus) &&
         (msg_extended == isotp_session.rx_id_extended) &&
         (msg->addr == isotp_session.rx_id);
}

static isotp_parsed_frame_t isotp_parse_frame(const CANPacket_t *msg) {
  isotp_parsed_frame_t parsed = {
    .type = ISOTP_FRAME_INVALID,
    .data = NULL,
    .len = 0U,
    .total_len = 0U,
    .flow_status = 0U,
    .block_size = 0U,
    .stmin_raw = 0U,
    .sn = 0U,
  };

  uint8_t actual_len = dlc_to_len[msg->data_len_code];
  uint8_t data_offset = 0U;
  bool parse_ok = true;

  if (isotp_session.rx_ext_addr_enabled) {
    if ((actual_len == 0U) || (msg->data[0] != isotp_session.rx_ext_addr)) {
      parse_ok = false;
    } else {
      data_offset = 1U;
    }
  }

  if (actual_len <= data_offset) {
    parse_ok = false;
  }

  if (parse_ok) {
    const uint8_t *data = &msg->data[data_offset];
    uint8_t frame_len = actual_len - data_offset;

    switch (data[0] & 0xF0U) {
      case 0x00U:
        parsed.len = data[0] & 0x0FU;
        if ((parsed.len == 0U) || ((uint8_t)(parsed.len + 1U) > frame_len)) {
          break;
        }
        parsed.type = ISOTP_FRAME_SINGLE;
        parsed.data = &data[1];
        break;
      case 0x10U:
        if (frame_len < 2U) {
          break;
        }
        parsed.total_len = ((uint16_t)(data[0] & 0x0FU) << 8U) | data[1];
        if ((parsed.total_len == 0U) || (parsed.total_len > ISOTP_MSG_MAX_LEN)) {
          break;
        }
        parsed.type = ISOTP_FRAME_FIRST;
        parsed.data = &data[2];
        parsed.len = frame_len - 2U;
        break;
      case 0x20U:
        if (frame_len < 2U) {
          break;
        }
        parsed.type = ISOTP_FRAME_CONSECUTIVE;
        parsed.sn = data[0] & 0x0FU;
        parsed.data = &data[1];
        parsed.len = frame_len - 1U;
        break;
      case 0x30U:
        if (frame_len < 3U) {
          break;
        }
        parsed.type = ISOTP_FRAME_FLOW_CONTROL;
        parsed.flow_status = data[0] & 0x0FU;
        parsed.block_size = data[1];
        parsed.stmin_raw = data[2];
        break;
      default:
        break;
    }
  }

  return parsed;
}

static bool isotp_send_flow_control(uint8_t flow_status, uint8_t block_size, uint8_t stmin) {
  uint8_t fc[3] = {
    (uint8_t)(0x30U | (flow_status & 0x0FU)),
    block_size,
    stmin,
  };
  return isotp_send_can_frame(fc, sizeof(fc));
}

static void isotp_handle_single_frame(const isotp_parsed_frame_t *parsed) {
  (void)isotp_msg_queue_push(&isotp_rx_q, parsed->data, parsed->len);
}

static void isotp_handle_first_frame(const isotp_parsed_frame_t *parsed) {
  uint8_t single_cap = isotp_single_frame_capacity(isotp_session.rx_ext_addr_enabled);

  if ((parsed->total_len > single_cap) && (parsed->len <= parsed->total_len)) {
    (void)memcpy(isotp_session.rx.buf, parsed->data, parsed->len);
    isotp_session.rx.expected_len = parsed->total_len;
    isotp_session.rx.offset = parsed->len;
    isotp_session.rx.next_sn = 1U;
    isotp_session.rx.state = ISOTP_RX_WAIT_CF;

    if (!isotp_send_flow_control(0U, 0U, ISOTP_DEFAULT_RX_STMIN)) {
      isotp_abort_rx();
    }
  }
}

static void isotp_finish_rx_if_complete(void) {
  if (isotp_session.rx.offset >= isotp_session.rx.expected_len) {
    (void)isotp_msg_queue_push(&isotp_rx_q, isotp_session.rx.buf, isotp_session.rx.expected_len);
    isotp_abort_rx();
  }
}

static void isotp_handle_consecutive_frame(const isotp_parsed_frame_t *parsed) {
  if (parsed->sn == isotp_session.rx.next_sn) {
    uint16_t remaining = isotp_session.rx.expected_len - isotp_session.rx.offset;
    uint8_t copy_len = MIN(remaining, parsed->len);
    (void)memcpy(&isotp_session.rx.buf[isotp_session.rx.offset], parsed->data, copy_len);
    isotp_session.rx.offset += copy_len;
    isotp_session.rx.next_sn = (isotp_session.rx.next_sn + 1U) & 0x0FU;
    isotp_finish_rx_if_complete();
  } else {
    isotp_abort_rx();
  }
}

static bool isotp_pop_next_tx_pdu(uint8_t *data, uint16_t *len) {
  return isotp_msg_queue_pop(&isotp_tx_q, data, len);
}

static void isotp_try_start_next_tx(uint32_t now_us) {
  uint8_t frame[8];
  uint8_t single_cap = isotp_single_frame_capacity(isotp_session.tx_ext_addr_enabled);
  uint8_t ff_cap = isotp_first_frame_capacity(isotp_session.tx_ext_addr_enabled);
  bool start_tx = false;

  isotp_drain_write_staging_buffer();

  if (isotp_session.configured &&
      (isotp_session.tx.state == ISOTP_TX_IDLE) &&
      (can_slots_empty(can_queues[isotp_session.bus]) > 0U)) {
    start_tx = isotp_pop_next_tx_pdu(isotp_session.tx.buf, &isotp_session.tx.len);
  }

  if (start_tx) {
    isotp_drain_write_staging_buffer();
    isotp_session.tx.transfer_deadline_us = isotp_deadline_from_ms(now_us, isotp_session.tx_transfer_timeout_ms);

    if (isotp_session.tx.len <= single_cap) {
      bool sent_ok;
      frame[0] = isotp_session.tx.len & 0x0FU;
      (void)memcpy(&frame[1], isotp_session.tx.buf, isotp_session.tx.len);

      sent_ok = isotp_send_can_frame(frame, (uint8_t)(isotp_session.tx.len + 1U));
      if (!sent_ok) {
        // Safety rejected the SF. Drop the active PDU like any other TX failure.
      }
      isotp_abort_tx();
    } else {
      frame[0] = 0x10U | ((isotp_session.tx.len >> 8U) & 0x0FU);
      frame[1] = isotp_session.tx.len & 0xFFU;
      (void)memcpy(&frame[2], isotp_session.tx.buf, ff_cap);

      if (!isotp_send_can_frame(frame, (uint8_t)(ff_cap + 2U))) {
        isotp_abort_tx();
      } else {
        isotp_session.tx.offset = ff_cap;
        isotp_session.tx.next_sn = 1U;
        isotp_session.tx.block_cf_sent = 0U;
        isotp_session.tx.wait_fc_count = 0U;
        isotp_session.tx.stmin_us = 0U;
        isotp_session.tx.deadline_us = isotp_deadline_from_ms(now_us, isotp_session.tx_message_timeout_ms);
        isotp_session.tx.state = ISOTP_TX_WAIT_FC;
      }
    }
  }
}

static void isotp_try_send_consecutive_frames(uint32_t now_us) {
  uint8_t frame[8];
  uint8_t cf_cap = isotp_consecutive_frame_capacity(isotp_session.tx_ext_addr_enabled);
  bool tx_failed = false;

  while ((isotp_session.tx.offset < isotp_session.tx.len) &&
         !tx_failed &&
         (can_slots_empty(can_queues[isotp_session.bus]) > 0U) &&
         ((isotp_session.tx.block_size == 0U) || (isotp_session.tx.block_cf_sent < isotp_session.tx.block_size))) {
    uint16_t remaining = isotp_session.tx.len - isotp_session.tx.offset;
    uint8_t copy_len = MIN(remaining, cf_cap);

    frame[0] = 0x20U | (isotp_session.tx.next_sn & 0x0FU);
    (void)memcpy(&frame[1], &isotp_session.tx.buf[isotp_session.tx.offset], copy_len);

    if (!isotp_send_can_frame(frame, (uint8_t)(copy_len + 1U))) {
      isotp_abort_tx();
      tx_failed = true;
    } else {
      isotp_session.tx.offset += copy_len;
      isotp_session.tx.next_sn = (isotp_session.tx.next_sn + 1U) & 0x0FU;
      isotp_session.tx.block_cf_sent += 1U;

      if (isotp_session.tx.stmin_us != 0U) {
        break;
      }
    }
  }

  if (!tx_failed) {
    if (isotp_session.tx.offset >= isotp_session.tx.len) {
      isotp_abort_tx();
    } else if ((isotp_session.tx.block_size != 0U) && (isotp_session.tx.block_cf_sent >= isotp_session.tx.block_size)) {
      isotp_session.tx.deadline_us = isotp_deadline_from_ms(now_us, isotp_session.tx_message_timeout_ms);
      isotp_session.tx.state = ISOTP_TX_WAIT_FC;
    } else {
      isotp_session.tx.next_cf_us = (isotp_session.tx.stmin_us == 0U) ? now_us : (now_us + isotp_session.tx.stmin_us);
      isotp_session.tx.state = ISOTP_TX_WAIT_STMIN;
    }
  }
}

static void isotp_handle_flow_control(const isotp_parsed_frame_t *parsed, uint32_t now_us) {
  switch (parsed->flow_status) {
    case 0U:
      isotp_session.tx.block_size = parsed->block_size;
      isotp_session.tx.block_cf_sent = 0U;
      isotp_session.tx.stmin_us = isotp_decode_stmin_us(parsed->stmin_raw);
      isotp_session.tx.next_cf_us = now_us;
      isotp_session.tx.state = ISOTP_TX_WAIT_STMIN;
      break;
    case 1U:
      isotp_session.tx.wait_fc_count += 1U;
      if (isotp_session.tx.wait_fc_count > ISOTP_N_WFTMAX) {
        isotp_abort_tx();
      } else {
        isotp_session.tx.deadline_us = isotp_deadline_from_ms(now_us, isotp_session.tx_message_timeout_ms);
      }
      break;
    case 2U:
    default:
      isotp_abort_tx();
      break;
  }
}

static void isotp_kick(uint32_t now_us) {
  if (isotp_session.configured) {
    if (isotp_session.tx.state == ISOTP_TX_IDLE) {
      isotp_try_start_next_tx(now_us);
    }

    if ((isotp_session.tx.state == ISOTP_TX_WAIT_STMIN) &&
        isotp_time_reached(now_us, isotp_session.tx.next_cf_us)) {
      isotp_try_send_consecutive_frames(now_us);
    }
  }
}

void isotp_periodic_handler(uint32_t now_us) {
  if (isotp_session.configured) {
    if ((isotp_session.tx.state != ISOTP_TX_IDLE) &&
        isotp_time_reached(now_us, isotp_session.tx.transfer_deadline_us)) {
      isotp_abort_tx();
    }

    if ((isotp_session.tx.state == ISOTP_TX_WAIT_FC) &&
        isotp_time_reached(now_us, isotp_session.tx.deadline_us)) {
      isotp_abort_tx();
    }

    isotp_kick(now_us);
  }
}

void isotp_rx_hook(const CANPacket_t *msg, uint32_t now_us) {
  if (isotp_session.configured && isotp_match_rx_frame(msg)) {
    isotp_parsed_frame_t parsed = isotp_parse_frame(msg);

    if (parsed.type != ISOTP_FRAME_INVALID) {
      if ((parsed.type == ISOTP_FRAME_FLOW_CONTROL) &&
          (isotp_session.tx.state == ISOTP_TX_WAIT_FC)) {
        isotp_handle_flow_control(&parsed, now_us);
      } else {
        switch (isotp_session.rx.state) {
          case ISOTP_RX_IDLE:
            if (parsed.type == ISOTP_FRAME_SINGLE) {
              isotp_handle_single_frame(&parsed);
            } else if (parsed.type == ISOTP_FRAME_FIRST) {
              isotp_handle_first_frame(&parsed);
            } else {
              // Ignore unrelated frame types while idle.
            }
            break;
          case ISOTP_RX_WAIT_CF:
            if (parsed.type == ISOTP_FRAME_CONSECUTIVE) {
              isotp_handle_consecutive_frame(&parsed);
            } else if (parsed.type == ISOTP_FRAME_FIRST) {
              isotp_abort_rx();
              isotp_handle_first_frame(&parsed);
            } else {
              // Ignore unrelated frame types while waiting for CF.
            }
            break;
          default:
            break;
        }
      }

      isotp_kick(now_us);
    }
  }
}
