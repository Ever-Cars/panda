#!/usr/bin/env python3
import os
import usb1
import random

from opendbc.car.structs import CarParams
from panda import Panda

def get_test_string():
  return b"test" + os.urandom(10)

if __name__ == "__main__":
  p = Panda()
  p.set_safety_mode(CarParams.SafetyModel.allOutput)

  count = 0
  print("Spamming all buses...")
  while True:
    at = random.randint(1, 2000)
    st = get_test_string()[0:8]
    bus = random.randint(0, 2)
    try:
      p.can_send(at, st, bus)
      count += 1
    except usb1.USBErrorTimeout as e:
      break
  print(f"Sent {count} messages")
