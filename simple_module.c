#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/err.h>
#include <linux/version.h>
#include <linux/completion.h>
#include <linux/usb.h>

#define DEVNR 511                 // Major device number
#define DEVNRNAME "Marks_Driver"  // Name that will show in /proc/devices/
#define BUFFER_SIZE 256           // Maximum buffer size for operations

#define IOCTL_MAGIC 'C'
#define PORTAL_SET_COLOUR _IOW(IOCTL_MAGIC, 1, char[BUFFER_SIZE])

static struct cdev my_cdev;       // Character device
static char buffer[BUFFER_SIZE];  // Buffer for read/write

//Synchronization objects
static DEFINE_MUTEX(my_mutex); //mutex to ensure that only one thread can access the critical section
static DECLARE_WAIT_QUEUE_HEAD(read_q); // A wait queue for read operations
static DECLARE_WAIT_QUEUE_HEAD(write_q); //  wait queue for write operations

// Flag to indicate if there is data available in the buffer.
//   0 = Buffer is empty (space available for write)
//   1 = Buffer is full (data available for read)
static int data_ready = 0;

static char colour[BUFFER_SIZE] = {0};


// table of USB devices that module supports
static struct usb_device_id portal_of_power_table[] = {
    { USB_DEVICE(0x1430, 0x0150) }
};
MODULE_DEVICE_TABLE(usb, portal_of_power_table);

static int portal_probe(struct usb_interface *interface, const struct usb_device_id *id);
static void portal_disconnect(struct usb_interface *interface);


static struct usb_driver skylanders_driver = {
    .name        = "portal_of_power",
    .probe       = portal_probe,
    .disconnect  = portal_disconnect,
    .id_table    = portal_of_power_table,
    .supports_autosuspend = 1,

};

static struct usb_device *usb_dev = NULL;

static int portal_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
    usb_dev = interface_to_usbdev(interface);  // Store the USB device reference
    pr_info("portal_of_power: Device (Vendor: 0x%04X, Product: 0x%04X) plugged\n", id->idVendor, id->idProduct);
    return 0;
}

static void portal_disconnect(struct usb_interface *interface)
{
    pr_info("portal_of_power: Device disconnected\n");
    usb_dev = NULL;  // Clear the reference
}


//Read thread
struct read_ctx {
    struct file *file;           // Pointer to file structure
    char __user *user_buffer;    // User-space buffer pointer to copy data to
    size_t length;               // number of bytes requested to read
    loff_t *offset;              // pointer to file offset
    int result;                  // Stores the result
    struct completion comp;      // signal read is done
};

// Read thread function
static int read_thread_func(void *data){
    struct read_ctx *ctx = (struct read_ctx *)data; // Cast the passed data to a read context pointer
    int to_copy;
    int not_copied;
    int total_copied = 0;
    int ret;

    printk(KERN_INFO "Mark's Driver - Read thread started, *off: %lld\n", *ctx->offset);

    // Loop until copied all
    while (total_copied < ctx->length) {

        // Block until buffer has data
        ret = wait_event_interruptible(read_q, data_ready != 0);
        // if a signal interrupted - error handling
        if (ret < 0) {
            printk(KERN_WARNING "Mark's Driver - Read thread interrupted while waiting for data\n");
            ctx->result = -ERESTARTSYS; //restart system call
            complete(&ctx->comp);   //Signal thread is finished
            return -ERESTARTSYS;
        }

        // Lock mutex
        ret = mutex_lock_interruptible(&my_mutex);
        // if a signal interrupted - error handling
        if (ret < 0) {
            printk(KERN_WARNING "Mark's Driver - Read thread interrupted while waiting for mutex\n");
            ctx->result = -ERESTARTSYS;
            complete(&ctx->comp);   //Signal thread is finished
            return -ERESTARTSYS;
        }
        printk(KERN_INFO "Mark's Driver - Read thread aquired Mutex");

        if(data_ready == 0){
            mutex_unlock(&my_mutex);
            printk(KERN_WARNING "Mark's Driver - Read thread call but nothing to read");
            continue;
        }

        to_copy = (ctx->length - total_copied) < BUFFER_SIZE ?(ctx->length - total_copied) : BUFFER_SIZE;   //copy size length or BUFFER_SIZE bytes (whichever is smaller)

        //Ensure all data is copied
        not_copied = copy_to_user(ctx->user_buffer + total_copied, buffer, to_copy);
        if (not_copied) {
            mutex_unlock(&my_mutex);
            printk(KERN_ERR "Mark's Driver - Could not read - Reason unknown\n");
            ctx->result = -EFAULT;
            complete(&ctx->comp);   //Signal thread is finished
            return -EFAULT;
        }


        // mark buffer as free
        data_ready = 0;

        // Update the offset
        *(ctx->offset) += (to_copy - not_copied);

        // Unlock mutex
        mutex_unlock(&my_mutex);

        // Wake up any waiting writer processes
        wake_up_interruptible(&write_q);

        printk(KERN_INFO "Mark's Driver - Read thread read: %d bytes, released mutex, and updated offset%lld\n", (to_copy - not_copied), *ctx->offset);   //print no of bytes read

        // Update the total bytes copied
        total_copied += (to_copy - not_copied);
    }

    // Set the result (no. of bytes read)
    ctx->result = total_copied;

    complete(&ctx->comp);   //Signal thread is finished
    printk(KERN_INFO "Mark's Driver - Read thread finished execution, total bytes read: %d\n", total_copied);
    return to_copy - not_copied;
}

//Read Function
static ssize_t my_read(struct file *file, char __user *user_buffer, size_t length, loff_t *offset){
    // initalize variables and structs
    struct task_struct *read_thread;
    struct read_ctx *ctx;
    int ret;

    printk(KERN_INFO "Mark's Driver - Read is called, *off: %lld\n", *offset);   //Print if the file is being read

    // Check if the file was opened with read permissions
    // FMODE_READ flag = file was opened for reading
    if (!(file->f_mode & FMODE_READ)) {
        printk(KERN_WARNING "Mark's Driver - Read attempted without read permissions\n");
        return -EACCES; // Return error code for permission denied
    }


    // Allocate memory read
    ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
    if (!ctx){
        printk(KERN_ERR "Mark's Driver - Failed to allocate read memory\n");
        return -ENOMEM; //return no memory error if failure to allocate memory
    }

    printk(KERN_INFO "Mark's Driver - Allocate read memory\n");

    // Initialize ctx with provided parameters
    ctx->file = file;
    ctx->user_buffer = user_buffer;
    ctx->length = length;
    ctx->offset = offset;
    init_completion(&ctx->comp);  // Initialize the completion structure

    // Create read thread
    read_thread = kthread_create(read_thread_func, ctx, "read_thread");
    if (IS_ERR(read_thread)) {
        printk(KERN_ERR "Mark's Driver - Failed to create read thread\n");
        kfree(ctx);
        return PTR_ERR(read_thread);
    }

    printk(KERN_INFO "Mark's Driver - Created Read Thread\n");

    // start read thread
    wake_up_process(read_thread);

    // Wait for read thread to finish
    wait_for_completion(&ctx->comp);
    ret = ctx->result;

    // Free allocated memory
    kfree(ctx);

    return ret;
}

//Write thread
struct write_ctx {
    struct file *file;           // Pointer to file structure
    const char __user *user_buffer; // User-space buffer pointer to copy data from
    size_t length;               // Number of bytes requested to write
    loff_t *offset;              // pointer to file offset
    int result;                  // Stores the result
    struct completion comp;      // signal write is done
};

// Write thread function
static int write_thread_func(void *data){

    struct write_ctx *ctx = (struct write_ctx *)data; // Cast the argument to a write context pointer
    int to_write;
    int not_written;
    int total_written = 0;
    int ret;

    printk(KERN_INFO "Mark's Driver - Write thread started, *off: %lld\n", *ctx->offset);

    // Loop until everything written
    while (total_written < ctx->length) {

        // Block until the buffer is free
        ret = wait_event_interruptible(write_q, data_ready == 0);
        // if a signal interrupted - error handling
        if (ret < 0) {
            printk(KERN_WARNING "Mark's Driver - Write thread interrupted while waiting for free buffer\n");
            ctx->result = -ERESTARTSYS; //restart system call
            complete(&ctx->comp);   //Signal thread is finished
            return -ERESTARTSYS;
        }

        // Lock mutex
        ret = mutex_lock_interruptible(&my_mutex);
        // if a signal interrupted - error handling
        if (ret < 0) {
            printk(KERN_WARNING "Mark's Driver - Write thread interrupted while waiting for mutex\n");
            ctx->result = -ERESTARTSYS;
            complete(&ctx->comp);   //Signal thread is finished
            return -ERESTARTSYS;
        }

        printk(KERN_INFO "Mark's Driver - Write thread aquired Mutex\n");

        if(data_ready != 0){
            mutex_unlock(&my_mutex);
            printk(KERN_WARNING "Mark's Driver - Write thread called but data buffer is full\n");
            continue;
        }

        to_write = (ctx->length - total_written) < BUFFER_SIZE ?(ctx->length - total_written) : BUFFER_SIZE;   //copy length or BUFFER_SIZE bytes (whichever is smaller)

        //Ensure all data is copied
        not_written = copy_from_user(buffer, ctx->user_buffer + total_written, to_write);
        if (not_written) {
            mutex_unlock(&my_mutex);
            printk(KERN_ERR "Mark's Driver - Write thread: Error copying data from user-space\n");
            ctx->result = -EFAULT;
            complete(&ctx->comp);   //Signal thread is finished
            return -EFAULT;
        }

        // Mark the buffer as full
        data_ready = 1;

        // Update the offset
        *(ctx->offset) += (to_write - not_written);

        // Unlock mutex
        mutex_unlock(&my_mutex);

        // Wake up processes waiting to read from buffer
        wake_up_interruptible(&read_q);

        printk(KERN_INFO "Mark's Driver - Write: %d bytes, released mutex, and updated offset%lld\n", (to_write - not_written), *ctx->offset);  //print no of bytes writen

        // Update the user's offset and total bytes copied
        total_written += (to_write - not_written);
    }

    // Set the result (no. of bytes read)
    ctx->result = total_written;
    complete(&ctx->comp);   //Signal thread is finished

    printk(KERN_INFO "Mark's Driver - Write thread finished execution, total bytes written: %d\n", total_written);
    return to_write - not_written;
}

static ssize_t my_write(struct file *file, const char __user *user_buffer, size_t length, loff_t *offset){
    // initalize variables and structs
    struct task_struct *write_thread;
    struct write_ctx *ctx;
    int ret;

    printk(KERN_INFO "Mark's Driver - Write is called\n");

    // Check if the file was opened with write permissions
    // FMODE_WRITE flag = file was opened for writing
    if (!(file->f_mode & FMODE_WRITE)) {
        printk(KERN_WARNING "Mark's Driver - Write attempted without write permissions\n");
        return -EACCES; // Return error code for permission denied
    }

    // Allocate memory for write
    ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
    if (!ctx){
        printk(KERN_ERR "Mark's Driver - Failed to allocate write memory\n");
        return -ENOMEM;
    }
    printk(KERN_INFO "Mark's Driver - Allocate write memory\n");

    // Initialize ctx with provided parameters
    ctx->file = file;
    ctx->user_buffer = user_buffer;
    ctx->length = length;
    ctx->offset = offset;
    init_completion(&ctx->comp);

    // Create the write
    write_thread = kthread_create(write_thread_func, ctx, "write_thread");
    if (IS_ERR(write_thread)) {
        printk(KERN_ERR "Mark's Driver - Failed to create write thread\n");
        kfree(ctx);
        return PTR_ERR(write_thread);
    }
    printk(KERN_INFO "Mark's Driver - Created Write Thread\n");

    // Start write thread
    wake_up_process(write_thread);

    // Wait for the write thread to finish
    wait_for_completion(&ctx->comp);
    ret = ctx->result;

    // Free allocated memory
    kfree(ctx);

    return ret;
}

//Open file operation
static int my_open(struct inode *inode, struct file *filp){
    printk(KERN_INFO "Mark's Driver - Major: %d, Minor: %d\n", imajor(inode), iminor(inode));  //print out the major and minor device number

    printk(KERN_INFO "Mark's Driver - Open: Offset: %lld, Mode: %d, Flags: %d\n", filp->f_pos, filp->f_mode, filp->f_flags);

    return 0;   // Return 0 means success
}

//Close File
static int my_close(struct inode *inode, struct file *filp){
    printk(KERN_INFO "Mark's Driver - File is closed\n");   //print out when file closed
    return 0;   // Return 0 means success
}

//ioctl function for changing portal colour
static unsigned char get_colour_code(const char *colour_str)
{
    if (strcmp(colour_str, "blue") == 0)
        return 1;
    else if (strcmp(colour_str, "red") == 0)
        return 2;
    else if (strcmp(colour_str, "green") == 0)
        return 3;
    else if (strcmp(colour_str, "yellow") == 0)
        return 4;
    else {
        printk(KERN_ERR "Mark's Driver - Unknown colour: %s\n", colour_str);
        return 0;  // Unknown/unsupported colour
    }
}

/* Helper: Send USB control message to change the portal colour */
static int change_portal_colour(const char *colour_str)
{
    unsigned char colour_code = get_colour_code(colour_str);
    int ret;

    if (colour_code == 0)
        return -EINVAL;
    /* Assume usb_dev is set by the USB driver (portal_probe) */
    extern struct usb_device *usb_dev;  // usb_dev is declared globally in this module

    if (!usb_dev) {
        printk(KERN_ERR "Mark's Driver - USB device not found!\n");
        return -ENODEV;
    }

    printk(KERN_INFO "Mark's Driver - Changing portal colour to %s (code %u)\n", colour_str, colour_code);

    ret = usb_control_msg(usb_dev,
                          usb_sndctrlpipe(usb_dev, 0),
                          0x01,  // USB_REQUEST (hypothetical)
                          USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT,
                          0x00,  // USB_VALUE
                          0x00,  // USB_INDEX
                          &colour_code,
                          sizeof(colour_code),
                          1000); // USB_TIMEOUT
    if (ret < 0) {
        printk(KERN_ERR "Mark's Driver - Failed to send USB control message: %d\n", ret);
        return ret;
    }

    printk(KERN_INFO "Mark's Driver - Colour change USB message sent successfully\n");
    return 0;
}

/* IOCTL function to handle colour change request from user space */
static long portal_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    char user_colour[32]; // Buffer to hold the colour string from user space

    if (cmd == PORTAL_SET_COLOUR){
        if (copy_from_user(colour, (char __user *)arg, sizeof(colour))){
            return -EFAULT;
        }
        printk(KERN_INFO "Changing portal colour to: %s\n", colour);
        return change_portal_colour(colour);
    }
    return -EINVAL;
}


//file operations supported by device driver
static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = my_open,
    .release = my_close,
    .read = my_read,
    .write = my_write,
    .unlocked_ioctl = portal_ioctl,
};


//Called when driver is Loaded
static int __init driverLoaded(void){

    int status; //needed for error checking
    dev_t devnr = MKDEV(DEVNR,0);   //device number

    status = register_chrdev_region(devnr, 1, DEVNRNAME);   //register device number
    if(status < 0){
        printk(KERN_ERR "Mark's Driver - Error registering device number!\n");
        return status;   //return error code
    }

    cdev_init(&my_cdev, &fops);     //initilaze character device with our file operations
    my_cdev.owner = THIS_MODULE;    //set owner field

    status = cdev_add(&my_cdev, devnr, 1);
    if(status < 0){
        printk(KERN_ERR "Mark's Driver - Error adding cdev!\n");
        unregister_chrdev_region(devnr, 1); //unregister device number
        return status;   //return error code
    }

    status = usb_register(&skylanders_driver);
    if (status < 0) {
        pr_err("Mark's Driver - Failied to regester %s driver. Error number %d\n", skylanders_driver.name, status);
        return status;  //return error code

    }

    printk(KERN_INFO "Mark's Driver is Loaded! - Major Device Number: %d\n", DEVNR);

    return 0;   // Return 0 means success
}

//Called when driver is Unloaded
static void __exit driverUnload(void){
    dev_t devnr = MKDEV(DEVNR,0);   //device number
    unregister_chrdev_region(devnr,1); //unregister device number
    printk(KERN_INFO "Mark's Driver - Device Number Unregister\n");
    cdev_del(&my_cdev);     //Remove character device from system
    usb_deregister (&skylanders_driver);
    printk(KERN_INFO "Mark's Driver - Unregester %s driver. Error number\n", skylanders_driver.name);
    printk(KERN_INFO "Mark's Driver - Goodbye is Unloaded!\n");
}

// Register module entry and exit points
module_init(driverLoaded);
module_exit(driverUnload);

//Licence and other info
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mark");
MODULE_DESCRIPTION("Kernal module for Markitecture project");
MODULE_VERSION("1.0");
