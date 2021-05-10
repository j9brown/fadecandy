#!/usr/bin/env python

# Open Pixel Control client: All lights to solid white

import opc, time

numLEDs = 240
client = opc.Client('localhost:7890')
color = [(1,0,0)]

while True:
  client.put_pixels(color * numLEDs)
