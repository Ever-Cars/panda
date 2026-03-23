import math
import socket
import threading
import time
from dataclasses import dataclass
from typing import Callable, Protocol

from panda.python.socketpanda import CAN_EFF_FLAG, SocketPanda, create_kernel_isotp_socket


class IsoTpTransport(Protocol):
  def can_reset_communications(self) -> None:
    ...

  def configure_isotp(self, bus: int, tx_arb_id: int, rx_arb_id: int, *, tx_extended: bool | None = None,
                      rx_extended: bool | None = None, tx_ext_addr: int | None = None,
                      rx_ext_addr: int | None = None, message_timeout_ms: int | None = None,
                      transfer_timeout_ms: int | None = None) -> None:
    ...

  def isotp_send(self, payload, *, timeout: int) -> None:
    ...

  def isotp_recv(self, *, timeout: int) -> list[bytes]:
    ...


def interface_exists(interface: str) -> bool:
  try:
    socket.if_nametoindex(interface)
  except OSError:
    return False
  return True


def build_payload(length: int, salt: int) -> bytes:
  return bytes(((salt + i) % 256) for i in range(length))


@dataclass(frozen=True)
class IsoTpEchoCase:
  name: str
  tx_arb_id: int
  rx_arb_id: int
  payload_lengths: tuple[int, ...]
  block_size: int = 0
  stmin: int = 0
  tx_extended: bool | None = None
  rx_extended: bool | None = None
  tx_ext_addr: int | None = None
  rx_ext_addr: int | None = None
  message_timeout_ms: int = 1000
  transfer_timeout_ms: int = 2000
  round_trip_timeout_s: float = 2.0

  def payloads(self) -> tuple[bytes, ...]:
    return tuple(build_payload(length, index * 0x11) for index, length in enumerate(self.payload_lengths, start=1))

  def expected_request_delay_s(self, payload_len: int) -> float:
    if self.stmin == 0:
      return 0.0

    sf_capacity = 7 - int(self.tx_ext_addr is not None)
    if payload_len <= sf_capacity:
      return 0.0

    ff_capacity = 6 - int(self.tx_ext_addr is not None)
    cf_capacity = 7 - int(self.tx_ext_addr is not None)
    remaining = payload_len - ff_capacity
    cf_count = math.ceil(remaining / cf_capacity)

    return max(cf_count - 1, 0) * (self.stmin / 1000.0)


ISOTP_ECHO_CASES = (
  IsoTpEchoCase(
    name="standard",
    tx_arb_id=0x7A1,
    rx_arb_id=0x7A9,
    payload_lengths=(1, 7, 62, 64, 256),
  ),
  IsoTpEchoCase(
    name="extended_id_block_size",
    tx_arb_id=0x18DA00F1,
    rx_arb_id=0x18DAF100,
    tx_extended=True,
    rx_extended=True,
    block_size=4,
    payload_lengths=(64, 256),
  ),
  IsoTpEchoCase(
    name="stmin",
    tx_arb_id=0x701,
    rx_arb_id=0x709,
    block_size=8,
    stmin=20,
    payload_lengths=(64,),
  ),
  IsoTpEchoCase(
    name="extended_address_same",
    tx_arb_id=0x6A1,
    rx_arb_id=0x6A9,
    tx_ext_addr=0xAA,
    rx_ext_addr=0xAA,
    payload_lengths=(1, 6, 62, 64, 256),
  ),
  IsoTpEchoCase(
    name="extended_address_mixed",
    tx_arb_id=0x6B1,
    rx_arb_id=0x6B9,
    tx_ext_addr=0xAA,
    rx_ext_addr=0xBB,
    payload_lengths=(1, 6, 62, 64, 256),
  ),
  IsoTpEchoCase(
    name="max_transfer",
    tx_arb_id=0x7C1,
    rx_arb_id=0x7C9,
    payload_lengths=(0xFFF,),
    transfer_timeout_ms=10000,
    round_trip_timeout_s=20.0,
  ),
)


class KernelIsoTpEchoEcu:
  def __init__(self, interface: str, case: IsoTpEchoCase):
    self._interface = interface
    self._case = case
    self._stop = threading.Event()
    self._ready = threading.Event()
    self._thread_error: Exception | None = None
    self._socket: socket.socket | None = None
    self._thread: threading.Thread | None = None
    self.rx_count = 0
    self.tx_count = 0

  def __enter__(self):
    self._socket = create_kernel_isotp_socket(
      self._interface,
      tx_id=self._case.rx_arb_id,
      rx_id=self._case.tx_arb_id,
      ext_address=self._case.rx_ext_addr,
      rx_ext_address=self._case.tx_ext_addr,
      bs=self._case.block_size,
      stmin=self._case.stmin,
      timeout=0.1,
    )

    self._thread = threading.Thread(target=self._run, name=f"isotp-echo-{self._case.name}", daemon=True)
    self._thread.start()
    if not self._ready.wait(timeout=1.0):
      raise RuntimeError("kernel ISO-TP echo ECU did not start")
    return self

  def __exit__(self, exc_type, exc, tb):
    self._stop.set()

    if self._socket is not None:
      self._socket.close()

    if self._thread is not None:
      self._thread.join(timeout=1.0)

    self.raise_if_failed()

  def raise_if_failed(self) -> None:
    if self._thread_error is not None:
      raise RuntimeError("kernel ISO-TP echo ECU failed") from self._thread_error

  def _run(self) -> None:
    assert self._socket is not None
    self._ready.set()

    while not self._stop.is_set():
      try:
        try:
          msg = self._socket.recv(65535)
        except (TimeoutError, socket.timeout):
          continue

        if not msg:
          continue

        self.rx_count += 1
        self._socket.send(msg)
        self.tx_count += 1
      except Exception as exc:
        if not self._stop.is_set():
          self._thread_error = exc
        return


class GatedIsoTpFlowControlEcu:
  def __init__(self, interface: str, *, tx_arb_id: int, rx_arb_id: int, tx_ext_addr: int | None = None,
               rx_ext_addr: int | None = None, block_size: int = 0, stmin: int = 0):
    self._interface = interface
    self._tx_arb_id = tx_arb_id
    self._rx_arb_id = rx_arb_id
    self._tx_ext_addr = tx_ext_addr
    self._rx_ext_addr = rx_ext_addr
    self._block_size = block_size
    self._stmin = stmin
    self._stop = threading.Event()
    self._ready = threading.Event()
    self._first_frame_seen = threading.Event()
    self._released = threading.Event()
    self._lock = threading.Lock()
    self._thread_error: Exception | None = None
    self._thread: threading.Thread | None = None
    self._socket_panda: SocketPanda | None = None
    self._pending_fc = False
    self.first_frames_seen = 0
    self.first_frame_times_s: list[float] = []

  def __enter__(self):
    self._socket_panda = SocketPanda(self._interface)
    self._thread = threading.Thread(target=self._run, name="isotp-fc-gate", daemon=True)
    self._thread.start()
    if not self._ready.wait(timeout=1.0):
      raise RuntimeError("gated ISO-TP flow control ECU did not start")
    return self

  def __exit__(self, exc_type, exc, tb):
    self._stop.set()

    if self._thread is not None:
      self._thread.join(timeout=1.0)

    with self._lock:
      if self._socket_panda is not None:
        self._socket_panda.close()
        self._socket_panda = None

    self.raise_if_failed()

  def raise_if_failed(self) -> None:
    if self._thread_error is not None:
      raise RuntimeError("gated ISO-TP flow control ECU failed") from self._thread_error

  def wait_for_first_frame(self, timeout_s: float) -> bool:
    return self._first_frame_seen.wait(timeout=timeout_s)

  def wait_for_first_frame_count(self, count: int, timeout_s: float) -> bool:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
      if self.first_frames_seen >= count:
        return True
      self.raise_if_failed()
      time.sleep(0.01)
    return self.first_frames_seen >= count

  def release(self) -> None:
    with self._lock:
      self._released.set()
      if self._pending_fc:
        self._send_flow_control_locked()

  def _run(self) -> None:
    assert self._socket_panda is not None
    self._ready.set()

    while not self._stop.is_set():
      try:
        with self._lock:
          msgs = self._socket_panda.can_recv()
      except Exception as exc:
        if not self._stop.is_set():
          self._thread_error = exc
        return

      if not msgs:
        time.sleep(0.001)
        continue

      try:
        for addr, dat, bus in msgs:
          if bus != 0:
            continue
          if (addr & ~CAN_EFF_FLAG) != self._tx_arb_id:
            continue
          if self._is_first_frame(dat):
            with self._lock:
              self.first_frames_seen += 1
              self.first_frame_times_s.append(time.monotonic())
              self._pending_fc = True
              self._first_frame_seen.set()
              if self._released.is_set():
                self._send_flow_control_locked()
      except Exception as exc:
        if not self._stop.is_set():
          self._thread_error = exc
        return

  def _is_first_frame(self, dat: bytes) -> bool:
    if self._tx_ext_addr is not None:
      if len(dat) < 2 or dat[0] != self._tx_ext_addr:
        return False
      frame_type = dat[1] & 0xF0
    else:
      if len(dat) < 1:
        return False
      frame_type = dat[0] & 0xF0
    return frame_type == 0x10

  def _send_flow_control_locked(self) -> None:
    assert self._socket_panda is not None

    frame = bytearray()
    if self._rx_ext_addr is not None:
      frame.append(self._rx_ext_addr)
    frame.extend((0x30, self._block_size, self._stmin))
    self._socket_panda.can_send(self._rx_arb_id, bytes(frame), 0)
    self._pending_fc = False


def recv_one_isotp_payload(transport: IsoTpTransport, *, timeout_s: float) -> bytes:
  deadline = time.monotonic() + timeout_s

  while time.monotonic() < deadline:
    remaining_ms = max(1, int((deadline - time.monotonic()) * 1000))
    msgs = transport.isotp_recv(timeout=min(remaining_ms, 100))
    if len(msgs) > 1:
      raise AssertionError(f"expected one ISO-TP response, got {len(msgs)}")
    if msgs:
      return msgs[0]

  raise AssertionError("timed out waiting for ISO-TP echo response")


def run_isotp_echo_matrix(transport: IsoTpTransport, interface: str, *, bus: int = 0,
                          before_case: Callable[[], None] | None = None,
                          cases: tuple[IsoTpEchoCase, ...] = ISOTP_ECHO_CASES) -> None:
  for case in cases:
    if before_case is not None:
      before_case()

    transport.can_reset_communications()
    transport.configure_isotp(
      bus,
      case.tx_arb_id,
      case.rx_arb_id,
      tx_extended=case.tx_extended,
      rx_extended=case.rx_extended,
      tx_ext_addr=case.tx_ext_addr,
      rx_ext_addr=case.rx_ext_addr,
      message_timeout_ms=case.message_timeout_ms,
      transfer_timeout_ms=case.transfer_timeout_ms,
    )

    with KernelIsoTpEchoEcu(interface, case) as ecu:
      for payload in case.payloads():
        ecu.raise_if_failed()

        start = time.monotonic()
        transport.isotp_send(payload, timeout=1000)
        try:
          response = recv_one_isotp_payload(transport, timeout_s=case.round_trip_timeout_s)
        except AssertionError as exc:
          raise AssertionError(
            f"{case.name} timed out for {len(payload)}-byte payload; ecu_rx={ecu.rx_count} ecu_tx={ecu.tx_count}"
          ) from exc
        elapsed = time.monotonic() - start

        assert response == payload, f"{case.name} echo mismatch for {len(payload)}-byte payload"

        min_delay_s = case.expected_request_delay_s(len(payload))
        if min_delay_s > 0:
          assert elapsed >= (min_delay_s * 0.75), f"{case.name} STmin was not applied: {elapsed:.3f}s < {min_delay_s:.3f}s"

        ecu.raise_if_failed()
