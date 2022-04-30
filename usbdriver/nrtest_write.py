
import struct
import os

# note that the number of the device may change. To check the actual device number:
# ls /dev/

dev = os.open("/dev/nr_driver0",os.O_RDWR)

trame = bytearray()

# the buffer must be 64 bytes
for i in range(0,64):
	trame.append(0x00)

#CAN message
trame[0] = 0x83		# Most significant bit (bit 7) indicates the presence of a CAN message. Bit 5 indicates an extended (29 bit) identifier. Bit 4 indicates a remote transmission request (RTR). Bits 0-3 contain the data length.
# Byte 1 and bits 5-7 of byte 2 contain the identifier, big endian.
trame[1] = 0x33
trame[2] = 0xE0
#the CAN data
trame[3] = 0x12
trame[4] = 0x34
trame[5] = 0x56

#USB data
trame[52] = 1;		# Number of CAN messages to send.
trame[58] = 0;		# CANCTRL = Normal mode. 
trame[60] = 0x02;	# SPI command = Write.   
trame[61] = 0x0f;	# Register = CANCTRL     
trame[62] = 0;		# Data = 0 (normal mode) 

os.write(dev,trame)

os.close(dev)

# Communication protocol 
#-------------------------
#
# USB Protocol
# 	Bytes 0..51: CAN messages
# 	Byte 52: Number of CAN messages to send. I have only tried sending a single message at a time.
# 	Byte 58: CANCTRL register - see datasheet for details.
# 	Byte 60: SPI command. For incoming messages, this echos back the command that was sent. Commands are 0xC0 (CAN reset), 0x03 (read register), 0x02 (write register), 0x80 (RTS - ?), 0xB0 (RD Status - ?) and 0xD0 (Read Firmware Version).
# 	Byte 61: Register. Set to the register to read or write. The response to a read request contains the same value.
# 	Byte 62: Data. The value to be written to a register for outgoing messages, or retrieved from a register for incoming messages.
#
# CAN Protocol
#	Byte 0: Most significant bit (bit 7) indicates the presence of a CAN message. Bit 5 indicates an extended (29 bit) identifier. Bit 4 indicates a remote transmission request (RTR). Bits 0-3 contain the data length.
#	[Standard ID] Bytes 1-2: Byte 1 and bits 5-7 of byte 2 contain the identifier, big endian.
#	[Extended ID] Bytes 1-4: From most significant to least significant, the message ID is in byte 1, byte 2 (bits 5-7), byte 2 (bits 0-1), byte 3, and byte 4.
#	Next [len] bytes: If this is not an RTR message, the content of the message follows (up to 8 bytes).

