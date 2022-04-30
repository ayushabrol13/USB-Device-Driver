/*


 * Here is the using Makefile to compile the driver:
 * ------------------------------------------------------------
 *               MAKEFILE TO COMPILE THE DRIVER
 * ------------------------------------------------------------
        obj-m := nr_driver.o

        KERNELDIR ?= /lib/modules/$(shell uname -r)/build
        PWD := $(shell pwd)

        default:
            $(MAKE) -C $(KERNELDIR) M=$(PWD) modules

        clean:
            $(MAKE) -C $(KERNELDIR) M=$(PWD) clean



 * ------------------------------------------------------------
 *                      USEFUL COMMANDS
 * ------------------------------------------------------------
 * To load the module : sudo insmod nr_driver.ko
 * To unload the module : sudo rmmod nr_driver
 * To check if the module has been loaded: lsmod
 * To check the logs from the kernel: dmesg
 * To check the logs from the kernel (adding the _NR_ filter): dmesg | grep _NR_
 * ------------------------------------------------------------
    Disctinction over the "usb_interface" structure and the "file" structure

                [ USER SPACE ]
               (/dev/nr_driverX)
              ^
     __--struct file--___________________   //functions open, read, write,
    /         v                          \    release
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

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <linux/kernel.h>
#include <linux/module.h> // macro THIS_MODULE
#include <linux/init.h>
#include <linux/usb.h>   // usb stuff
#include <linux/mutex.h> // lock_kernel(), unlock_kernel()
#include <linux/slab.h>  // kzalloc()
#include <linux/errno.h> // pr_err()
#include <asm/uaccess.h> // copy_to_user(), copy_from_user()
#include <linux/wait.h>  // wait_queue_head_t structure

// vendor and product ids of the MCP2515 Microship Demo board
#define VENDOR_ID 0x04d8
#define PRODUCT_ID 0x0070
#define USB_MINOR_BASE 1

// Prevent races between open() and disconnect
static DEFINE_MUTEX(disconnect_mutex);

//------------------------------------------------------------
//             STUCT CORRESPONDING TO THE DEVICE
//------------------------------------------------------------
struct usb_nr
{

    // to record the interrupt in endpoint, defined in probe()
    struct usb_endpoint_descriptor *int_in_endpoint;

    // to record the interrupt out endpoint, defined in probe()
    struct usb_endpoint_descriptor *int_out_endpoint;

    // the urb to read data with
    //   memory allocated in probe()
    //   initialized in open()
    //   used in read()
    struct urb *int_in_urb;

    // the urb to write data with
    //   memory allocated in probe()
    //   initialized in open()
    //   used in write()
    struct urb *int_out_urb;

    // the usb device for this device, used to intialize the urb
    //   A USB device driver commonly has to convert data from a given
    //   struct usb_interface structure into a struct usb_device structure
    //   that the USB core needs for a wide range of function calls. To do
    //   this, the function interface_to_usbdev is provided. Hopefully,
    //   in the future, all USB calls that currently need a struct
    //   usb_device will be converted to take a struct usb_interface
    //   parameter and will not require the drivers to do the conversion.
    struct usb_device *usbdev;

    // the buffer to receive data (from the device through the urb in)
    unsigned char *int_in_buffer;

    // number of bytes received in the buffer after the urb in request
    size_t len_in_buffer;

    // the buffer to receive data (from the device through the urb in)
    unsigned char *int_out_buffer;

    // a read is going on (an urb has been sent)
    bool ongoing_read;

    // a write is going on (an urb has been sent)
    bool outgoing_write;

    // to wait for an ongoing read (an urb that has not been processed yet)
    wait_queue_head_t int_in_wait;
    // to wait for an outgoing write (an urb that has not been processed yet)
    wait_queue_head_t int_out_wait;
};

// function to free all the memory allocated
void free_usb_nr(struct usb_nr *dev)
{
    usb_free_urb(dev->int_in_urb);
    usb_free_urb(dev->int_out_urb);

    // release a use of the usb device structure (ust_get_dev in probe function)
    usb_put_dev(dev->usbdev);
    kfree(dev->int_in_buffer);
    kfree(dev->int_out_buffer);
    kfree(dev);
}

// needed to be declared here for the nr_open() function
static struct usb_driver nr_driver;

//------------------------------------------------------------
//       IMPLEMENTATION OF THE FILE OPERATION FUNCTIONS
//------------------------------------------------------------

//                            OPEN
//------------------------------------------------------------
// Though this is always the first operation performed on the device file, the
//  driver is not required to declare a corresponding method. If this entry is
//  NULL, opening the device always succeeds, but your driver isn't notified.
//
//  The *inode* structure is used by the kernel internally to represent files.
//  Therefore, it is different from the file structure that represents an open
//  file descriptor. There can be numerous file structures representing multiple
//  open descriptors on a single file, but they all point to a single inode
//  structure. The inode structure contains a great deal of information about
//  the file. As a general rule, only two fields of this structure are of
//  interest for writing driver code:
//
//  The *file* structure represents an open file. (It is not specific to device
//      drivers; every open file in the system has an associated struct file in
//      kernel space.) It is created by the kernel on open and is passed to any
//      function that operates on the file, until the last close. After all
//      instances of the file are closed, the kernel releases the data structure
static int nr_open(struct inode *inode, struct file *filp)
{
    // define a pointer over a device struct (usb_ur)
    struct usb_nr *dev = NULL;

    int retval = 0;
    int subminor;
    struct usb_interface *interface; // to get the device interface

    // obtain the minor number from an inode. Note that traditionnally,
    //   major number corresponds to the driver associated to the device
    //   and the minor number determine wich device is referred to
    subminor = iminor(inode);

    // from the minor and the driver it is possible to retrieve the interface
    interface = usb_find_interface(&nr_driver, subminor);

    // we check if it was possible to get the interface back
    if (!interface)
    {
        pr_err("_NR_ %s - error, can't find device for minor %d\n",
               __func__, subminor);
        retval = -ENODEV;
        goto exit;
    }

    // recover our data pointer from the interface
    dev = usb_get_intfdata(interface);
    if (!dev)
    {
        retval = -ENODEV;
        goto exit;
    }

    // save our data pointer in the file's private structure
    //   to be able to recover it in the fops functions (read, write...)
    filp->private_data = dev;

    // variable to get the number of bytes of the buffer receiving
    //   from the urb request
    dev->len_in_buffer = 0;

    // variable to test if an in urb request is going on
    dev->ongoing_read = false;

    // variable to test if an out urb request is going on
    dev->outgoing_write = false;
exit:
    return retval;
}
// release, called when the opened file is closed
static int nr_release(struct inode *inode, struct file *file)
{
    // printk("_NR_ nr_release()\n");
    return 0;
}
// The completion handler function that is called by the USB core when
//   the urb is completely transferred or when an error occurs to the urb. Within
//   this function, the USB driver may inspect the urb, free it, or resubmit it
//   for another transfer.
static void nr_read_int_callback(struct urb *urb)
{
    struct usb_nr *dev;
    dev = urb->context;

    // sync/async unlink faults aren't errors
    if (urb->status)
    {
        if (!(urb->status == -ENOENT ||
              urb->status == -ECONNRESET ||
              urb->status == -ESHUTDOWN))
            pr_err("_NR_ %s - nonzero write interuption status received: %d\n",
                   __func__, urb->status);
    }
    dev->len_in_buffer = urb->actual_length;
    dev->ongoing_read = false;

    // wake the queue sleeping in the read function
    wake_up_interruptible(&dev->int_in_wait);
}

//                           READ
//------------------------------------------------------------
// Used to retrieve data from the device. A null pointer in this position causes
//  the read system call to fail with -EINVAL (“Invalid argument”). A
//  nonnegative return value represents the number of bytes successfully read
//  (the return value is a “signed size” type, usually the native integer type
//  for the target platform).
//  filp is the file pointer and count is the size of the requested data
//  transfer. The buff argument points to the user buffer holding the data to be
//  written or the empty buffer where the newly read data should be placed.
//  Finally, ppos is a pointer to a “long offset type” object that indicates the
//  file position the user is accessing
//
//  The return value for read is interpreted by the calling application program:
//      If the value equals the count argument passed to the read system call,
//          the requested number of bytes has been transferred. This is the
//          optimal case.
//      If the value is positive, but smaller than count , only part of the data
//          has been transferred. This may happen for a number of reasons,
//          depending on the device. Most often, the application program retries
//          the read. For instance, if you read using the fread function, the
//          library function reissues the system call until completion of the
//          requested data transfer.
//      If the value is 0 , end-of-file was reached (and no data was read).
//      A negative value means there was an error. The value specifies what the
//          error was, according to <linux/errno.h>. Typical values returned on
//          error include -EINTR (interrupted system call) or -EFAULT (bad
//          address).
static ssize_t nr_read(struct file *filp, char *buffer, size_t count,
                       loff_t *ppos)
{
    // to return the number of readed bytes
    ssize_t rs = 0;

    // define a pointer over a device struct (usb_ur)
    struct usb_nr *dev = NULL;

    // recover our data pointer from the open file structure (saved inside
    //  the open() function )
    dev = filp->private_data;

retry:
    // first we need to test if an urb hasn't already been submit (and not
    //     finished yet)
    if (dev->ongoing_read)
    {
        // An urb has already been sent,
        //   we have to (legend) wait for it (dary)

        // The process is put to sleep until the condition evaluates to true or a
        //   signal is received. The condition is checked each time the waitqueue
        //   is woken up. wake_up() has to be called after changing any variable
        //   that could change the result of the wait condition
        rs = wait_event_interruptible(dev->int_in_wait, (!dev->ongoing_read));
        if (rs < 0)
        {
            pr_err("_NR_ %s - rs=%d, error while waiting\n", __func__, (int)rs);
            goto exit;
        }
    }
    // if we are here, it means no urb is being processed. We have to check if
    //   one has been (if we have read some data)
    if (dev->len_in_buffer > 0)
    {

        // Copy a block of data into user space.
        //   Returns number of bytes that could not be
        rs = copy_to_user(buffer, dev->int_in_buffer, dev->len_in_buffer);

        // Whatever the amount of data the method transfers, it should generally
        //   update the file position at *ppos to represent the current file
        //   position after successful completion of the system call. The kernel
        //   then propagates the file position change back into the file
        //   structure when appropriate.
        *ppos += dev->len_in_buffer - rs;

        // See the return value in the comments at the beginning of the function
        rs = dev->len_in_buffer - rs;
        dev->len_in_buffer = 0;
        goto exit;
    }
    // if we are here, it means no urb is being processed and no data has been
    //   read, we need to send a new urb
    // The function usb_fill_int_urb is a helper function to properly initialize
    //   a urb to be sent to an interrupt endpoint of a USB device:
    usb_fill_int_urb(
        // urb* : A pointer to the urb to be initialized.
        dev->int_in_urb,

        // usb_device* : The USB device to which this urb is to be sent.
        dev->usbdev,

        // unsigned int : The specific endpoint of the USB device to which
        //   this urb is to be sent. This value is created with the
        //   previously mentioned usb_sndintpipe or usb_rcvintpipe functions.
        //       usb_rcvintpipe() specifies an interrupt IN endpoint for the
        //       specified USB device with the specified endpoint number.
        usb_rcvintpipe(dev->usbdev,
                       dev->int_in_endpoint->bEndpointAddress),

        // A pointer to the buffer from which outgoing data is taken or
        //   into which incoming data is received. Note that this can not
        //   be a static buffer and must be created with a call to kmalloc.
        dev->int_in_buffer,

        // The length of the buffer pointed to by the transfer_buffer pointer
        dev->int_in_endpoint->wMaxPacketSize,

        // usb_complete_t : Pointer to the completion handler that is called
        //   when this urb is completed.
        nr_read_int_callback,

        // void * : Pointer to the blob that is added to the urb structure
        //   for later retrieval by the completion handler function.
        dev,

        // int : The interval at which that this urb should be scheduled.
        dev->int_in_endpoint->bInterval);
    rs = usb_submit_urb(dev->int_in_urb, GFP_KERNEL);
    if (rs < 0)
    {
        pr_err("_NR_ %s - failed submitting urb, error %d\n", __func__,
               (int)rs);
        goto exit;
    }
    // an urb request has been submitted and is not over yet
    dev->ongoing_read = true;

    // we need to wait for the urb to be processed
    goto retry;
exit:
    return rs;
}

// The completion handler function that is called by the USB core when
//   the urb is completely transferred or when an error occurs to the urb. Within
//   this function, the USB driver may inspect the urb, free it, or resubmit it
//   for another transfer.
static void nr_write_int_callback(struct urb *urb)
{
    struct usb_nr *dev;
    dev = urb->context;
    // sync/async unlink faults aren't errors
    if (urb->status)
    {
        if (!(urb->status == -ENOENT ||
              urb->status == -ECONNRESET ||
              urb->status == -ESHUTDOWN))
            pr_err("_NR_ %s - nonzero write interuption status received: %d\n",
                   __func__, urb->status);
    }
    dev->outgoing_write = false;

    // wake the queue sleeping in the write function
    wake_up_interruptible(&dev->int_out_wait);
}

//                           WRITE
//------------------------------------------------------------
// write, like read, can transfer less data than was requested, according to the
//  following rules for the return value:
//      If the value equals count , the requested number of bytes has been
//          transferred.
//      If the value is positive, but smaller than count , only part of the data
//          has been transferred. The program will most likely retry writing the
//          rest of the data.
//      If the value is 0 , nothing was written. This result is not an error,
//          and there is no reason to return an error code. Once again, the
//          standard library retries the call to write.
//      A negative value means an error occurred; as for read, valid error
//          values are those defined in <linux/errno.h
static ssize_t nr_write(struct file *filp, const char *buffer, size_t count,
                        loff_t *ppos)
{

    // define a pointer over a device struct (usb_ur)
    struct usb_nr *dev = NULL;

    int retval = 0;

    // recover our data pointer from the open file structure (saved inside
    //  the open() function )
    dev = filp->private_data;

    if (count <= 0 || count > dev->int_out_endpoint->wMaxPacketSize)
    {
        // verify that we want to send a correct amount of data
        //	(the lenght data that have to be send to our usb device)
        pr_err("_NR_ %s - not or too many data to send", __func__);
        retval = -EINVAL;
        goto error;
    }

    // get the data from the user space
    if (copy_from_user(dev->int_out_buffer, buffer, count))
    {
        pr_err("_NR_ %s - getting data from the user space", __func__);
        retval = -EFAULT;
        goto error;
    }

    // initialize the urb properly (function detailed in the read() function)
    usb_fill_int_urb(dev->int_out_urb,
                     dev->usbdev,
                     usb_sndintpipe(dev->usbdev,
                                    dev->int_out_endpoint->bEndpointAddress),
                     dev->int_out_buffer,
                     count,
                     nr_write_int_callback,
                     dev,
                     dev->int_out_endpoint->bInterval);

    dev->outgoing_write = true;
    retval = usb_submit_urb(dev->int_out_urb, GFP_KERNEL);
    if (retval)
    {
        pr_err("_NR_ %s - error submitting the urb", __func__);
        goto error;
    }

    // detailed in the read() function
    retval = wait_event_interruptible(dev->int_out_wait,
                                      (!dev->outgoing_write));
    if (retval < 0)
    {
        pr_err("_NR_ %s - rs=%d, error while waiting\n", __func__, (int)retval);
        goto error;
    }
    return count;
error:
    return retval;
}

// The file_operations structure is how a char driver sets up this connection.
//   Each field in the structure must point to the function in the driver that
//   implements a specific operation, or be left NULL for unsupported operations
static const struct file_operations nr_fops = {
    // The first file_operations field is not an operation at all;
    //  it is a pointer to the module that “owns” the structure.
    //  This field is used to prevent the module from being unloaded
    //  while its operations are in use.
    .owner = THIS_MODULE,

    // Though this is always the first operation performed on the device file,
    //  the driver is not required to declare a corresponding method.
    .open = nr_open,

    // This operation is invoked when the file structure is being released
    .release = nr_release,

    // size_t (*read) (struct file *, char __user *, size_t, loff_t *);
    //  Used to retrieve data from the device.
    .read = nr_read,

    // size_t (*write) (struct file *, char __user *, size_t, loff_t *);
    //	Used to send data to the device.
    .write = nr_write,
};

// This struct usb_class_driver is used to define a number of different
//   parameters that the USB driver wants the USB core to know when registering
//   for a minor number.
static struct usb_class_driver nr_class = {

    // The name that sysfs uses to describe the device.
    //  If the number of the device needs to be in the name,
    //  the characters %d should be in the name string.
    .name = "nr_driver%d",

    // Pointer to the struct file_operations that this driver
    //  has defined to use to register as the character device
    .fops = &nr_fops,

    // This is the start of the assigned minor range for this driver.
    //  All devices associated with this driver are created with unique,
    //  increasin gminor numbers beginning with this value.
    .minor_base = USB_MINOR_BASE,
};

//------------------------------------------------------------
//    IMPLEMENTATION OF THE USB DRIVER FUNCTIONS/STUCTURES
//------------------------------------------------------------

// defines the USB device ID for the device as having precisely that vendor and
//   product code
static struct usb_device_id id_table[] = {
    {USB_DEVICE(VENDOR_ID, PRODUCT_ID)},
    {},
};
// then adds that information to the exported USB module device table
MODULE_DEVICE_TABLE(usb, id_table);

//                           PROBE
//------------------------------------------------------------
// probe function
//  This function is called by the USB core when it thinks it has a struct
//  usb_interface that this driver can handle. A pointer to the struct
//  usb_device_id that the USB core used to make this decision is also passed to
//  this function. If the USB driver claims the struct usb_interface that is
//  passed to it, it should initialize the device properly and return 0. If the
//  driver does not want to claim the device, or an error occurs, it should
//  return a negative error value.
//      usb_interface : this structure is what the USB core passes to USB
//          drivers and is what the USB driver then is in charge of controlling
static int nr_probe(struct usb_interface *interface,
                    const struct usb_device_id *id)
{
    int retval = -ENODEV;

    // define a pointer over a device struct (usb_ur)
    struct usb_nr *dev = NULL;

    struct usb_endpoint_descriptor *endpoint;

    // An array of interface structures containing all of the
    //   alternate settin gs that maybe selected for this interface.
    //   Each struct usb_host_interface consists of a set of endpoint
    //   configurations as defined by the struct usb_host_endpoint structure
    struct usb_host_interface *iface_desc;

    int i; // for the for loop

    // Allocate memory for our device state and initialize it
    //   kzalloc — allocate memory. The memory is set to zero.
    //   GFP_KERNEL: the type of memory to allocate
    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev)
    {
        pr_err("_NR_ %s - Out of memory\n", __func__);
        goto error;
    }

    // get the usb_device struct from the interface, the using of usb_get_dev():
    //  https://www.kernel.org/doc/htmldocs/usb/API-usb-get-dev.html
    //   "Drivers for USB interfaces should normally record such references in
    //   their probe methods, when they bind to an interface, and release them by
    //   calling usb_put_dev, in their disconnect methods."
    dev->usbdev = usb_get_dev(interface_to_usbdev(interface));

    // initialization of the wait_queue_head_t need to wait for the urb to be
    //   processed (read() function) There are several ways of handling sleeping
    //   and waking up in Linux, each suited to different needs. All, however,
    //   work with the same basic data type, a wait queue (wait_queue_head_t). A
    //   wait queue is a queue of processes that are waiting for an event.
    //   init_waitqueue_head initializes a wait_queue_head_t
    init_waitqueue_head(&dev->int_in_wait);
    init_waitqueue_head(&dev->int_out_wait);

    // Set up interrupt endpoint information
    // A pointer into the array altsetting, denoting the currently active
    //  setting for this interface.
    iface_desc = interface->cur_altsetting;

    // This block of code first loops over every endpoint that is
    //   present in this interface and assigns a local pointer
    //   to the endpoint structure to make it easier to access later
    for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i)
    {
        endpoint = &iface_desc->endpoint[i].desc;
        // we want to get the first (and only in our case) interrupt IN endpoint
        if (!dev->int_in_endpoint && usb_endpoint_is_int_in(endpoint))
        {
            // we found the endpoint
            dev->int_in_endpoint = endpoint;

            // we prepare our in buffer to be able to handle the data
            //   exchanged througt this endpoint
            //       endpoint->wMaxPacketSize: the maximal data
            //       exchanged from this endpoint
            dev->int_in_buffer = kmalloc(endpoint->wMaxPacketSize,
                                         GFP_KERNEL);
            if (!dev->int_in_buffer)
            {
                pr_err("_NR_ %s - Could not allocate int_in_buffer\n", __func__);
                goto error;
            }
        }
        // and the first (and only in our case) interrupt OUT endpoint
        if (!dev->int_out_endpoint && usb_endpoint_is_int_out(endpoint))
        {
            dev->int_out_endpoint = endpoint;

            // we prepare our out buffer to be able to handle the data
            //   exchanged througt this endpoint
            //       endpoint->wMaxPacketSize: the maximal data
            //       exchanged from this endpoint
            dev->int_out_buffer = kmalloc(endpoint->wMaxPacketSize,
                                          GFP_KERNEL);
            if (!dev->int_out_buffer)
            {
                pr_err("_NR_ %s - Could not allocate int_out_buffer\n",
                       __func__);
                goto error;
            }
        }
    }
    if (!dev->int_in_endpoint)
    {
        // If we missed the endpoints we display an error message
        pr_err("_NR_ %s - could not find interrupt IN endpoint\n", __func__);
        goto error;
    }
    else if (!dev->int_out_endpoint)
    {
        pr_err("_NR_ %s - could not find interrupt OUT endpoint\n", __func__);
        goto error;
    }

    // intialization of the urb for the usb reading
    // The first parameter (iso_packets) is the number of isochronous
    //   packets this urb should contain. If you do not want to create
    //   an isochronous urb, this variable should be set to 0 .
    dev->int_in_urb = usb_alloc_urb(0, GFP_KERNEL);

    if (!dev->int_in_urb)
    {
        pr_err("_NR_ %s - Could not allocate int_in_urb\n", __func__);
        goto error;
    }

    // intialization of the urb for the usb writing
    dev->int_out_urb = usb_alloc_urb(0, GFP_KERNEL);
    if (!dev->int_out_urb)
    {
        pr_err("_NR_ %s - Could not allocate int_out_urb\n", __func__);
        goto error;
    }

    // eveything went well,
    //   we save our data pointer in this interface device
    usb_set_intfdata(interface, dev);

    // we register the driver
    retval = usb_register_dev(interface, &nr_class);
    if (retval)
    {
        // something prevented us from registering this driver
        pr_err("_NR_ %s - Not able to get a minor for this device.\n",
               __func__);
        usb_set_intfdata(interface, NULL);
        goto error;
    }
    return 0; // return 0 indicates we will manage this device

error: // we use goto in order to be sure to free
       //   the memory if an error occurs
    if (dev)
    {
        free_usb_nr(dev); // we need to free the memory allocated using kzalloc
    }
    return retval;
}

//                        DISCONNECT
//------------------------------------------------------------
static void nr_disconnect(struct usb_interface *interface)
{
    struct usb_nr *dev;

    // prevent open() from racing disconnect(): not interruptible
    mutex_lock(&disconnect_mutex);

    // we recover the data recorded in the interface
    //   (recorded during the probe() function)
    dev = usb_get_intfdata(interface);

    // we remove the reference over "dev" in the interface
    usb_set_intfdata(interface, NULL);
    if (dev)
    {
        // we need to free the memory allocated to the struct usb_nr
        free_usb_nr(dev);
    }

    // give back the minor
    //   (to avoid the incrementation of the file-node- in /dev/)
    usb_deregister_dev(interface, &nr_class);

    // release the lock
    mutex_unlock(&disconnect_mutex);
}

// The main structure that all USB drivers must create is a struct usb_driver to
//   create a value struct usb_driver structure, only four fields need to
//   beinitialized
static struct usb_driver nr_driver = {
    // Pointer to the name of the driver.
    //  It must be unique amon gall USB drivers in the kernel
    //  and is normally set to the same name as the module name of the driver.
    //  It shows up in sysfs under /sys/bus/usb/drivers/ when the driver is in
    //      the kernel.
    .name = "nr_driver",

    // Pointer to the struct usb_device_id table that contains a list of all of
    //  the different kinds of USB devices this driver can accept
    .id_table = id_table,

    // Pointer to the probe function in the USB driver.
    .probe = nr_probe,

    // Pointer to the disconnect function in the USB driver.
    .disconnect = nr_disconnect,
};

//------------------------------------------------------------
//           "CONSTRUCTOR/DESCTUCTOR" OF THE DRIVER
//------------------------------------------------------------

// To register the struct usb_driver with the USB core, a call to
//   usb_register_driver is made with a pointer to the struct usb_driver. This is
//   traditionally done in the module initialization code for the USB driver:
static int __init usb_nr_init(void)
{
    int retval = -1;
    retval = usb_register(&nr_driver);
    if (retval)
        pr_err("_NR_ %s - usb_register failed. Error number %d\n", __func__,
               retval);
    return retval;
}

// When the USB driver is to be unloaded, the struct usb_driver needs to be
//   unregistered from the kernel. This is done with a call to
//   usb_deregister_driver. When this call happens, any USB interfaces that were
//   currently bound to this driver are disconnected, and the disconnect function
//   is called for them.
static void __exit usb_nr_exit(void)
{
    usb_deregister(&nr_driver);
}

// link the init and exit function to the module
module_init(usb_nr_init);
module_exit(usb_nr_exit);

MODULE_AUTHOR("Ayush Abrol\n");
MODULE_LICENSE("GPL");
