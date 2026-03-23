import threading
import time

import pytest

from panda.python.socketpanda import CAN_EFF_FLAG, SocketPanda
from panda.python.socketpanda import create_kernel_isotp_socket
from panda.tests.isotp_helpers import ISOTP_ECHO_CASES, IsoTpEchoCase, interface_exists, run_isotp_echo_matrix
from panda.tests.libpanda import libpanda_py
from panda.tests.usbprotocol.test_comms import LibpandaIsoTpTransport, TX_QUEUES, lpp, unpackage_can_msg


def normalize_socketcan_addr(addr: int) -> int:
  return addr & ~CAN_EFF_FLAG


class LibpandaSocketCanBridge:
  def __init__(self, interface: str):
    self._interface = interface
    self._socket_panda = SocketPanda(interface)
    self._lock = threading.Lock()
    self._stop = threading.Event()
    self._thread_error: Exception | None = None
    self._thread: threading.Thread | None = None
    self._start_ns = 0

  def __enter__(self):
    self._start_ns = time.monotonic_ns()
    self._thread = threading.Thread(target=self._run, name="libpanda-socketcan-bridge", daemon=True)
    self._thread.start()
    return self

  def __exit__(self, exc_type, exc, tb):
    self._stop.set()
    if self._thread is not None:
      self._thread.join(timeout=1.0)

    with self._lock:
      self._socket_panda.close()

    self.raise_if_failed()

  def clear(self):
    with self._lock:
      self._socket_panda.can_clear(0)

  def raise_if_failed(self):
    if self._thread_error is not None:
      raise RuntimeError("libpanda socketcan bridge failed") from self._thread_error

  def _run(self):
    pkt = libpanda_py.ffi.new("CANPacket_t *")

    while not self._stop.is_set():
      try:
        now_us = (time.monotonic_ns() - self._start_ns) // 1000
        lpp.isotp_periodic_handler(now_us)

        for q in TX_QUEUES:
          while lpp.can_pop(q, pkt):
            addr, dat, bus = unpackage_can_msg(pkt)
            with self._lock:
              self._socket_panda.can_send(addr, dat, bus)

        with self._lock:
          msgs = self._socket_panda.can_recv()

        for addr, dat, bus in msgs:
          if bus != 0:
            continue
          can_pkt = libpanda_py.make_CANPacket(normalize_socketcan_addr(addr), 0, dat)
          lpp.isotp_rx_hook(can_pkt, now_us)

        time.sleep(0.001)
      except Exception as exc:
        if not self._stop.is_set():
          self._thread_error = exc
        return


@pytest.mark.timeout(45)
@pytest.mark.parametrize("case", ISOTP_ECHO_CASES, ids=lambda case: case.name)
def test_isotp_socketcan_echo(case: IsoTpEchoCase):
  interface = "vcan0"
  if not interface_exists(interface):
    pytest.skip(f"{interface} is not available")

  transport = LibpandaIsoTpTransport()
  with LibpandaSocketCanBridge(interface) as bridge:
    run_isotp_echo_matrix(transport, interface, cases=(case,))
    bridge.raise_if_failed()


@pytest.mark.timeout(10)
@pytest.mark.parametrize("tx_ext_addr,rx_ext_addr", [(0xAA, 0xAA), (0xAA, 0xBB)])
def test_kernel_isotp_extended_addr_socket(tx_ext_addr: int, rx_ext_addr: int):
  interface = "vcan0"
  if not interface_exists(interface):
    pytest.skip(f"{interface} is not available")

  client = create_kernel_isotp_socket(
    interface,
    tx_id=0x5A1,
    rx_id=0x5A9,
    ext_address=tx_ext_addr,
    rx_ext_address=rx_ext_addr,
  )
  server = create_kernel_isotp_socket(
    interface,
    tx_id=0x5A9,
    rx_id=0x5A1,
    ext_address=rx_ext_addr,
    rx_ext_address=tx_ext_addr,
  )

  try:
    payload = b"\x11\x22\x33"
    client.send(payload)
    assert server.recv(4095) == payload
  finally:
    client.close()
    server.close()
