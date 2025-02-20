#!/usr/bin/env python3
import os
import random
from pandajungle import PandaJungle

VIN = 'SADHD2S17N1618222'
ids = {
  0x7e5 : 0x7ea # Jaguar IPace VIN
}

def pad_array(barr):
  barr.extend(bytearray(8-len(barr)))
  return barr

def recv(dev):
  while True:
    packet = dev.can_recv()
    if len(packet) == 0:
      continue
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
  total_to_send = 20
  # Send first vin frame
  d = bytearray(2)
  d[0] = 0x10
  d[1] = total_to_send
  d.extend(resp)

  # Add first frame identifier for OBD2
  if len(d) < 5:
    d.append(0x01)
  
  # Add the VIN
  total_sent = 8 - len(d)
  d.extend(VIN[:total_sent].encode())
  print(f'Sending {d} with len {len(d)} on bus {bus}')
  dev.can_send(ids[address], d, bus)
  total_to_send -= total_sent

  if not wait_for_continue(dev):
    print(f'Unable to get continue frame')
    return False
  
  # Send the next frames
  frame_num = 1
  while total_to_send > 0:
    d = bytearray(1)
    d[0] = 0x20 + frame_num
    d.extend(bytearray(VIN[total_sent:total_sent+7].encode()))
    d = pad_array(d)

    print(f'Sending {d} with len {len(d)} on bus {bus}')
    dev.can_send(ids[address], d, bus)
    total_sent += 7
    frame_num += 1
    total_to_send -= 8
  
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
  p = PandaJungle()
  p.set_safety_mode(PandaJungle.SAFETY_ALLOUTPUT)
  wait_for_request(p)


