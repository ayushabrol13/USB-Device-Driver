/*
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
 * To check the logs from the kernel (adding the _NR_ filter): dmesg | grep _NR_
*/

#include <linux/module.h>	// macro THIS_MODULE
#include <linux/init.h>		
#include <linux/usb.h>		// usb stuff
#include <linux/mutex.h>	// lock_kernel(), unlock_kernel()
#include <linux/slab.h>		// kzalloc()
#include <linux/errno.h>	// pr_err()
#include <asm/uaccess.h>	// copy_to_user(), copy_from_user()

#define VENDOR_ID		0x04d8
#define PRODUCT_ID		0x0070
#define USB_MINOR_BASE	1

// Prevent races between open() and disconnect
static DEFINE_MUTEX(disconnect_mutex);

//------------------------------------------------------------
//             STUCT CORRESPONDING TO THE DEVICE              
//------------------------------------------------------------


struct usb_nr {
	char testbuff[6];
	char lenbuff;
	struct usb_endpoint_descriptor	*int_in_endpoint;		// to record the interrupt in endpoint
	struct usb_endpoint_descriptor	*int_out_endpoint;		// to record the interrupt out endpoint
};

//function to free all the memory allocated
void free_usb_nr(struct usb_nr *dev){
	printk("_NR_ free_usb_nr()\n");
	kfree(dev);
}

static struct usb_driver nr_driver; //needed to be declared here for the nr_open() function

//------------------------------------------------------------
//       IMPLEMENTATION OF THE FILE OPERATION FUNCTIONS       
//------------------------------------------------------------

//open
//Though this is always the first operation performed on the device file, the driver
//	is not required to declare a corresponding method. If this entry is NULL, opening
//	the device always succeeds, but your driver isn't notified.
//
//	The *inode* structure is used by the kernel internally to represent files. Therefore, it is
//		different from the file structure that represents an open file descriptor. There can be
//		numerous file structures representing multiple open descriptors on a single file, but
//		they all point to a single inode structure. The inode structure contains a great deal of
//		information about the file. As a general rule, only two fields of this structure are 
//		of interest for writing driver code:
//
//	The *file* structure represents an open file. (It is not specific to device drivers; every
//		open file in the system has an associated struct file in kernel space.) It is created by
//		the kernel on open and is passed to any function that operates on the file, until the last
//		close. After all instances of the file are closed, the kernel releases the data structure.
static int nr_open(struct inode *inode, struct file *filp){
	struct usb_nr *dev = NULL;			//define a pointer over a device struct (usb_ur)
	int retval = 0;
	int subminor;
	struct usb_interface *interface;	//to get the device interface
	printk("_NR_ nr_open()\n");

	subminor = iminor(inode); 			//obtain the minor number from an inode. Note that traditionnally,
										//	major number corresponds to the driver associated to the device
										//	and the minor number determine wich device is referred to
	// from the minor and the driver it is possible to retrieve the interface
	interface = usb_find_interface(&nr_driver, subminor);

	//we check if it was possible to get the interface back
	if (!interface) {
		pr_err("_NR_%s - error, can't find device for minor %d\n", __func__, subminor);
		retval = -ENODEV;
		goto exit;
	}
	//recover our data pointer from the interface
	dev = usb_get_intfdata(interface);
	if (!dev) {
		retval = -ENODEV;
		goto exit;
	}

	//INITIALISATION OF THE BUFFER FOR THE TESTS
	dev->testbuff[0] = 'H';
	dev->testbuff[1] = 'i';
	dev->testbuff[2] = ' ';
	dev->testbuff[3] = 'N';
	dev->testbuff[4] = 'R';
	dev->testbuff[5] = '\n';
	dev->lenbuff = 6;



	filp->private_data = dev; 			//save our data pointer in the file's private structure
										//	to be able to recover it in the fops functions (read, write...)

	printk("_NR_ data pointer recorded in the open file");
exit:
	return retval;
}

//release
static int nr_release(struct inode *inode, struct file *file){
	printk("_NR_ nr_release()\n");
	return 0;
}

//                       READ FUNCTION                        
//------------------------------------------------------------
//Used to retrieve data from the device. A null pointer in this position causes the read system call to fail with
//	-EINVAL (“Invalid argument”). A nonnegative return value represents the number of bytes successfully read
//	(the return value is a “signed size” type, usually the native integer type for the target platform).
//	filp is the file pointer and count is the size of the requested data transfer. The buff argument points to the
//	user buffer holding the data to be written or the empty buffer where the newly read data should be placed. Finally,
//	ppos is a pointer to a “long offset type” object that indicates the file position the user is accessing
static ssize_t nr_read(struct file *filp, char *buffer, size_t count, loff_t *ppos){
	ssize_t l;
	struct usb_nr *dev = NULL;								//define a pointer over a device struct (usb_ur)
	printk("_NR_ nr_read()\n");

	dev = filp->private_data;								//recover our data pointer from the open file structure (saved inside
															// the open() function )
	l=copy_to_user(buffer, dev->testbuff, dev->lenbuff);	//Copy a block of data into user space. 
															//	Returns number of bytes that could not be copied.
															//	On success, this will be zero. 
															//	copy_to_user(to, from, nbdata);
	printk("_NR_ return of the reading:%d\n", (unsigned int)l);
	return dev->lenbuff-l;	//warning! function "cat" 
}

//The file_operations structure is how a char driver sets up this connection.
//	Each field in the structure must point to the function in the driver that 
//implements a specific operation, or be left NULL for unsupported operations
static const struct file_operations nr_fops = {
	.owner =		THIS_MODULE,	//The first file_operations field is not an operation at all; 
									//	it is a pointer to the module that “owns” the structure.
									//	This field is used to prevent the module from being unloaded
									//	while its operations are in use.
	.open =			nr_open,		//Though this is always the first operation performed on the
									//	device file, the driver is not required to declare a corresponding method. 
	.release =		nr_release, 	//This operation is invoked when the file structure is being released
	.read = 		nr_read,		//size_t (*read) (struct file *, char __user *, size_t, loff_t *);
									//	Used to retrieve data from the device.
};

//This struct usb_class_driver is used to define a number of different parameters
//	that the USB driver wants the USB core to know when registering for a minor number.
static struct usb_class_driver nr_class = {
	.name =			"nr_driver%d",	//The name that sysfs uses to describe the device. 
									//	If the number of the device needs to be in the name,
									//	the characters %d should be in the name string.
	.fops =			&nr_fops,		//Pointer to the struct file_operations that this driver
									//	has defined to use to register as the character device
	.minor_base =	USB_MINOR_BASE,	//This is the start of the assigned minor range for this driver.
									//	All devices associated with this driver are created with unique,
									//	increasin gminor numbers beginning with this value.
};


//------------------------------------------------------------
//    IMPLEMENTATION OF THE USB DRIVER FUNCTIONS/STUCTURES    
//------------------------------------------------------------

//defines the USB device ID for the device as having precisely that vendor and product code
static struct usb_device_id id_table [] = {
	{ USB_DEVICE(VENDOR_ID, PRODUCT_ID) },
	{ },
};
//then adds that information to the exported USB module device table
MODULE_DEVICE_TABLE (usb, id_table);

//                      PROBE FUNCTION                        
//------------------------------------------------------------
//probe function
//	This function is called by the USB core when it thinks it has a struct usb_interface
//	that this driver can handle. A pointer to the struct usb_device_id that the USB core
//	used to make this decision is also passed to this function. If the USB driver claims
//	the struct usb_interface that is passed to it, it should initialize the device properly
//	and return 0. If the driver does not want to claim the device, or an error occurs, 
//	it should return a negative error value.
//		usb_interface : this structure is what the USB core passes to USB drivers
//		and is what the USB driver then is in charge of controlling
static int nr_probe(struct usb_interface *interface, const struct usb_device_id *id){
	int retval= -ENODEV;
	struct usb_nr *dev = NULL;					//define a pointer over a device struct (usb_ur)
	struct usb_endpoint_descriptor *endpoint;
	struct usb_host_interface *iface_desc;		//An array of interface structures containing all of the 
												//	alternate settin gs that maybe selected for this interface.
												//	Each struct usb_host_interface consists of a set of endpoint 
												//	configurations as defined by the struct usb_host_endpoint structure
	int i;
	printk("_NR_ probe()\n");

	//Allocate memory for our device state and initialize it
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);	//kzalloc — allocate memory. The memory is set to zero.
												//	GFP_KERNEL: the type of memory to allocate
	if (!dev) {
		pr_err("_NR_ Out of memory\n");
		goto error;
	}

	iface_desc = interface->cur_altsetting;		//A pointer into the array altsetting, denoting the currently active
												//	setting for this interface.

	// Set up interrupt endpoint information
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		//This block of code first loops over every endpoint that is present in this interface and
		//assigns a local pointer to the endpoint structure to make it easier to access later
		endpoint = &iface_desc->endpoint[i].desc;
		//we want to get the first (and only in our case) interrupt IN endpoint
		if (!dev->int_in_endpoint && usb_endpoint_is_int_in(endpoint)){
			dev->int_in_endpoint = endpoint;
			printk("_NR_ INT IN endpoint found at 0x%x\n", dev->int_in_endpoint->bEndpointAddress);
		}
		//and the first (and only in our case) interrupt OUT endpoint
		if (!dev->int_out_endpoint && usb_endpoint_is_int_out(endpoint)){
			dev->int_out_endpoint = endpoint;
			printk("_NR_ INT OUT endpoint found at 0x%x\n", dev->int_out_endpoint->bEndpointAddress);
		}
	}
	//If we missed the endpoints we display an error message
	if (! dev->int_in_endpoint) {
		pr_err("_NR_ could not find interrupt IN endpoint\n");
		goto error;
	}else if(!dev->int_out_endpoint){
		pr_err("_NR_ could not find interrupt OUT endpoint\n");
		goto error;
	}
	
	//eveything went well, 
	//we save our data pointer in this interface device
	usb_set_intfdata(interface, dev);


	//we register the driver
	retval = usb_register_dev(interface, &nr_class);
	if (retval) {
		// something prevented us from registering this driver
		pr_err("_NR_ Not able to get a minor for this device.\n");
		usb_set_intfdata(interface, NULL);
		goto error;
	}

	return 0; //return 0 indicates we will manage this device

//we use goto in order to be sure to free the memory if an error occurs
error:
	if(dev){
		//we need to free the memory allocated using kzalloc
		free_usb_nr(dev);
	}
	return retval;
}


//disconnect
static void nr_disconnect(struct usb_interface *interface){
	struct usb_nr *dev;
	printk("_NR_ nr_disconnect()\n");

	//prevent open( ) from racing disconnect( )
	mutex_lock(&disconnect_mutex);		// Not interruptible

	//we recover the data recorded in the interface (recorded during the probe() function)
	dev = usb_get_intfdata(interface);

	usb_set_intfdata(interface, NULL); 	//we remove the reference over "dev" in the interface

	if(dev){
		//we need to free the memory allocated using kzalloc
		free_usb_nr(dev);
	}

	//give back the minor
	usb_deregister_dev(interface, &nr_class);

	mutex_unlock(&disconnect_mutex); //release the lock
	printk("_NR_ all cleaned!\n");
}

//The main structure that all USB drivers must create is a struct usb_driver
//	To create a value struct usb_driver structure, only four fields need to be initialized
static struct usb_driver nr_driver = {
	.name =			"nr_driver", 		//Pointer to the name of the driver.
										//	It must be unique amon gall USB drivers in the kernel
										//	and is normally set to the same name as the module name of the driver.
										//	It shows up in sysfs under /sys/bus/usb/drivers/ when the driver is in the kernel.
	.id_table =		id_table,			//Pointer to the struct usb_device_id table that contains a list of all of the
										//	different kinds of USB devices this driver can accept
	.probe =		nr_probe,			//Pointer to the probe function in the USB driver.
	.disconnect =	nr_disconnect,		//Pointer to the disconnect function in the USB driver.
};

//------------------------------------------------------------
//           "CONSTRUCTOR/DESCTUCTOR" OF THE DRIVER           
//------------------------------------------------------------

//To register the struct usb_driver with the USB core, a call to usb_register_driver
//	is made with a pointer to the struct usb_driver. This is traditionally done in the module
//	initialization code for the USB driver:
static int __init usb_nr_init(void){
	int retval = -1;
	printk("_NR_ Constructor of driver - usb_nr_init() -\n");
	printk("_NR_ Registering kernel with driver\n");
	retval = usb_register(&nr_driver);
	if (retval)
		pr_err("_NR_ usb_register failed. Error number %d\n", retval);
	else
		printk("_NR_ Registration is complete\n");
	return retval;
}

//When the USB driver is to be unloaded, the struct usb_driver needs to be unregistered from
//	the kernel. This is done with a call to usb_deregister_driver. When this call happens, any
//	USB interfaces that were currently bound to this driver are disconnected, and the disconnect
//	function is called for them.
static void __exit usb_nr_exit(void){
	printk("_NR_ Destructor of the driver - usb_nr_exit() -\n");
	usb_deregister(&nr_driver);
	printk("_NR_ Unregistration complete\n");
}

//link the init and exit function to the module
module_init(usb_nr_init);
module_exit(usb_nr_exit);

MODULE_AUTHOR("Nicolas Delanoue & Remy Guyonneau\n");
MODULE_LICENSE("GPL");

