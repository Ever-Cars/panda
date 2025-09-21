#!/usr/bin/env python3
import os
from panda import PandaJungle

if __name__ == "__main__":
  try:
    claim = os.getenv("CLAIM") is not None

    serials = PandaJungle.list()
    if os.getenv("SERIAL"):
      serials = [x for x in serials if x==os.getenv("SERIAL")]

    panda_jungles = [PandaJungle(x, claim=claim) for x in serials]

    if not len(panda_jungles):
      sys.exit("no panda jungles found")

    for pj in panda_jungles:
      pj.reset()

  except Exception as e:
    print(e)
