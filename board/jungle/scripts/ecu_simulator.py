#!/usr/bin/env python3
import os
import random
import argparse
import hexdump
from pandajungle import PandaJungle

DEBUG = False
VIN = 'SADHD2S17N1618222'
ids = {
  0x7e5 : 0x7ea # Jaguar IPace VIN
}

def dumpPacket(addr, data, bus, pre=''):
  if not DEBUG:
    return
  dump = hexdump.hexdump(data, result='return')
  print(f'{pre} ({hex(addr)}, {bus}): {dump[10:]}')

# Packet is a tuple of Address (int), Data (bytearray), Bus (int)
def dumpPackets(p, pre=''):
  if not DEBUG:
    return
  for addr, data, bus in p:
    dumpPacket(addr, data, bus, pre)

def pad_array(barr):
  barr.extend(bytearray(8-len(barr)))
  return barr

def send(dev, addr, data, bus):
  dumpPacket(addr, data, bus, 'TX')
  print()
  dev.can_send(addr, data, bus)

def recv(dev):
  while True:
    packet = dev.can_recv()
    if len(packet) == 0:
      continue
    dumpPackets(packet, 'RX')
    return packet

def wait_for_continue(dev):
  count = 10
  while count > 0:
    count -= 1
    p = recv(dev)
    for addr, data, _ in p:
      if addr in ids and data[0] == 0x30:
        return True
  return False
  
def is_vin_request(packet):
  addr = packet[0]
  data = packet[1]
  size = data[0]

  # Potential OBD2 VIN Request
  if size == 2:
    if data[1] == 0x09 and data[2] == 0x02:
      return bytearray([0x49, 0x02])
  # Potential UDS VIN Request
  elif size == 3:
    if data[1] == 0x22 and data[2] == 0xf1 and data[3] == 0x90:
      return bytearray([0x62, 0xf1, 0x90])
  return None

def send_vin(dev, packet):
  address, _, bus = packet
  resp = is_vin_request(packet)
  if not resp:
    print(f'Not a VIN request: {packet}')
    return False

  total_sent = 0
  total_to_send = len(VIN) + len(resp)
  # Send first vin frame
  d = bytearray(2)
  d[0] = 0x10
  d[1] = total_to_send
  d.extend(resp)

  # Add first frame identifier for OBD2
  if len(d) < 5:
    d.append(0x01)
  
  # Add the VIN
  vin_index = 8 - len(d)
  d.extend(VIN[:vin_index].encode())
  # dev.can_send(ids[address], d, bus)
  send(dev, ids[address], d, bus)
  total_to_send -= vin_index + len(resp)

  if not wait_for_continue(dev):
    print(f'Unable to get continue frame')
    return False
  
  # Send the next frames
  frame_num = 1
  while total_to_send > 0:
    d = bytearray(1)
    d[0] = 0x20 + frame_num
    d.extend(bytearray(VIN[vin_index:vin_index+7].encode()))
    d = pad_array(d)

    # dev.can_send(ids[address], d, bus)
    send(dev, ids[address], d, bus)
    vin_index += 7
    frame_num += 1
    total_to_send -= 7
  
  return True

def wait_for_request(dev):
  print(f'Waiting for VIN request')

  packets = recv(dev)
  for p in packets:
    if p[0] not in ids:
      print(f'{hex(p[0])} is not a known request ID')
      continue
      
    send_vin(dev, p)


if __name__ == "__main__":
  parser = argparse.ArgumentParser(
                    description='Simulate a vehicle ECU to get VIN')
  parser.add_argument('-d', '--debug', action='store_true', help='Print CAN packet exchange information')
  args = parser.parse_args()

  DEBUG = args.debug
  p = PandaJungle()
  p.can_clear(0xFFFF)
  p.set_safety_mode(PandaJungle.SAFETY_ALLOUTPUT)
  wait_for_request(p)


