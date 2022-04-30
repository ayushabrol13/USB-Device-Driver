/*
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
 *                      USEFULL COMMANDS                       
 * ------------------------------------------------------------
 * To load the module : sudo insmod usb-minimalistic.ko
 * To unload the module : sudo rmmod usb_minimalistic
 * To check if the module has been loaded: lsmod
 * To check the logs from the kernel: dmesg
 * To check the logs from the kernel (adding the _NR_ filter): dmesg | grep _NR_
 * To test the read/write functions:
 * 		sudo su 							//you need to be root
 * 		cat /dev/nr_driver0 				//to test the read function (warning the number may change!)
 * 		echo "test" > /dev/nr_driver0 		//to test the write function (will be displayed in the logs dmesg)
*/

/*
		Disctinction over the "usb_interface" structure and the "file" structure
		
		                                        
		            [ USER SPACE ]              
		           (/dev/nr_driverX)            
		          ^                             
		 __--struct file--___________________   //functions open, read, write
		/         v                          \  
		|    ___________                     |  
		|   /           \                    |  
		|   | NR_DRIVER |                    |  
		|   \___________/                    |  
		|         ^                          |  
		| struct usb_interface    [ KERNEL ] |  //functions probe, disconnect
		|         v                          |  
		|    __________                      |  
		|   /          \                     |  
		|   | USB CORE |                     |  
		|   \__________/                     |  
		|                                    |  
		\____________________________________/  
		                                        
		             [ HARDWARE ]               
*/

#include <linux/module.h>	// macro THIS_MODULE
#include <linux/init.h>		
#include <linux/usb.h>		// usb stuff
#include <linux/mutex.h>	// lock_kernel(), unlock_kernel()
#include <linux/slab.h>		// kzalloc()
#include <linux/errno.h>	// pr_err()
#include <asm/uaccess.h>	// copy_to_user(), copy_from_user()
#include <linux/wait.h>		// wait_queue_head_t structure

#define VENDOR_ID		0x04d8
#define PRODUCT_ID		0x0070
#define USB_MINOR_BASE	1

// Prevent races between open() and disconnect
static DEFINE_MUTEX(disconnect_mutex);

//------------------------------------------------------------
//             STUCT CORRESPONDING TO THE DEVICE              
//------------------------------------------------------------
struct usb_nr {
	int flag;											//FOR THE TESTS
	struct usb_endpoint_descriptor	*int_in_endpoint;	//to record the interrupt in endpoint, defined in probe()
	struct usb_endpoint_descriptor	*int_out_endpoint;	//to record the interrupt out endpoint, defined in probe()
	struct urb						*int_in_urb;		//the urb to read data with
														//	memory allocated in probe()
														//	initialized in open()
														//	used in read()
	struct usb_device				*usbdev;			//the usb device for this device, used to intialize the urb
														//	A USB device driver commonly has to convert data from a given
														//	struct usb_interface structure into a struct usb_device structure
														//	that the USB core needs for a wide range of function calls. To do
														//	this, the function interface_to_usbdev is provided. Hopefully,
														//	in the future, all USB calls that currently need a struct
														//	usb_device will be converted to take a struct usb_interface
														//	parameter and will not require the drivers to do the conversion.
	unsigned char					*int_in_buffer;		//the buffer to receive data (from the device through the urb in)
	size_t							len_in_buffer;		//number of bytes received in the buffer after the urb in request
	bool							ongoing_read;		//a read is going on (an urb has been sent)
	wait_queue_head_t				int_in_wait;		//to wait for an ongoing read (an urb that has not been processed yet)
};

//function to free all the memory allocated
void free_usb_nr(struct usb_nr *dev){
	printk("_NR_ free_usb_nr()\n");
	usb_free_urb(dev->int_in_urb);
	usb_put_dev(dev->usbdev);				//release a use of the usb device structure (ust_get_dev in probe function)
	kfree(dev->int_in_buffer);
	kfree(dev);
}

static struct usb_driver nr_driver; //needed to be declared here for the nr_open() function

//------------------------------------------------------------
//       IMPLEMENTATION OF THE FILE OPERATION FUNCTIONS       
//------------------------------------------------------------

//                            OPEN                            
//------------------------------------------------------------
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
	filp->private_data = dev; 			//save our data pointer in the file's private structure
										//	to be able to recover it in the fops functions (read, write...)

	dev->flag = 0;						//FOR THE TESTS
	dev->len_in_buffer = 0;				//variable to get the number of bytes of the buffer receiving from the urb request
	dev->ongoing_read = false;			//variable to test if an urb request is going on

	printk("_NR_ data pointer recorded in the open file\n");
exit:
	return retval;
}

//release
static int nr_release(struct inode *inode, struct file *file){
	printk("_NR_ nr_release()\n");
	return 0;
}


//The completion handler function that is called by the USB core when
//	the urb is completely transferred or when an error occurs to the urb. Within this
//	function, the USB driver may inspect the urb, free it, or resubmit it for another
//	transfer. (See the section “Completing Urbs: The Completion Callback Handler”
//	for more details about the completion handler.)
static void nr_read_int_callback(struct urb *urb)
{
	struct usb_nr *dev;
	printk("_NR_ nr_read_int_callback()\n");

	dev = urb->context;

	// sync/async unlink faults aren't errors 
	if (urb->status) {
		if (!(urb->status == -ENOENT ||
			urb->status == -ECONNRESET ||
			urb->status == -ESHUTDOWN))
			pr_err("_NR_ nonzero write interuption status received: %d\n", urb->status);
	}
	dev->len_in_buffer = urb->actual_length;
	printk("_NR_ actual len:%d\n",(int) dev->len_in_buffer);
	dev->ongoing_read = false;

	wake_up_interruptible(&dev->int_in_wait);		//wait the queue sleeping in the read function
}

//                           READ                             
//------------------------------------------------------------
//Used to retrieve data from the device. A null pointer in this position causes the read system call to fail with
//	-EINVAL (“Invalid argument”). A nonnegative return value represents the number of bytes successfully read
//	(the return value is a “signed size” type, usually the native integer type for the target platform).
//	filp is the file pointer and count is the size of the requested data transfer. The buff argument points to the
//	user buffer holding the data to be written or the empty buffer where the newly read data should be placed. Finally,
//	ppos is a pointer to a “long offset type” object that indicates the file position the user is accessing
//
//	The return value for read is interpreted by the calling application program:
//		If the value equals the count argument passed to the read system call, the requested number of bytes has been
//			transferred. This is the optimal case.
//		If the value is positive, but smaller than count , only part of the data has been transferred. This may happen
//			for a number of reasons, depending on the device. Most often, the application program retries the read.
//			For instance, if you read using the fread function, the library function reissues the system call until
//			completion of the requested data transfer.
//		If the value is 0 , end-of-file was reached (and no data was read).
//		A negative value means there was an error. The value specifies what the error was, according to <linux/errno.h>.
//			Typical values returned on error include -EINTR (interrupted system call) or -EFAULT (bad address).
static ssize_t nr_read(struct file *filp, char *buffer, size_t count, loff_t *ppos){
	ssize_t rs = 0;											//to return the number of readed bytes
	struct usb_nr *dev = NULL;								//define a pointer over a device struct (usb_ur)
	printk("_NR_ nr_read()\n");

	dev = filp->private_data;								//recover our data pointer from the open file structure (saved inside
															// the open() function )

	//if(dev->flag == 1) return 0;							//FOR THE TEST (TO DO THE READ ONLY ONCE)

retry:
	//first we need to test if an urb hasn't already been submit (and not finished yet)
	if(dev->ongoing_read){														//An urb has already been sent,
																				//	we have to (legend) wait for it (dary)
		printk("_NR_ \t dev->ongoing_read = true, wainting...\n");
		
		rs = wait_event_interruptible(dev->int_in_wait, (!dev->ongoing_read));	//The process is put to sleep until the condition
																				//	evaluates to true or a signal is received.
																				//	The condition is checked each time the waitqueue
																				//	is woken up. wake_up() has to be called after
																				//	changing any variable that could change the 
																				//	result of the wait condition
		printk("_NR_ \t wake up!!!\n");
		if (rs < 0){
			printk("_NR_ \t rs=%d, error while waiting\n", (int)rs);
			goto exit;
		}

		dev->flag = 1;										//FOR THE TEST (TO DO THE READ ONLY ONCE)
	}

	//if we are here, it means no urb is being processed. We have to check if one has been (if we have read some data)
	if(dev->len_in_buffer > 0){
		rs = copy_to_user(buffer, dev->int_in_buffer, dev->len_in_buffer);	//Copy a block of data into user space. 
																				//	Returns number of bytes that could not be
		printk("_NR_ return of the reading:%d\n", (unsigned int)rs);
		*ppos += dev->len_in_buffer-rs;						//Whatever the amount of data the method transfers, it should generally
															//	update the file position at *ppos to represent the current file position
															//	after successful completion of the system call. The kernel then
															//	propagates the file position change back into the file structure when
															//	appropriate.
		rs = dev->len_in_buffer-rs;							//See the return value in the comments at the beginning of the function
		printk("_NR_ \t dev->len_in_buffer = %d\n", (int)dev->len_in_buffer);
		dev->len_in_buffer = 0;
		goto exit;
	}

	//if we are here, it means no urb is being processed and no data has been read, we need to send a new urb
	//The function usb_fill_int_urb is a helper function to properly initialize a urb to be
	//	sent to an interrupt endpoint of a USB device:
	printk("_NR_ \t preparing the urb\n");
	usb_fill_int_urb(
			dev->int_in_urb,									//urb* : A pointer to the urb to be initialized.
			dev->usbdev,										//usb_device* : The USB device to which this urb is to be sent.
			usb_rcvintpipe(dev->usbdev,							//unsigned int : The specific endpoint of the USB device to which this
					dev->int_in_endpoint->bEndpointAddress),	//	urb is to be sent. This value is created with the previously
																//	mentioned usb_sndintpipe or usb_rcvintpipe functions.
																//		usb_rcvintpipe() specifies an interrupt IN endpoint for the
																//		specified USB device with the specified endpoint number.
			dev->int_in_buffer,									//A pointer to the buffer from which outgoing data is taken or
																//	into which incoming data is received. Note that this can not 
																//	be a static buffer and must be created with a call to kmalloc.
			dev->int_in_endpoint->wMaxPacketSize,				//The length of the buffer pointed to by the transfer_buffer pointer.
			nr_read_int_callback,								//usb_complete_t : Pointer to the completion handler that is called
																//	when this urb is completed.
			dev,												//void * : Pointer to the blob that is added to the urb structure
																//	for later retrieval by the completion handler function.
			dev->int_in_endpoint->bInterval);					//int : The interval at which that this urb should be scheduled.

	
	printk("_NR_ \t sending the urb\n");
	rs = usb_submit_urb(dev->int_in_urb, GFP_KERNEL);
	if (rs < 0) {
		pr_err("_NR_ failed submitting read urb, error %d\n", (int) rs);
		goto exit;
	}
	dev->ongoing_read = true;								//an urb request has been submitted and is not over yet

	dev->flag = 1;											//FOR THE TEST (TO DO THE READ ONLY ONCE)

	printk("_NR_ \t going to retry\n");
	goto retry;												//we need to wait for the urb to be processed

exit:
	return rs;
}

//                            WRITE                           
//------------------------------------------------------------
//write, like read, can transfer less data than was requested, according to the following
//	rules for the return value:
//		If the value equals count , the requested number of bytes has been transferred.
//		If the value is positive, but smaller than count , only part of the data has been transferred.
//			The program will most likely retry writing the rest of the data.
//		If the value is 0 , nothing was written. This result is not an error, and there is no
//			reason to return an error code. Once again, the standard library retries the call to write.
//		A negative value means an error occurred; as for read, valid error values are those defined in <linux/errno.h
static ssize_t nr_write(struct file *filp, const char *buffer, size_t count, loff_t *ppos){
	//ssize_t rs = 0;											//to return the number of written bytes
	//struct usb_nr *dev = NULL;								//define a pointer over a device struct (usb_ur)
	int i;														//for the for loop

	printk("_NR_ nr_write()\n");

	printk("_NR_ data written:");
	for(i=0; i<count; i++){
		printk("%c", buffer[i]);
	}printk("\n");

	return count;
}

//The file_operations structure is how a char driver sets up this connection.
//	Each field in the structure must point to the function in the driver that 
//	implements a specific operation, or be left NULL for unsupported operations
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
	.write = 		nr_write,		//size_t (*write) (struct file *, char __user *, size_t, loff_t *);
									//	Used to send data to the device.
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

//                           PROBE                            
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
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);	//Allocate memory for our device state and initialize it
												//	kzalloc — allocate memory. The memory is set to zero.
												//	GFP_KERNEL: the type of memory to allocate
	if (!dev) {
		pr_err("_NR_ Out of memory\n");
		goto error;
	}
	//get the usb_device struct from the interface
	//the using of usb_get_dev(): https://www.kernel.org/doc/htmldocs/usb/API-usb-get-dev.html
	//	"Drivers for USB interfaces should normally record such references in their probe methods,
	//	when they bind to an interface, and release them by calling usb_put_dev, in their disconnect methods."
	dev->usbdev = usb_get_dev(interface_to_usbdev(interface));

	//initialization of the wait_queue_head_t need to wait for the urb to be processed (read() function)
	//	There are several ways of handling sleeping and waking up in Linux, each suited to different needs.
	//	All, however, work with the same basic data type, a wait queue (wait_queue_head_t). A wait queue is 
	//	a queue of processes that are waiting for an event. init_waitqueue_head initializes a wait_queue_head_t 
	init_waitqueue_head(&dev->int_in_wait);

	// Set up interrupt endpoint information
	iface_desc = interface->cur_altsetting;		//A pointer into the array altsetting, denoting the currently active
												//	setting for this interface.
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {	//This block of code first loops over every endpoint that is 
															//	present in this interface and assigns a local pointer
															//	to the endpoint structure to make it easier to access later
		endpoint = &iface_desc->endpoint[i].desc;
		//we want to get the first (and only in our case) interrupt IN endpoint
		if (!dev->int_in_endpoint && usb_endpoint_is_int_in(endpoint)){
			dev->int_in_endpoint = endpoint;							//we found the endpoint
			dev->int_in_buffer = kmalloc(endpoint->wMaxPacketSize,		//we prepare our in buffer to be able to handle the data
										 GFP_KERNEL);					//	exchanged througt this endpoint
																		//		endpoint->wMaxPacketSize: the maximal data
																		//		exchanged from this endpoint
			if (!dev->int_in_buffer) {
				pr_err("_NR_ Could not allocate int_in_buffer\n");
				goto error;
			}
			printk("_NR_ INT IN endpoint found at 0x%x\n", dev->int_in_endpoint->bEndpointAddress);
		}
		//and the first (and only in our case) interrupt OUT endpoint
		if (!dev->int_out_endpoint && usb_endpoint_is_int_out(endpoint)){
			dev->int_out_endpoint = endpoint;
			printk("_NR_ INT OUT endpoint found at 0x%x\n", dev->int_out_endpoint->bEndpointAddress);
		}
	}
	if (! dev->int_in_endpoint) {							//If we missed the endpoints we display an error message
		pr_err("_NR_ could not find interrupt IN endpoint\n");
		goto error;
	}else if(!dev->int_out_endpoint){
		pr_err("_NR_ could not find interrupt OUT endpoint\n");
		goto error;
	}
	dev->int_in_urb = usb_alloc_urb(0, GFP_KERNEL);			//intialization of the urb for the usb reading
															//The first parameter (iso_packets) is the number of isochronous
															//	packets this urb should contain. If you do not want to create
															//	an isochronous urb, this variable should be set to 0 .
	if (!dev->int_in_urb) {
		pr_err("_NR_ Could not allocate int_in_urb\n");
		goto error;
	}
	usb_set_intfdata(interface, dev);						//eveything went well,
															//	we save our data pointer in this interface device
	retval = usb_register_dev(interface, &nr_class);		//we register the driver
	if (retval) {
		pr_err("_NR_ Not able to get a minor for this device.\n");	// something prevented us from registering this driver
		usb_set_intfdata(interface, NULL);
		goto error;
	}
	return 0;												//return 0 indicates we will manage this device

error:														//we use goto in order to be sure to free
															//	the memory if an error occurs
	if(dev){
		free_usb_nr(dev);									//we need to free the memory allocated using kzalloc
	}
	return retval;
}

//                        DISCONNECT                          
//------------------------------------------------------------
static void nr_disconnect(struct usb_interface *interface){
	struct usb_nr *dev;
	printk("_NR_ nr_disconnect()\n");

											
	mutex_lock(&disconnect_mutex);				//prevent open() from racing disconnect(): not interruptible
	dev = usb_get_intfdata(interface);			//we recover the data recorded in the interface
												//	(recorded during the probe() function)
	usb_set_intfdata(interface, NULL); 			//we remove the reference over "dev" in the interface
	if(dev){
		free_usb_nr(dev);						//we need to free the memory allocated to the struct usb_nr
	}
	usb_deregister_dev(interface, &nr_class);	//give back the minor
												//	(to avoid the incrementation of the file-node- in /dev/)
	mutex_unlock(&disconnect_mutex); 			//release the lock
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

MODULE_AUTHOR("Ayush Abrol\n");
MODULE_LICENSE("GPL");

