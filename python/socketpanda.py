import socket
import struct
import time

from .base import TIMEOUT

# /**
#  * struct canfd_frame - CAN flexible data rate frame structure
#  * @can_id: CAN ID of the frame and CAN_*_FLAG flags, see canid_t definition
#  * @len:    frame payload length in byte (0 .. CANFD_MAX_DLEN)
#  * @flags:  additional flags for CAN FD
#  * @__res0: reserved / padding
#  * @__res1: reserved / padding
#  * @data:   CAN FD frame payload (up to CANFD_MAX_DLEN byte)
#  */
# struct canfd_frame {
# 	canid_t can_id;  /* 32 bit CAN_ID + EFF/RTR/ERR flags */
# 	__u8    len;     /* frame payload length in byte */
# 	__u8    flags;   /* additional flags for CAN FD */
# 	__u8    __res0;  /* reserved / padding */
# 	__u8    __res1;  /* reserved / padding */
# 	__u8    data[CANFD_MAX_DLEN] __attribute__((aligned(8)));
# };
CAN_HEADER_FMT = "=IBB2x"
CAN_HEADER_LEN = struct.calcsize(CAN_HEADER_FMT)
CAN_MAX_DLEN = 8
CANFD_MAX_DLEN = 64

CAN_CONFIRM_FLAG = 0x800
CAN_EFF_FLAG = 0x80000000
CAN_ISOTP = getattr(socket, "CAN_ISOTP", None)

CANFD_BRS = 0x01 # bit rate switch (second bitrate for payload data)
CANFD_FDF = 0x04 # mark CAN FD for dual use of struct canfd_frame

# socket.SO_RXQ_OVFL is missing
# https://github.com/torvalds/linux/blob/47ac09b91befbb6a235ab620c32af719f8208399/include/uapi/asm-generic/socket.h#L61
SO_RXQ_OVFL = 40
SOL_CAN_BASE = 100
SOL_CAN_ISOTP = None if CAN_ISOTP is None else (SOL_CAN_BASE + CAN_ISOTP)

CAN_ISOTP_OPTS = 1
CAN_ISOTP_RECV_FC = 2
CAN_ISOTP_LL_OPTS = 5

CAN_ISOTP_LISTEN_MODE = 0x001
CAN_ISOTP_EXTEND_ADDR = 0x002
CAN_ISOTP_RX_EXT_ADDR = 0x200

CAN_ISOTP_DEFAULT_EXT_ADDRESS = 0x00
CAN_ISOTP_DEFAULT_PAD_CONTENT = 0xCC
CAN_ISOTP_DEFAULT_FRAME_TXTIME = 0
CAN_ISOTP_DEFAULT_RECV_WFTMAX = 0
CAN_ISOTP_DEFAULT_LL_MTU = 16
CAN_ISOTP_DEFAULT_LL_TX_DL = 8
CAN_ISOTP_DEFAULT_LL_TX_FLAGS = 0
PANDA_CAN_CNT = 3
ISOTP_MAX_LEN = 0xFFF

import typing
@typing.no_type_check # mypy struggles with macOS here...
def create_socketcan(interface:str, recv_buffer_size:int) -> socket.socket:
  # settings mostly from https://github.com/linux-can/can-utils/blob/master/candump.c
  socketcan = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
  socketcan.setblocking(False)
  socketcan.setsockopt(socket.SOL_CAN_RAW, socket.CAN_RAW_FD_FRAMES, 1)
  socketcan.setsockopt(socket.SOL_CAN_RAW, socket.CAN_RAW_RECV_OWN_MSGS, 1)
  socketcan.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, recv_buffer_size)
  # TODO: why is it always 2x the requested size?
  assert socketcan.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF) == recv_buffer_size * 2
  # TODO: how to dectect and alert on buffer overflow?
  socketcan.setsockopt(socket.SOL_SOCKET, SO_RXQ_OVFL, 1)
  socketcan.bind((interface,))
  return socketcan


def normalize_isotp_arb_id(arb_id: int, extended: bool | None = None) -> tuple[int, bool]:
  if arb_id < 0:
    raise ValueError(f"invalid ISO-TP arbitration ID: {arb_id}")

  if extended is None:
    extended = arb_id > 0x7FF

  if extended:
    if arb_id > 0x1FFFFFFF:
      raise ValueError(f"invalid extended ISO-TP arbitration ID: {hex(arb_id)}")
  elif arb_id > 0x7FF:
    raise ValueError(f"invalid standard ISO-TP arbitration ID: {hex(arb_id)}")

  return arb_id, extended


def create_kernel_isotp_socket(interface: str, *, tx_id: int, rx_id: int, tx_extended: bool | None = None,
                               rx_extended: bool | None = None, ext_address: int | None = None,
                               rx_ext_address: int | None = None, bs: int = 0, stmin: int = 0,
                               listen_only: bool = False, timeout: float | None = None) -> socket.socket:
  if CAN_ISOTP is None or SOL_CAN_ISOTP is None:
    raise NotImplementedError("CAN_ISOTP is not available on this platform")

  tx_id, tx_extended = normalize_isotp_arb_id(tx_id, tx_extended)
  rx_id, rx_extended = normalize_isotp_arb_id(rx_id, rx_extended)

  if rx_ext_address == ext_address:
    rx_ext_address = None

  flags = 0
  tx_ext = CAN_ISOTP_DEFAULT_EXT_ADDRESS if ext_address is None else ext_address
  rx_ext = CAN_ISOTP_DEFAULT_EXT_ADDRESS if rx_ext_address is None else rx_ext_address

  if ext_address is not None:
    flags |= CAN_ISOTP_EXTEND_ADDR
  if rx_ext_address is not None:
    flags |= CAN_ISOTP_RX_EXT_ADDR
  if listen_only:
    flags |= CAN_ISOTP_LISTEN_MODE

  sock = socket.socket(socket.PF_CAN, socket.SOCK_DGRAM, CAN_ISOTP)
  sock.setsockopt(
    SOL_CAN_ISOTP,
    CAN_ISOTP_OPTS,
    struct.pack("@2I4B", flags, CAN_ISOTP_DEFAULT_FRAME_TXTIME, tx_ext,
                CAN_ISOTP_DEFAULT_PAD_CONTENT, CAN_ISOTP_DEFAULT_PAD_CONTENT, rx_ext),
  )
  sock.setsockopt(
    SOL_CAN_ISOTP,
    CAN_ISOTP_RECV_FC,
    struct.pack("@3B", bs, stmin, CAN_ISOTP_DEFAULT_RECV_WFTMAX),
  )
  sock.setsockopt(
    SOL_CAN_ISOTP,
    CAN_ISOTP_LL_OPTS,
    struct.pack("@3B", CAN_ISOTP_DEFAULT_LL_MTU, CAN_ISOTP_DEFAULT_LL_TX_DL, CAN_ISOTP_DEFAULT_LL_TX_FLAGS),
  )
  sock.settimeout(timeout)

  tx_bind_id = tx_id | (CAN_EFF_FLAG if tx_extended else 0)
  rx_bind_id = rx_id | (CAN_EFF_FLAG if rx_extended else 0)
  sock.bind((interface, rx_bind_id, tx_bind_id))
  return sock

# Panda class substitute for socketcan device (to support using the uds/iso-tp/xcp/ccp library)
class SocketPanda():
  CAN_SEND_TIMEOUT_MS = 10
  ISOTP_SEND_TIMEOUT_MS = 10

  def __init__(self, interface:str="can0", recv_buffer_size:int=212992) -> None:
    self.interface = interface
    self.recv_buffer_size = recv_buffer_size
    self.socket = create_socketcan(interface, recv_buffer_size)
    self._isotp_socket: socket.socket | None = None
    self._isotp_bus = 0
    self._isotp_tx_arb_id: int | None = None
    self._isotp_rx_arb_id: int | None = None
    self._isotp_tx_extended: bool | None = None
    self._isotp_rx_extended: bool | None = None
    self._isotp_tx_ext_addr: int | None = None
    self._isotp_rx_ext_addr: int | None = None
    self._isotp_message_timeout_ms: int | None = None
    self._isotp_transfer_timeout_ms: int | None = None

  def __del__(self):
    self.close()

  def close(self) -> None:
    if hasattr(self, "_isotp_socket") and self._isotp_socket is not None:
      self._isotp_socket.close()
      self._isotp_socket = None
    if hasattr(self, "socket"):
      self.socket.close()

  def __enter__(self):
    return self

  def __exit__(self, exc_type, exc, tb):
    self.close()

  def get_serial(self) -> tuple[int, int]:
    return (0, 0)

  def get_version(self) -> int:
    return 0

  def can_clear(self, bus:int) -> None:
    self.socket.close()
    self.socket = create_socketcan(self.interface, self.recv_buffer_size)
    self._reset_isotp_socket()

  def set_safety_mode(self, mode:int, param=0) -> None:
    pass

  def can_send_many(self, arr, *, fd=False, timeout=CAN_SEND_TIMEOUT_MS) -> None:
    for msg in arr:
      self.can_send(*msg, fd=fd, timeout=timeout)

  def can_send(self, addr, dat, bus, *, fd=False, timeout=CAN_SEND_TIMEOUT_MS) -> None:
    # Even if the CANFD_FDF flag is not set, the data still must be 8 bytes for classic CAN frames.
    data_len = CANFD_MAX_DLEN if fd else CAN_MAX_DLEN
    msg_len = len(dat)
    msg_dat = dat.ljust(data_len, b'\x00')

    # Set extended ID flag
    if addr > 0x7ff:
      addr |= CAN_EFF_FLAG

    # Set FD flags
    flags = CANFD_BRS | CANFD_FDF if fd else 0

    can_frame = struct.pack(CAN_HEADER_FMT, addr, msg_len, flags) + msg_dat

    # Try to send until timeout. sendto might block if the TX buffer is full.
    # TX buffer size can also be adjusted through `ip link set can0 txqueuelen <size>` if needed
    start_t = time.monotonic()
    while (time.monotonic() - start_t < (timeout / 1000)) or (timeout == 0):
      try:
        self.socket.sendto(can_frame, (self.interface,))
        break
      except (BlockingIOError, OSError):
        continue
    else:
      raise TimeoutError

  def can_recv(self) -> list[tuple[int, bytes, int]]:
    msgs = list()
    while True:
      try:
        dat, _, msg_flags, _ = self.socket.recvmsg(self.recv_buffer_size)
        assert len(dat) >= CAN_HEADER_LEN, f"ERROR: received {len(dat)} bytes"

        can_id, msg_len, _ = struct.unpack(CAN_HEADER_FMT, dat[:CAN_HEADER_LEN])
        assert len(dat) >= CAN_HEADER_LEN + msg_len, f"ERROR: received {len(dat)} bytes, expected at least {CAN_HEADER_LEN + msg_len} bytes"

        msg_dat = dat[CAN_HEADER_LEN:CAN_HEADER_LEN+msg_len]
        bus = 128 if (msg_flags & CAN_CONFIRM_FLAG) else 0
        msgs.append((can_id, msg_dat, bus))
      except BlockingIOError:
        break # buffered data exhausted
    return msgs

  def _reset_isotp_socket(self) -> None:
    if self._isotp_socket is not None:
      self._isotp_socket.close()
      self._isotp_socket = None

  def _require_isotp_config(self) -> tuple[int, int, bool, bool]:
    if self._isotp_tx_arb_id is None or self._isotp_rx_arb_id is None:
      raise RuntimeError("ISO-TP arbitration IDs are not configured")
    if self._isotp_tx_extended is None or self._isotp_rx_extended is None:
      raise RuntimeError("ISO-TP arbitration ID format is not configured")
    return self._isotp_tx_arb_id, self._isotp_rx_arb_id, self._isotp_tx_extended, self._isotp_rx_extended

  def _get_isotp_socket(self) -> socket.socket:
    if self._isotp_socket is None:
      tx_arb_id, rx_arb_id, tx_extended, rx_extended = self._require_isotp_config()
      self._isotp_socket = create_kernel_isotp_socket(
        self.interface,
        tx_id=tx_arb_id,
        rx_id=rx_arb_id,
        tx_extended=tx_extended,
        rx_extended=rx_extended,
        ext_address=self._isotp_tx_ext_addr,
        rx_ext_address=self._isotp_rx_ext_addr,
        timeout=None,
      )
    return self._isotp_socket

  def _with_socket_timeout(self, sock: socket.socket, timeout_ms: int):
    timeout_s = None if timeout_ms == 0 else (timeout_ms / 1000.0)
    old_timeout = sock.gettimeout()
    sock.settimeout(timeout_s)
    return old_timeout

  # ******************* ISO-TP *******************

  def set_isotp_bus(self, bus: int):
    if not (0 <= bus < PANDA_CAN_CNT):
      raise ValueError(f"invalid ISO-TP bus: {bus}")
    self._isotp_bus = bus

  def set_isotp_tx_arb_id(self, arb_id: int, extended: bool | None = None):
    self._isotp_tx_arb_id, self._isotp_tx_extended = normalize_isotp_arb_id(arb_id, extended)
    self._reset_isotp_socket()

  def set_isotp_rx_arb_id(self, arb_id: int, extended: bool | None = None):
    self._isotp_rx_arb_id, self._isotp_rx_extended = normalize_isotp_arb_id(arb_id, extended)
    self._reset_isotp_socket()

  def set_isotp_ext_addr(self, tx_addr: int | None = None, rx_addr: int | None = None):
    tx_cfg = 0 if tx_addr is None else (0x100 | int(tx_addr))
    rx_cfg = 0 if rx_addr is None else (0x100 | int(rx_addr))

    if not (0 <= tx_cfg <= 0x1FF):
      raise ValueError(f"invalid ISO-TP TX extended address: {tx_addr}")
    if not (0 <= rx_cfg <= 0x1FF):
      raise ValueError(f"invalid ISO-TP RX extended address: {rx_addr}")

    self._isotp_tx_ext_addr = tx_addr
    self._isotp_rx_ext_addr = rx_addr
    self._reset_isotp_socket()

  def set_isotp_tx_timeouts(self, message_timeout_ms: int, transfer_timeout_ms: int):
    if message_timeout_ms <= 0 or transfer_timeout_ms <= 0:
      raise ValueError("ISO-TP timeouts must be positive")
    self._isotp_message_timeout_ms = int(message_timeout_ms)
    self._isotp_transfer_timeout_ms = int(transfer_timeout_ms)

  def configure_isotp(self, bus: int, tx_arb_id: int, rx_arb_id: int, *, tx_extended: bool | None = None,
                      rx_extended: bool | None = None, tx_ext_addr: int | None = None,
                      rx_ext_addr: int | None = None, message_timeout_ms: int | None = None,
                      transfer_timeout_ms: int | None = None):
    self.set_isotp_bus(bus)
    self.set_isotp_tx_arb_id(tx_arb_id, tx_extended)
    self.set_isotp_rx_arb_id(rx_arb_id, rx_extended)
    self.set_isotp_ext_addr(tx_ext_addr, rx_ext_addr)

    if (message_timeout_ms is None) != (transfer_timeout_ms is None):
      raise ValueError("ISO-TP timeouts must be configured together")
    if message_timeout_ms is not None and transfer_timeout_ms is not None:
      self.set_isotp_tx_timeouts(message_timeout_ms, transfer_timeout_ms)

  def isotp_send_many(self, payloads, *, timeout=ISOTP_SEND_TIMEOUT_MS):
    sock = self._get_isotp_socket()
    old_timeout = self._with_socket_timeout(sock, timeout)
    try:
      for payload in payloads:
        payload_bytes = bytes(payload)
        if not (0 < len(payload_bytes) <= ISOTP_MAX_LEN):
          raise ValueError(f"invalid ISO-TP payload length: {len(payload_bytes)}")
        sock.send(payload_bytes)
    except (TimeoutError, socket.timeout) as exc:
      raise TimeoutError from exc
    finally:
      sock.settimeout(old_timeout)

  def isotp_send(self, payload, *, timeout=ISOTP_SEND_TIMEOUT_MS):
    self.isotp_send_many([payload], timeout=timeout)

  def isotp_recv(self, *, timeout: int = TIMEOUT):
    sock = self._get_isotp_socket()
    old_timeout = self._with_socket_timeout(sock, timeout)
    try:
      try:
        dat = sock.recv(ISOTP_MAX_LEN)
      except (TimeoutError, socket.timeout):
        return []
    finally:
      sock.settimeout(old_timeout)

    return [dat] if dat else []
