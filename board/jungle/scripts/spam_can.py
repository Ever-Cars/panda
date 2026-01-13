#!/usr/bin/env python3
import os
import time
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
  parser.add_argument('-b', '--bus', type=int, action='append',
                      help='CAN bus number to spam (can be specified multiple times, e.g., -b 0 -b 1 -b 2)')
  parser.add_argument('-p', '--packets', type=int, default=None,
                      help='Number of packets to send (default: infinite)')
  args = parser.parse_args()

  buses = args.bus if args.bus is not None else [0, 1, 2]
  packets = args.packets

  p = PandaJungle()
  p.set_safety_mode(CarParams.SafetyModel.allOutput)

  print(f"Spamming buses: {buses}")
  bus0_count = 0
  bus1_count = 0
  bus2_count = 0
  count = 0

  while packets is None or count < packets:
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
      count += 1
    except usb1.USBErrorTimeout as e:
      pass
    except KeyboardInterrupt:
      break
    print(f"Message Counts... Bus 0: {bus0_count} Bus 1: {bus1_count} Bus 2: {bus2_count} | Total: {count}", end='\r')

  print("Clearing CAN buffers...")
  time.sleep(1)
  for bus in range(3):
    p.can_clear(bus)
  print(f"Message Counts... Bus 0: {bus0_count} Bus 1: {bus1_count} Bus 2: {bus2_count} | Total: {count}")
