#!/usr/bin/env python3
import os
import random
import argparse
import hexdump
from panda import PandaJungle
from opendbc.car.structs import CarParams

DEBUG = False
VIN = ''
req_id = 0
resp_id = 0

vin_map = [
  # request ID, response ID, VIN
  [ 0x7df, 0x7e8, "SADHC2S16K1F69225" ],
  [ 0x7e0, 0x7e8, "WVWKR7AU0KW910028" ],
  [ 0x7e0, 0x7e8, "1G1FY6S05L4108591" ],
  [ 0x7e5, 0x7ed, "WBY8P2C05M7K00499" ],
  [ 0x726, 0x72e, "3FMTK2R7XMMA30186" ],
  [ 0x7df, 0x7e8, "5YJSA1E52NF471625" ],
  [ 0x7e0, 0x7e8, "1G1FY6S05N4114569" ],
  [ 0x7e5, 0x7ed, "SADHD2S17N1618222" ],
  [ 0x7e4, 0x7ea, "KM8KRDAF3PU182686" ],
  [ 0x7e5, 0x7ed, "W1N9M1DBXPN046826" ],
  [ 0x7e2, 0x7ea, "KM8KRDDF2RU277223" ],
  [ 0x7df, 0x7ea, "KNDCR3L16P5045191" ],
  [ 0x7df, 0x7ea, "JTMAAAAA6RA033039" ],
  # [ 0x1dd01a01, 0x1f402e80, "YV4ED3URXM2565349" ],
  # [ 0x17fc0076, 0x17fe0076, "WVGTMPE26MP062726" ],
  # [ 0x1dd01a01, 0x1f402e80, "LPSED3KA0NL055435" ],
]

def dumpPacket(addr, data, bus, pre=''):
  dump = hexdump.hexdump(data, result='return')
  print(f'{pre:<8} {hex(addr):<10} | {bus:<1}: {dump[10:]}')

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

def wait_for_continue(dev, kind):
  """Wait for a continue frame after sending first VIN frame."""
  count = 10

  while count > 0:
    count -= 1
    p = recv(dev)
    for addr, data, _ in p:
      if addr == req_id and data[0] == 0x30:
        return True
  return False
  
def is_vin_request(packet):
  """Return ('obd'|'uds', response_prefix_bytes) if VIN request, else None."""
  addr = packet[0]
  data = packet[1]
  size = data[0]

  if addr != req_id:
    return (None, None)

  # Potential OBD2 VIN Request
  if size == 2:
    if data[1] == 0x09 and data[2] == 0x02:
      return ('obd', bytearray([0x49, 0x02]))
  # Potential UDS VIN Request
  elif size == 3:
    if data[1] == 0x22 and data[2] == 0xf1 and data[3] == 0x90:
      return ('uds', bytearray([0x62, 0xf1, 0x90]))

  return (None, None)

def send_vin(dev, packet):
  address, _, bus = packet
  kind, resp = is_vin_request(packet)
  if not resp:
    print(f'Not a VIN request: {packet}')
    return False

  dumpPacket(packet[0], packet[1], packet[2], 'VIN REQ')
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

  send(dev, resp_id, d, bus)
  dumpPacket(resp_id, d, bus, 'VIN RESP')
  total_to_send -= vin_index + len(resp)

  if not wait_for_continue(dev, kind):
    print(f'Unable to get continue frame')
    return False
  
  print(f'Sending remaining {total_to_send} VIN bytes')
  # Send the next frames
  frame_num = 1
  while total_to_send > 0:
    d = bytearray(1)
    d[0] = 0x20 + frame_num
    d.extend(bytearray(VIN[vin_index:vin_index+7].encode()))
    d = pad_array(d)

    send(dev, resp_id, d, bus)
    dumpPacket(resp_id, d, bus, 'VIN RESP')
    vin_index += 7
    frame_num += 1
    total_to_send -= 7
  
  return True

def wait_for_request(dev):
  print(f'Waiting for VIN request')

  packets = recv(dev)
  for p in packets:
    if send_vin(dev, p):
      return True
      
  return False


if __name__ == "__main__":
  parser = argparse.ArgumentParser(
                    description='Simulate a vehicle ECU to get VIN')
  parser.add_argument('-d', '--debug', action='store_true', help='Print CAN packet exchange information')
  args = parser.parse_args()

  DEBUG = args.debug
  r = random.choice(vin_map)
  req_id = r[0]
  resp_id = r[1]
  VIN = r[2]
  print(f'Simulating ECU with request ID {hex(req_id)}, response ID {hex(resp_id)}, VIN {VIN}')

  p = PandaJungle()
  p.set_safety_mode(CarParams.SafetyModel.elm327)

  while True:
    if wait_for_request(p):
      r = random.choice(vin_map)
      req_id = r[0]
      resp_id = r[1]
      VIN = r[2]
      print(f'Simulating ECU with request ID {hex(req_id)}, response ID {hex(resp_id)}, VIN {VIN}')
      p.can_clear(0xFFFF)
