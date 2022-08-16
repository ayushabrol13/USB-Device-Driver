# USB driver (GNU/Linux)

The idea was to develop a Linux Usb Device Driver.

Some useful resources we used:

We present different versions of the driver we made from the most basic and useless (but understanble) one to the final one (NR Driver).Note that this driver, as we wanted it to be as "simple" as possible, does not manage a lot of exceptions and possible errors! Be careful if you want to use it.
We only tested it in simple cases (only one device plugged in, only one program trying to communicate at the same time)

Here is a list of the presented files: 

- NR Driver
    - nr_driver.c
    - makefile
    - nr_driver_script.sh
    - nrtest_read.py
    - nrtest_write.py

- Step by step
    - Minimalist driver
    - Finding endpoints
    - Read function
    - Read/write functions
    - Read using urbs


## NR driver

This corresponds to the source code of our driver for the device.

- ***nr_driver.c***: the c code of the driver.

- ***Makefile***: the makefile to compile the driver.

- ***nr_driver_script.sh***: A script to compile and load the driver into the kernel. Note that the usbhid driver used to claim our device before our own driver. To avoid this, the script unloads the usbhid driver giving us time to plug our device and then reload the usbhid driver (as in our case it is needed for the mouse and the keyboard...).This is the only solution we have so far.

- ***nrtest_read.py***: A python example program using the driver to read values from the device. Note that this python program needs to be executed as root (using sudo for instance) in order to open the corresponding /dev file (created by the driver while plugging the device).

- ***nrtest_write.py***: A python example program using the driver to write values to the device. Note that this python program needs to be executed as root (using sudo for instance) in order to open the corresponding /dev file (created by the driver while pluggin the device). Using an oscilloscope it is possible to see the sent CAN data over the CAN bus. 


## Step by step

Here are the steps we took building our driver:

- ***driver_minimalist.c***: This is a minimalist driver that does nothing (no read neither write functions to communicate with the device) But it is a good starting point to write a more useful driver.

- ***driver_endpoints.c***: This driver automatically searches the endpoints (in and out) of the device.

- ***driver_read.c***: This driver allows the read function. Note that it does not communicate with the device yet! This is just to test the communication between the user space and the kernel!

- ***driver_read_write.c***: This driver allows the read and write functions. Note that it does not communicate with the device yet! This is just to test the communication between the user space and the kernel!

- ***driver_read_urb.c***: Finally this driver communicates with the device througt URBs (USB Data Block). We are now able to communicate from the user space to the kernel AND from the kernel to the usb device. For the urb writing function, you can check our final driver.

## How to compile and load the driver

* Use make command to compile the makefile provided for the nr_driver.c file and build all dependencies and modules to compile the main driver code.

            make

* To load the module : 
            
            sudo insmod nr_driver.ko

* To unload the module : 

            sudo rmmod nr_driver

* To check if the module has been loaded: 
            
            lsmod

* To check the logs from the kernel: 

            dmesg

* To check the logs from the kernel (adding the NR filter): 
            
            dmesg | grep _NR_
