#!/usr/bin/env python

# Light each LED in sequence, and repeat.

import opc, time, sys

numLEDs = 240
client = opc.Client('localhost:7890')
delay = float(sys.argv[1])
color = (40, 40, 40)

while True:
	for i in range(numLEDs):
		pixels = [ (0,0,0) ] * numLEDs
		pixels[i] = color
		client.put_pixels(pixels)
		time.sleep(delay)
