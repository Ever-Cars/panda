#!/usr/bin/env python3
import time
from panda import Panda
from prettytable import PrettyTable

def make_table(health):
  table = PrettyTable()
  table.field_names = ["Key", "Value"]
  for key, value in health.items():
    if type(value) is float:
      value = f"{value:.4f}"
    table.add_row([key, value])
  table.align["Key"] = "l"
  table.align["Value"] = "r"
  return table

if __name__ == "__main__":
  panda = Panda()
  while True:
    i = 0
    st = time.monotonic()
    while time.monotonic() - st < 1:
      panda.health()
      i += 1
    print(make_table(panda.health()))
    print(f"Speed: {i}Hz")