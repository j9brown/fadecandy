#!/usr/bin/env python

# Open Pixel Control client: All lights to solid white

import opc, time

numLEDs = 512
client = opc.Client('localhost:7890')

bright = 2
a = [ (0, 0, 0) ] * numLEDs
b = [ (bright, 4*bright, bright) ] * numLEDs
c = [ (3*bright, 0, 0) ] * numLEDs
delay = 3

while True:
  client.put_pixels(a)
  time.sleep(delay)
  client.put_pixels(b)
  time.sleep(delay)
  client.put_pixels(c)
  time.sleep(delay)
