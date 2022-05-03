import struct
import os

dev = os.open("/dev/nr_driver0",os.O_RDWR)

trame = bytearray()

for i in range(0,64):
	trame.append(0x00)

trame[0] = 0x83	
trame[1] = 0x33
trame[2] = 0xE0
trame[3] = 0x12
trame[4] = 0x34
trame[5] = 0x56

trame[52] = 1;	
trame[58] = 0;		
trame[60] = 0x02; 
trame[61] = 0x0f;	     
trame[62] = 0;		

os.write(dev,trame)

os.close(dev)