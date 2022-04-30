/*
 * The minimalist code is mostly from SolidusCode "Linux Kernel Module
 * Programming - USB Device Driver 02" the video can be found here
 * https://www.youtube.com/watch?v=5IDL070RtoQ (09/05/2016)
 *
 * The comment informations are from
 * http://www.oreilly.com/openbook/linuxdrive3/book (09/05/2016)
 * Here is the using Makefile to compile the driver:
 *
 * ------------------------------------------------------------
 *               MAKEFILE TO COMPILE THE DRIVER
 * ------------------------------------------------------------
		DEBUG = y

		ifeq ($(DEBUG),y)
			DBGFLAGS = -O -g -DML_DEBUG
		else
			DBGFLAGS = -O2
		endif

		ccflags-y += $(DBGFLAGS)

		ifneq ($(KERNELRELEASE),)
			obj-m := usb-minimalist.o

		else
			KERNELDIR ?= /lib/modules/$(shell uname -r)/build
			PWD := $(shell pwd)

		default:
			$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

		clean:
			$(MAKE) -C $(KERNELDIR) M=$(PWD) clean

		endif
 * ------------------------------------------------------------
 *
 * The command to load the load the module : sudo insmod usb-minimalistic.ko
 * To check if the module has been loaded: lsmod
 * To check the logs from the kernel: dmesg
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/errno.h> // pr_err()

#define VENDOR_ID 0x04d8
#define PRODUCT_ID 0x0070
#define USB_MINOR_BASE 1

//------------------------------------------------------------
//       IMPLEMENTATION OF THE FILE OPERATION FUNCTIONS
//------------------------------------------------------------

// open
static int nr_open(struct inode *inode, struct file *file)
{
	int retval = 0;
	printk("_NR_ open function\n");
	return retval;
}

// release
static int nr_release(struct inode *inode, struct file *file)
{
	printk("_NR_ release function\n");
	return 0;
}

// The file_operations structure is how a char driver sets up this connection.
// Each field in the structure must point to the function in the driver that
// implements a specific operation, or be left NULL for unsupported operations
static const struct file_operations nr_fops = {
	.owner = THIS_MODULE, // The first file_operations field is not an operation at all;
						  // it is a pointer to the module that “owns” the structure.
						  // This field is used to prevent the module from being unloaded
						  // while its operations are in use.
	.open = nr_open, // Though this is always the first operation performed on the
					 //	device file, the driver is not required to declare a corresponding method.
	.release = nr_release, // This operation is invoked when the file structure is being released
};

// This struct usb_class_driver is used to define a number of different parameters
// that the USB driver wants the USB core to know when registering for a minor number.
static struct usb_class_driver nr_class = {
	.name = "nr%d", // The name that sysfs uses to describe the device.
					//	If the number of the device needs to be in the name,
					//	the characters %d should be in the name string.
	.fops = &nr_fops, // Pointer to the struct file_operations that this driver
					  // has defined to use to register as the character device
	.minor_base = USB_MINOR_BASE, // This is the start of the assigned minor range for this driver.
								  //	All devices associated with this driver are created with unique,
								  //	increasin gminor numbers beginning with this value.
};

//------------------------------------------------------------
//    IMPLEMENTATION OF THE USB DRIVER FUNCTIONS/STUCTURES
//------------------------------------------------------------

// defines the USB device ID for the device as having precisely that vendor and product code
static struct usb_device_id id_table[] = {
	{USB_DEVICE(VENDOR_ID, PRODUCT_ID)},
	{},
};
// then adds that information to the exported USB module device table
MODULE_DEVICE_TABLE(usb, id_table);

// probe function
// This function is called by the USB core when it thinks it has a struct usb_interface
// that this driver can handle. A pointer to the struct usb_device_id that the USB core
// used to make this decision is also passed to this function. If the USB driver claims
// the struct usb_interface that is passed to it, it should initialize the device properly
// and return 0. If the driver does not want to claim the device, or an error occurs,
// it should return a negative error value.
static int nr_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	int retval;
	printk("_NR_ usb device plugged\n");
	retval = usb_register_dev(interface, &nr_class);
	if (retval)
	{
		// something prevented us from registering this driver
		pr_err("_NR_ Not able to get a minor for this device.\n");
		usb_set_intfdata(interface, NULL);
		return retval;
	}
	return 0; // return 0 indicates we will manage this device
}

// disconnect
static void nr_disconnect(struct usb_interface *interface)
{
	printk("_NR_ usb device has been removed\n");
}

// The main structure that all USB drivers must create is a struct usb_driver
// To create a value struct usb_driver structure, only four fields need to be initialized
static struct usb_driver nr_driver = {
	.name = "nr_driver", // Pointer to the name of the driver.
						 //	It must be unique amon gall USB drivers in the kernel
						 //	and is normally set to the same name as the module name of the driver.
						 //	It shows up in sysfs under /sys/bus/usb/drivers/ when the driver is in the kernel.
	.id_table = id_table, // Pointer to the struct usb_device_id table that contains a list of all of the
						  //	different kinds of USB devices this driver can accept
	.probe = nr_probe,			 // Pointer to the probe function in the USB driver.
	.disconnect = nr_disconnect, // Pointer to the disconnect function in the USB driver.
};

//------------------------------------------------------------
//           "CONSTRUCTOR/DESCTUCTOR" OF THE DRIVER
//------------------------------------------------------------

// To register the struct usb_driver with the USB core, a call to usb_register_driver
// is made with a pointer to the struct usb_driver. This is traditionally done in the module
// initialization code for the USB driver:
static int __init usb_nr_init(void)
{
	int retval = -1;
	printk("_NR_ Constructor of driver\n");
	printk("_NR_ Registering kernel with driver\n");
	retval = usb_register(&nr_driver);
	if (retval)
		pr_err("_NR_ usb_register failed. Error number %d\n", retval);
	else
		printk("_NR_ Registration is complete\n");
	return retval;
}

// When the USB driver is to be unloaded, the struct usb_driver needs to be unregistered from
// the kernel. This is done with a call to usb_deregister_driver. When this call happens, any
// USB interfaces that were currently bound to this driver are disconnected, and the disconnect
// function is called for them.
static void __exit usb_nr_exit(void)
{
	printk("_NR_ Destructor of the driver\n");
	usb_deregister(&nr_driver);
	printk("_NR_ Unregistration complete\n");
}

// link the init and exit function to the module
module_init(usb_nr_init);
module_exit(usb_nr_exit);

MODULE_AUTHOR("Nicolas Delanoue & Remy Guyonneau\n");
MODULE_LICENSE("GPL");
