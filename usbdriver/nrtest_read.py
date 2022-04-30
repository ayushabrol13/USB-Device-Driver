# Author : remy.guyonneau@univ-angers.fr # Date : 12/05/2016 # Version : 0.1 # MaJ : -
# this python programm allows to get one data buffer from the usb device

import struct
import os

# note that the number of the device may change. To check the actual device number:
# ls /dev/
dev = os.open("/dev/nr_driver0",os.O_RDWR)

print("* starting the reading... (do not forget to load the CAN-bus using the LOAD button)")
result = os.read(dev,64)
print("* The first data from the usb device:")
print(' '.join(x.encode('hex') for x in result))

os.close(dev)
