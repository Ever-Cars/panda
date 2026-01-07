#!/usr/bin/env python3
import time
import re
from panda import Panda
from prettytable import PrettyTable

RED = '\033[91m'
GREEN = '\033[92m'

def make_table(health1, health2, health3):
  table = PrettyTable()
  table.field_names = ["Key", "Bus 0", "Bus 1", "Bus 2"]
  for key1, value1 in health1.items():
    value2 = health2[key1]
    value3 = health3[key1]
    # Normalize float values
    if type(value1) is float:
      value1 = f"{value1:.4f}"
      value2 = f"{value2:.4f}"
      value3 = f"{value3:.4f}"
    table.add_row([key1, colorize_errors(value1), colorize_errors(value2), colorize_errors(value3)])

  table.align["Key"] = "l"
  return table

def colorize_errors(value):
  if isinstance(value, str):
    if re.search(r'(?i)No error', value):
      return f'{GREEN}{value}\033[0m'
    elif re.search(r'(?i)(?<!No error\s)(err|error)', value):
      return f'{RED}{value}\033[0m'
  return str(value)

if __name__ == "__main__":

  panda = Panda()
  while True:
    print(chr(27) + "[2J") # clear screen
    print("Connected to " + ("internal panda" if panda.is_internal() else "External panda") + f" id: {panda.get_serial()[0]}: {panda.get_version()}")
    health1 = panda.can_health(0)
    health2 = panda.can_health(1)
    health3 = panda.can_health(2)
    table = make_table(health1, health2, health3)
    print(table)
    time.sleep(1)
