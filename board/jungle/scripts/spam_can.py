#!/usr/bin/env python3
import os
import usb1
import random
import argparse
from opendbc.car.structs import CarParams
from panda import PandaJungle

MAX_BUS_ERROR_CNT = 10

def get_test_string():
  return b"test" + os.urandom(10)

if __name__ == "__main__":
  parser = argparse.ArgumentParser(description="Read VIN from a vehicle using UDS over CAN.")
  parser.add_argument('-b', '--bus', type=int, default=None,
                      help='CAN bus number to spam (default: 0, 1, 2)')
  args = parser.parse_args()

  buses = [args.bus] if args.bus is not None else [0, 1, 2]

  p = PandaJungle()
  p.set_safety_mode(CarParams.SafetyModel.allOutput)

  print("Spamming all buses...")
  bus0_count = 0
  bus1_count = 0
  bus2_count = 0

  while True:
    at = random.randint(1, 2000)
    st = get_test_string()[0:8]
    # Choose random from buses
    bus = random.choice(buses)
    try:
      p.can_send(at, st, bus)
      if bus == 0:
        bus0_count += 1
      elif bus == 1:
        bus1_count += 1
      elif bus == 2:
        bus2_count += 1
    except usb1.USBErrorTimeout as e:
      pass
    print(f"Message Counts... Bus 0: {bus0_count} Bus 1: {bus1_count} Bus 2: {bus2_count}", end='\r')
  print(f"Message Counts... Bus 0: {bus0_count} Bus 1: {bus1_count} Bus 2: {bus2_count}")
