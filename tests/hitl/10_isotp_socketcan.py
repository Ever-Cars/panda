import threading
import time

import pytest
from opendbc.car.structs import CarParams

from panda.tests.hitl.helpers import clear_can_buffers
from panda.tests.isotp_helpers import GatedIsoTpFlowControlEcu, build_payload, interface_exists, run_isotp_echo_matrix


@pytest.mark.timeout(90)
def test_isotp_socketcan_echo(p):
  interface = "can0"
  if not interface_exists(interface):
    pytest.skip(f"{interface} is not available")

  p.set_safety_mode(CarParams.SafetyModel.allOutput)
  run_isotp_echo_matrix(p, interface, before_case=lambda: clear_can_buffers(p))


@pytest.mark.timeout(90)
def test_isotp_socketcan_tx_blocks_until_buffer_space_available(p):
  interface = "can0"
  if not interface_exists(interface):
    pytest.skip(f"{interface} is not available")

  tx_arb_id = 0x5D1
  rx_arb_id = 0x5D9
  message_count = 6
  payloads = [build_payload(0xFFF, index * 0x23) for index in range(1, message_count + 1)]
  send_done = threading.Event()
  send_error: Exception | None = None

  def send_payloads() -> None:
    nonlocal send_error
    try:
      p.isotp_send_many(payloads, timeout=15000)
    except Exception as exc:
      send_error = exc
    finally:
      send_done.set()

  clear_can_buffers(p)
  p.can_reset_communications()
  p.set_safety_mode(CarParams.SafetyModel.allOutput)
  p.configure_isotp(
    0,
    tx_arb_id,
    rx_arb_id,
    message_timeout_ms=10000,
    transfer_timeout_ms=10000,
  )

  with GatedIsoTpFlowControlEcu(interface, tx_arb_id=tx_arb_id, rx_arb_id=rx_arb_id) as ecu:
    sender = threading.Thread(target=send_payloads, name="isotp-hitl-stress-send", daemon=True)
    sender.start()

    assert ecu.wait_for_first_frame(timeout_s=2.0), "panda never emitted the first ISO-TP first frame"
    time.sleep(0.5)
    ecu.raise_if_failed()

    assert not send_done.is_set(), "ISO-TP send finished before buffer backpressure could build"
    assert ecu.first_frames_seen == 1, f"expected one gated first frame before release, got {ecu.first_frames_seen}"

    ecu.release()

    assert send_done.wait(timeout=15.0), "ISO-TP send did not unblock after queue space became available"
    sender.join(timeout=0.1)
    ecu.raise_if_failed()

    if send_error is not None:
      raise send_error

    assert ecu.wait_for_first_frame_count(message_count, timeout_s=10.0), (
      f"expected {message_count} first frames after release, saw {ecu.first_frames_seen}"
    )


@pytest.mark.timeout(90)
def test_isotp_socketcan_tx_message_timeout_advances_queue(p):
  interface = "can0"
  if not interface_exists(interface):
    pytest.skip(f"{interface} is not available")

  tx_arb_id = 0x5E1
  rx_arb_id = 0x5E9
  message_timeout_ms = 250
  payloads = [build_payload(64, index * 0x31) for index in range(1, 5)]

  clear_can_buffers(p)
  p.can_reset_communications()
  p.set_safety_mode(CarParams.SafetyModel.allOutput)
  p.configure_isotp(
    0,
    tx_arb_id,
    rx_arb_id,
    message_timeout_ms=message_timeout_ms,
    transfer_timeout_ms=5000,
  )

  with GatedIsoTpFlowControlEcu(interface, tx_arb_id=tx_arb_id, rx_arb_id=rx_arb_id) as ecu:
    p.isotp_send_many(payloads, timeout=1000)

    assert ecu.wait_for_first_frame_count(len(payloads), timeout_s=5.0), (
      f"expected {len(payloads)} timed-out first frames, saw {ecu.first_frames_seen}"
    )
    ecu.raise_if_failed()

    timestamps = ecu.first_frame_times_s
    assert len(timestamps) == len(payloads), f"expected {len(payloads)} first frame timestamps, got {len(timestamps)}"

    deltas_ms = [(later - earlier) * 1000.0 for earlier, later in zip(timestamps, timestamps[1:])]
    min_expected_ms = message_timeout_ms * 0.75
    max_expected_ms = message_timeout_ms * 2.5

    for delta_ms in deltas_ms:
      assert delta_ms >= min_expected_ms, (
        f"next first frame started too early after timeout: {delta_ms:.1f}ms < {min_expected_ms:.1f}ms"
      )
      assert delta_ms <= max_expected_ms, (
        f"next first frame started too late after timeout: {delta_ms:.1f}ms > {max_expected_ms:.1f}ms"
      )
