#!/usr/bin/env python3
import random
import struct
import time
import unittest

from opendbc.car.structs import CarParams
from panda import DLC_TO_LEN, USBPACKET_MAX_SIZE, pack_can_buffer, unpack_can_buffer
from panda.python import pack_isotp_arb_id, pack_isotp_buffer, unpack_isotp_buffer
from panda.tests.libpanda import libpanda_py

lpp = libpanda_py.libpanda

CHUNK_SIZE = USBPACKET_MAX_SIZE
TX_QUEUES = (lpp.tx1_q, lpp.tx2_q, lpp.tx3_q)


def unpackage_can_msg(pkt):
  dat_len = DLC_TO_LEN[pkt[0].data_len_code]
  dat = bytes(pkt[0].data[0:dat_len])
  return pkt[0].addr, dat, pkt[0].bus


def random_can_messages(n, bus=None):
  msgs = []
  for _ in range(n):
    if bus is None:
      bus = random.randint(0, 3)
    address = random.randint(1, (1 << 29) - 1)
    data = bytes([random.getrandbits(8) for _ in range(DLC_TO_LEN[random.randrange(0, len(DLC_TO_LEN))])])
    msgs.append((address, data, bus))
  return msgs


class LibpandaIsoTpTransport:
  def __init__(self):
    self.isotp_rx_overflow_buffer = b""
    self.reset_state()

  def reset_state(self):
    self.isotp_rx_overflow_buffer = b""
    lpp.set_safety_hooks(CarParams.SafetyModel.allOutput, 0)
    lpp.comms_can_reset()
    lpp.comms_isotp_reset()
    lpp.set_microsecond_timer(0)

    pkt = libpanda_py.ffi.new('CANPacket_t *')
    for q in TX_QUEUES + (lpp.rx_q,):
      while lpp.can_pop(q, pkt):
        pass

  def can_reset_communications(self):
    self.reset_state()

  def send_control(self, request, param1=0, param2=0, length=0):
    req = libpanda_py.ffi.new('ControlPacket_t *')
    req[0].request = request
    req[0].param1 = param1
    req[0].param2 = param2
    req[0].length = length
    resp = libpanda_py.ffi.new('uint8_t[64]')
    return lpp.comms_control_handler(req, resp), bytes(resp)

  def configure_isotp(self, bus: int, tx_arb_id: int, rx_arb_id: int, *, tx_extended: bool | None = None,
                      rx_extended: bool | None = None, tx_ext_addr: int | None = None,
                      rx_ext_addr: int | None = None, message_timeout_ms: int | None = None,
                      transfer_timeout_ms: int | None = None):
    self.send_control(0xea, bus)

    tx_packed = pack_isotp_arb_id(tx_arb_id, tx_extended)
    self.send_control(0xeb, tx_packed & 0xFFFF, tx_packed >> 16)

    rx_packed = pack_isotp_arb_id(rx_arb_id, rx_extended)
    self.send_control(0xec, rx_packed & 0xFFFF, rx_packed >> 16)

    tx_cfg = 0 if tx_ext_addr is None else (0x100 | int(tx_ext_addr))
    rx_cfg = 0 if rx_ext_addr is None else (0x100 | int(rx_ext_addr))
    self.send_control(0xed, tx_cfg, rx_cfg)

    if message_timeout_ms is not None and transfer_timeout_ms is not None:
      self.send_control(0xee, int(message_timeout_ms), int(transfer_timeout_ms))

  def read_isotp_bulk(self, max_len=64):
    dat = libpanda_py.ffi.new(f"uint8_t[{max_len}]")
    rx_len = lpp.comms_isotp_read(dat, max_len)
    return bytes(dat[0:rx_len])

  def isotp_send(self, payload, *, timeout: int):
    del timeout
    tx = bytes(pack_isotp_buffer([payload]))
    lpp.comms_isotp_write(tx, len(tx))

  def isotp_recv(self, *, timeout: int) -> list[bytes]:
    deadline = time.monotonic() + (timeout / 1000.0)
    dat = libpanda_py.ffi.new("uint8_t[16384]")

    while True:
      rx_len = lpp.comms_isotp_read(dat, 16384)
      msgs, self.isotp_rx_overflow_buffer = unpack_isotp_buffer(self.isotp_rx_overflow_buffer + bytes(dat[0:rx_len]))
      if msgs or time.monotonic() >= deadline:
        return msgs
      time.sleep(0.001)


class TestPandaComms(unittest.TestCase):
  def setUp(self):
    self.transport = LibpandaIsoTpTransport()

  def test_tx_queues(self):
    for bus in range(len(TX_QUEUES)):
      message = (0x100, b"test", bus)

      can_pkt_tx = libpanda_py.make_CANPacket(message[0], message[2], message[1])
      can_pkt_rx = libpanda_py.ffi.new('CANPacket_t *')

      assert lpp.can_push(TX_QUEUES[bus], can_pkt_tx), "CAN push failed"
      assert lpp.can_pop(TX_QUEUES[bus], can_pkt_rx), "CAN pop failed"

      assert unpackage_can_msg(can_pkt_rx) == message

  def test_comms_reset_rx(self):
    # store some test messages in the queue
    test_msg = (0x100, b"test", 0)
    for _ in range(100):
      can_pkt_tx = libpanda_py.make_CANPacket(test_msg[0], test_msg[2], test_msg[1])
      lpp.can_push(lpp.rx_q, can_pkt_tx)

    # read a small chunk such that we have some overflow
    TINY_CHUNK_SIZE = 6
    dat = libpanda_py.ffi.new(f"uint8_t[{TINY_CHUNK_SIZE}]")
    rx_len = lpp.comms_can_read(dat, TINY_CHUNK_SIZE)
    assert rx_len == TINY_CHUNK_SIZE, "comms_can_read returned too little data"

    _, overflow = unpack_can_buffer(bytes(dat))
    assert len(overflow) > 0, "overflow buffer should not be empty"

    # reset the comms to clear the overflow buffer on the panda side
    lpp.comms_can_reset()

    # read a large chunk, which should now contain valid messages
    LARGE_CHUNK_SIZE = 512
    dat = libpanda_py.ffi.new(f"uint8_t[{LARGE_CHUNK_SIZE}]")
    rx_len = lpp.comms_can_read(dat, LARGE_CHUNK_SIZE)
    assert rx_len == LARGE_CHUNK_SIZE, "comms_can_read returned too little data"

    msgs, _ = unpack_can_buffer(bytes(dat))
    assert len(msgs) > 0, "message buffer should not be empty"
    for m in msgs:
      assert m == test_msg, "message buffer should contain valid test messages"

  def test_comms_reset_tx(self):
    # store some test messages in the queue
    test_msg = (0x100, b"test", 0)
    packed = pack_can_buffer([test_msg for _ in range(100)], chunk=True)

    # write a small chunk such that we have some overflow
    TINY_CHUNK_SIZE = 6
    lpp.comms_can_write(bytes(packed[0][:TINY_CHUNK_SIZE]), TINY_CHUNK_SIZE)

    # reset the comms to clear the overflow buffer on the panda side
    lpp.comms_can_reset()

    # write a full valid chunk, which should now contain valid messages
    lpp.comms_can_write(bytes(packed[1]), len(packed[1]))

    # read the messages from the queue and make sure they're valid
    queue_msgs = []
    pkt = libpanda_py.ffi.new('CANPacket_t *')
    while lpp.can_pop(TX_QUEUES[0], pkt):
      queue_msgs.append(unpackage_can_msg(pkt))

    assert len(queue_msgs) > 0, "message buffer should not be empty"
    for m in queue_msgs:
      assert m == test_msg, "message buffer should contain valid test messages"


  def test_can_send_usb(self):
    for bus in range(3):
      with self.subTest(bus=bus):
        for _ in range(100):
          msgs = random_can_messages(200, bus=bus)
          packed = pack_can_buffer(msgs)

          # Simulate USB bulk chunks
          for buf in packed:
            for i in range(0, len(buf), CHUNK_SIZE):
              chunk_len = min(CHUNK_SIZE, len(buf) - i)
              lpp.comms_can_write(bytes(buf[i:i+chunk_len]), chunk_len)

          # Check that they ended up in the right buffers
          queue_msgs = []
          pkt = libpanda_py.ffi.new('CANPacket_t *')
          while lpp.can_pop(TX_QUEUES[bus], pkt):
            queue_msgs.append(unpackage_can_msg(pkt))

          self.assertEqual(len(queue_msgs), len(msgs))
          self.assertEqual(queue_msgs, msgs)

  def test_can_receive_usb(self):
    msgs = random_can_messages(50000)
    packets = [libpanda_py.make_CANPacket(m[0], m[2], m[1]) for m in msgs]

    rx_msgs = []
    overflow_buf = b""
    while len(packets) > 0:
      # Push into queue
      while lpp.can_slots_empty(lpp.rx_q) > 0 and len(packets) > 0:
        lpp.can_push(lpp.rx_q, packets.pop(0))

      # Simulate USB bulk IN chunks
      MAX_TRANSFER_SIZE = 16384
      dat = libpanda_py.ffi.new(f"uint8_t[{CHUNK_SIZE}]")
      while True:
        buf = b""
        while len(buf) < MAX_TRANSFER_SIZE:
          max_size = min(CHUNK_SIZE, MAX_TRANSFER_SIZE - len(buf))
          rx_len = lpp.comms_can_read(dat, max_size)
          buf += bytes(dat[0:rx_len])
          if rx_len < max_size:
            break

        if len(buf) == 0:
          break
        unpacked_msgs, overflow_buf = unpack_can_buffer(overflow_buf + buf)
        rx_msgs.extend(unpacked_msgs)

    self.assertEqual(len(rx_msgs), len(msgs))
    self.assertEqual(rx_msgs, msgs)

  def test_isotp_send_single_frame(self):
    self.transport.configure_isotp(0, 0x700, 0x708)
    payload = b"abcdef"
    record = struct.pack("<H", len(payload)) + payload

    lpp.comms_isotp_write(record[:3], 3)
    lpp.comms_isotp_write(record[3:], len(record) - 3)
    lpp.isotp_periodic_handler(0)

    pkt = libpanda_py.ffi.new('CANPacket_t *')
    assert lpp.can_pop(TX_QUEUES[0], pkt), "ISO-TP single frame was not queued"
    assert unpackage_can_msg(pkt) == (0x700, b"\x06abcdef\xcc", 0)

  def test_isotp_receive_single_frame(self):
    self.transport.configure_isotp(0, 0x700, 0x708)
    msg = libpanda_py.make_CANPacket(0x708, 0, b"\x03xyz")

    lpp.isotp_rx_hook(msg, 0)

    assert self.transport.read_isotp_bulk() == struct.pack("<H", 3) + b"xyz"

  def test_isotp_send_multi_frame_after_fc(self):
    self.transport.configure_isotp(0, 0x700, 0x708)
    payload = b"0123456789"
    record = struct.pack("<H", len(payload)) + payload

    lpp.comms_isotp_write(record, len(record))
    lpp.isotp_periodic_handler(0)

    pkt = libpanda_py.ffi.new('CANPacket_t *')
    assert lpp.can_pop(TX_QUEUES[0], pkt), "ISO-TP first frame was not queued"
    assert unpackage_can_msg(pkt) == (0x700, b"\x10\x0a012345", 0)

    fc = libpanda_py.make_CANPacket(0x708, 0, b"\x30\x00\x00")
    lpp.isotp_rx_hook(fc, 0)

    assert lpp.can_pop(TX_QUEUES[0], pkt), "ISO-TP consecutive frame was not queued"
    assert unpackage_can_msg(pkt) == (0x700, b"\x216789\xcc\xcc\xcc", 0)

  def test_isotp_send_multi_frame_after_wait_fc(self):
    self.transport.configure_isotp(0, 0x700, 0x708, message_timeout_ms=10, transfer_timeout_ms=100)
    payload = b"0123456789"
    record = struct.pack("<H", len(payload)) + payload

    lpp.comms_isotp_write(record, len(record))
    lpp.isotp_periodic_handler(0)

    pkt = libpanda_py.ffi.new('CANPacket_t *')
    assert lpp.can_pop(TX_QUEUES[0], pkt), "ISO-TP first frame was not queued"
    assert unpackage_can_msg(pkt) == (0x700, b"\x10\x0a012345", 0)

    wait_fc = libpanda_py.make_CANPacket(0x708, 0, b"\x31\x00\x00")
    lpp.isotp_rx_hook(wait_fc, 0)

    assert not lpp.can_pop(TX_QUEUES[0], pkt), "ISO-TP should stay paused after WAIT flow control"

    continue_fc = libpanda_py.make_CANPacket(0x708, 0, b"\x30\x00\x00")
    lpp.isotp_rx_hook(continue_fc, 5000)

    assert lpp.can_pop(TX_QUEUES[0], pkt), "ISO-TP consecutive frame was not queued after WAIT + CTS"
    assert unpackage_can_msg(pkt) == (0x700, b"\x216789\xcc\xcc\xcc", 0)

  def test_isotp_send_multi_frame_wait_fc_timeout_starts_next_tx(self):
    self.transport.configure_isotp(0, 0x700, 0x708, message_timeout_ms=10, transfer_timeout_ms=100)
    payloads = [b"0123456789", b"ABCDEFGHIJ"]
    tx = bytes(pack_isotp_buffer(payloads))

    lpp.comms_isotp_write(tx, len(tx))
    lpp.isotp_periodic_handler(0)

    pkt = libpanda_py.ffi.new('CANPacket_t *')
    assert lpp.can_pop(TX_QUEUES[0], pkt), "first ISO-TP first frame was not queued"
    assert unpackage_can_msg(pkt) == (0x700, b"\x10\x0a012345", 0)

    wait_fc = libpanda_py.make_CANPacket(0x708, 0, b"\x31\x00\x00")
    lpp.isotp_rx_hook(wait_fc, 0)

    lpp.isotp_periodic_handler(9999)
    assert not lpp.can_pop(TX_QUEUES[0], pkt), "ISO-TP advanced before WAIT timeout expired"

    lpp.isotp_periodic_handler(10000)
    assert lpp.can_pop(TX_QUEUES[0], pkt), "next ISO-TP first frame was not queued after WAIT timeout"
    assert unpackage_can_msg(pkt) == (0x700, b"\x10\x0aABCDEF", 0)

  def test_isotp_send_multi_frame_after_abort_fc(self):
    self.transport.configure_isotp(0, 0x700, 0x708)
    payloads = [b"0123456789", b"ABCDEFGHIJ"]
    tx = bytes(pack_isotp_buffer(payloads))

    lpp.comms_isotp_write(tx, len(tx))
    lpp.isotp_periodic_handler(0)

    pkt = libpanda_py.ffi.new('CANPacket_t *')
    assert lpp.can_pop(TX_QUEUES[0], pkt), "first ISO-TP first frame was not queued"
    assert unpackage_can_msg(pkt) == (0x700, b"\x10\x0a012345", 0)

    abort_fc = libpanda_py.make_CANPacket(0x708, 0, b"\x32\x00\x00")
    lpp.isotp_rx_hook(abort_fc, 0)

    assert lpp.can_pop(TX_QUEUES[0], pkt), "next ISO-TP first frame was not queued after ABORT flow control"
    assert unpackage_can_msg(pkt) == (0x700, b"\x10\x0aABCDEF", 0)

  def test_isotp_receive_multi_frame(self):
    self.transport.configure_isotp(0, 0x700, 0x708)
    ff = libpanda_py.make_CANPacket(0x708, 0, b"\x10\x0a012345")
    cf = libpanda_py.make_CANPacket(0x708, 0, b"\x216789")

    lpp.isotp_rx_hook(ff, 0)

    pkt = libpanda_py.ffi.new('CANPacket_t *')
    assert lpp.can_pop(TX_QUEUES[0], pkt), "ISO-TP flow control was not queued"
    assert unpackage_can_msg(pkt) == (0x700, b"\x30\x00\x00\xcc\xcc\xcc\xcc\xcc", 0)

    lpp.isotp_rx_hook(cf, 0)

    assert self.transport.read_isotp_bulk() == struct.pack("<H", 10) + b"0123456789"

  def test_isotp_usb_write_ready_requires_room_for_one_max_record(self):
    self.transport.configure_isotp(0, 0x700, 0x708)
    payload = bytes([0xA5]) * 0xFFF
    record = struct.pack("<H", len(payload)) + payload

    lpp.comms_isotp_write(record, len(record))
    lpp.isotp_periodic_handler(0)

    for _ in range(3):
      lpp.comms_isotp_write(record, len(record))

    lpp.comms_isotp_write(record[:-1], len(record) - 1)
    assert lpp.comms_isotp_can_write_usb(), "EP5 should stay armed while one max record can still be completed"

    lpp.comms_isotp_write(record[-1:], 1)
    assert not lpp.comms_isotp_can_write_usb(), "EP5 should stop accepting writes once less than one max record fits"


if __name__ == "__main__":
  unittest.main()
