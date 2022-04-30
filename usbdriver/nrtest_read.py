
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
