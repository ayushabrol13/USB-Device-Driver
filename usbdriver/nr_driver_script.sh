#!/bin/bash 

MODULE="nr_driver"
USBHID="usbhid"

set -e # this will cause the script to exit on the first error

# If a previous version of the nr_driver in loaded, it is needed to be unloaded
if lsmod | grep "$MODULE" &> /dev/null ; then
	printf "[..]\tremoving previous $MODULE"
	sudo rmmod $MODULE
	printf "\r[OK]\tremoving previous $MODULE\n"
fi

# Compiling the driver
printf "[..]\tcleaning project"
make clean > /dev/null
wait
printf "\r[OK]\tcleaning project\n"
printf "[..]\tcompiling $MODULE.c"
make > /dev/null
printf "\r[OK]\tcompiling $MODULE.c\n"

# Loading the driver in the kernel
printf "[..]\tloading $MODULE"
sudo insmod $MODULE.ko
printf "\r[OK]\tloading $MODULE\n"
printf "[..]\tcleaning project"
make clean > /dev/null
printf "\r[OK]\tcleaning project\n"

# As we do not want usbhid driver to claim our device
# we unload usbhid driver while plugging the device

printf "[..]\tunloading $USBHID"
modprobe -r $USBHID
printf "\r[OK]\tunloading $USBHID\n"

for (( i=10; i>0; i--)); do
	sleep 1 &
	printf "\r["
	if ((i < 10 )); then
		printf " " # to occupy the same space in the display with two and one digit
	fi
	printf "$i] \ttime to plug your MCP2515 demo board device"
	wait
done
printf "\r[xx]  \ttime to plug your MCP2515 demo board device\n"

# and we load the usbhid driver back as it may be used for
# the mouse and keyboard
printf "[..]\tloading $USBHID"
modprobe $USBHID
printf "\r[OK]\tloading $USBHID\n"
