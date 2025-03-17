#include <linux/cdev.h>      // managing character devices
#include <linux/device.h>    // device nodes
#include <linux/fs.h>        // device registration, file operations and major/minor numbers
#include <linux/hid.h>       // handles HID devices
#include <linux/init.h>      // __init and __exit
#include <linux/input.h>     // handles input devices
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>    // logging
#include <linux/proc_fs.h>   // proc files
#include <linux/uaccess.h>   // copy_to_user and copy_from_user (mem access)
#include <linux/usb.h>       // USB operations

#define DEVICE_NAME "Marks_Driver"
#define BUFFER_SIZE 256

#define MAJOR_NUMBER 500    //Major Device Number

#define VENDOR_ID 0x046d     //logitech vendor ID
#define PRODUCT_ID 0xc077    //my specific mouse

// IOCTL commands (magic letter = L)
// 1,2 differenciate between IOCTL functions
//_IOR: Input/Output Read
//_IOW: Input/Output Write
#define IOCTL_GET _IOR('L', 1, short)
#define IOCTL_SET _IOW('L', 2, short)

long marks_mouse_ioctl_functions(struct file *file, unsigned int command_code, unsigned long arguement);

static char buffer [BUFFER_SIZE];    // Buffer for read/write
static size_t data_buffer_size = 0;     // Size of data currently stored in buffer
static struct class *device_class;      // Class for the device, used for creating device files in sysfs
static struct input_dev *input_device_struct;        //device structure representing the mouse
static struct cdev marks_mouse_cdev;       // Character device structure
static struct device *device_struct_registering;     //mouse device structure created for registering the device
static struct proc_dir_entry *proc_file;        // used for creating /proc files
static struct proc_dir_entry *proc_folder;        // used for creating /proc folder

static DEFINE_MUTEX(read_mutex);    //read mutex

// track button status
static short button_status = 0;

// Values tracked for the proc file
static int left_click = 0;
static int right_click = 0;
static int scroll_click = 0;

//Open file operation
static int marks_mouse_open(struct inode *inode, struct file *filp){
    printk(KERN_INFO "Mark's Driver - Major: %d, Minor: %d\n", imajor(inode), iminor(inode));  //print out the major and minor device number

    printk(KERN_INFO "Mark's Driver - Open: Offset: %lld, Mode: %d, Flags: %d\n", filp->f_pos, filp->f_mode, filp->f_flags);    //Print info of opened file

    return 0;   // Return 0 means success
}

//Close File
static int marks_mouse_close(struct inode *inode, struct file *filp){
    printk(KERN_INFO "Mark's Driver - File is closed\n");   //print out when file closed
    return 0;   // Return 0 means success
}

// Read Function
static ssize_t marks_mouse_read(struct file *file, char __user *user_buffer, size_t length, loff_t *offset){
    size_t available;
    size_t data_to_copy;
    int status; // for error checking

    printk(KERN_INFO "Mark's Driver - Read started, *off: %lld\n", *offset);

    mutex_lock(&read_mutex);    // Lock the mutex to protect the buffer and offset
    printk(KERN_INFO "Mark's Driver - Read aquired Mutex");

    //Check if any data available
    if (*offset >= data_buffer_size) {
        *offset = 0;    //reset offset
        data_buffer_size = 0;   //reset buffer
        mutex_unlock(&read_mutex);  //unlock mutex
        printk(KERN_WARNING "Mark's Driver - Read called but nothing to read\n");
        return 0;
    }

    available = data_buffer_size - *offset;     // Calculate number bytes are available to read

    data_to_copy = (length < available) ? length : available;   //copy size requested length or data available (whichever is smaller)

    //Ensures all data is copied
    status = copy_to_user(user_buffer, buffer + *offset, data_to_copy);
    if (status) {
        mutex_unlock(&read_mutex);  //unlock mutex
        printk(KERN_ERR "Mark's Driver - Could not read from device\n");
        return -EFAULT;
    }

    *offset += data_to_copy;  // Update the offset

    // If all data has been read, reset offset and clear buffer
    if (*offset >= data_buffer_size) {
        *offset = 0;    //reset offset
        data_buffer_size = 0;   //reset buffer
    }

    mutex_unlock(&read_mutex);    // Unlock the mutex

    printk(KERN_INFO "Mark's Driver - Device read %zu bytes\n", data_to_copy);
    printk(KERN_INFO "Mark's Driver - Read: %zu bytes, released mutex, and updated offset%lld\n", data_to_copy, *offset);
    return data_to_copy;
}

// Ioctl Functions
long marks_mouse_ioctl_functions(struct file *file, unsigned int command_code, unsigned long argument){

    //Get current button status from kernel space and pass to user space
    if (command_code == IOCTL_GET) {

        printk(KERN_INFO "Mark's Driver - Copying the current button pressed from kernel space to user space\n");

        //Ensures data was copied successfully
        if (copy_to_user((int *)argument, &button_status, sizeof(button_status))){
            printk(KERN_WARNING "Mark's Driver - Error while copying button pressed\n");
            return -EFAULT;
        }

    //reads value from user space and writes it into the kernel’s button_status variable
    }else if (command_code == IOCTL_SET) {

        printk(KERN_INFO "Mark's Driver - Changing the value of the current button pressed\n");

        //Ensures data was copied successfully
        if (copy_from_user(&button_status, (int *)argument, sizeof(button_status))){
            printk(KERN_WARNING "Mark's Driver - Error while changing value\n");
            return -EFAULT;
        }

    //Recieved a different command
    }else {
        printk(KERN_ERR "Mark's Driver - IOCTL functions recieved an unknow command\n");
        return -EINVAL; //unknown command
    }

    return 0;
}

//file operations supported by device driver
static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = marks_mouse_open,
    .release = marks_mouse_close,
    .read = marks_mouse_read,
    .unlocked_ioctl = marks_mouse_ioctl_functions
};

//read proc files
static ssize_t read_proc(struct file *file, char __user *user_buffer, size_t bytes_to_read, loff_t *offset){
    char buffer_proc_output[BUFFER_SIZE];

    // copy string into buffer
    int length = snprintf(buffer_proc_output, sizeof(buffer_proc_output),"Scroll: %d\nLeft: %d\nRight: %d\n\n", scroll_click, left_click, right_click);

    printk(KERN_INFO "Mark's Driver - Copied %d bytes from proc files\n", length);

    //copy data from kernal buffer to userspace
    return simple_read_from_buffer(user_buffer, bytes_to_read, offset, buffer_proc_output, length); //helper function to safely copy data from kernel to user space
}

//write proc file
static ssize_t write_proc(struct file *file, const char *user_buffer, size_t count, loff_t *offset) {
    char text[255];
    int to_copy;
    int not_copied;
    int delta;

    // clear block of memory
    memset(text, 0, sizeof(text));

    // Get amount of data to copy
    to_copy = min(count, sizeof(text));

    // Copy data to kernel
    not_copied = copy_from_user(text, user_buffer, to_copy);
    printk(KERN_INFO "Mark's Driver - Written %s to proc files\n", text);

    // Calculate data
    delta = to_copy - not_copied;

    return delta;;
}

// proc file operations
static struct proc_ops proc_file_operations = {
    .proc_read = read_proc,
    .proc_write = write_proc,
};

// create proc files and folders
static int create_proc(void){

    //create and verify creation of proc folder
    proc_folder = proc_mkdir("MDriver", NULL);
    if (proc_folder == NULL) {
        printk(KERN_ERR "Mark's Driver - Failed to create /proc/MDriver/ directory\n");
        return -ENOMEM;
    }

    //Creates proc file /proc/MDriver/Marks_Driver
    //0644 means owner has read and write and users have read only
    //Null means no parent directory
    proc_file = proc_create(DEVICE_NAME, 0644, proc_folder, &proc_file_operations);
    if (proc_file == NULL) {
        printk(KERN_ERR "Mark's Driver - Failed to create /proc/MDriver/%s\n", DEVICE_NAME);
        proc_remove(proc_folder);   //remove proc folder
        return -ENOMEM;
    }

    printk(KERN_INFO "Mark's Driver - Create proc files: /proc/MDriver/%s\n", DEVICE_NAME);
    return 0;
}

//remove proc files and folders
static void remove_proc(void){
    proc_remove(proc_folder);   //remove proc folder
    proc_remove(proc_file);     //remove proc file
    printk(KERN_INFO "Mark's Driver - Removed proc files /proc/MDriver/%s\n", DEVICE_NAME);
}

// table of HID devices that module supports
static struct hid_device_id hid_mouse_table[] = {
    { HID_USB_DEVICE(VENDOR_ID, PRODUCT_ID) },
    { }  // Terminating entry
};
MODULE_DEVICE_TABLE(hid, hid_mouse_table);

//initializes the input device for the mouse
static int init_mouse_input(struct hid_device *hid_device_struct, const struct hid_device_id *id){
    int status;

    //Reads devices capabilities
    status = hid_parse(hid_device_struct);
    if (status) {
        printk(KERN_ERR "Mark's Driver - Failed to read device capabilities (hid_parse): %d\n", status);
        return status;
    }
    printk(KERN_INFO "Mark's Driver - Added read device capabilities (hid_parse)\n");

    //sets up any nessessary hardware interface (starts communication with hardware)
    //HID_CONNECT_DEFAULT specifies default connection mode
    status = hid_hw_start(hid_device_struct, HID_CONNECT_DEFAULT);
    if (status) {
        printk(KERN_ERR "Mark's Driver - Failed to start communicating with hardware (hid_hw_start): %d\n", status);
        return status;
    }
    printk(KERN_INFO "Mark's Driver - Started device communicating with hardware (hid_hw_start)\n");

    //Creates input device structure
    input_device_struct = input_allocate_device();
    if (!input_device_struct) {
        printk(KERN_ERR "Mark's Driver - Failed to create input device structure (input_allocate_device)\n");
        return -ENOMEM;
    }

    input_device_struct->name = "logitech_mouse";
    input_device_struct->phys = "Bus 003 Device 029";    //device location
    input_device_struct->id.bustype = BUS_USB;           //connected via usb
    input_device_struct->id.vendor = id->vendor;         //sets vendor ID
    input_device_struct->id.product = id->product;       //sets product ID
    input_device_struct->id.version = 0x0107;            //sets the version to 1.7 (firmware version for mouse)

    set_bit(EV_REL, input_device_struct->evbit); //device supports relative movement events
    set_bit(REL_X, input_device_struct->relbit); //device supports relative movement along X axes
    set_bit(REL_Y, input_device_struct->relbit); //device supports relative movement along y axes
    set_bit(EV_KEY, input_device_struct->evbit); //device supports button events
    set_bit(BTN_LEFT, input_device_struct->keybit);
    set_bit(BTN_RIGHT, input_device_struct->keybit);
    set_bit(BTN_MIDDLE, input_device_struct->keybit);    //types of button presses

    //adds the input device to the system
    status = input_register_device(input_device_struct);
    if (status) {
        input_free_device(input_device_struct);
        printk(KERN_ERR "Mark's Driver - Failed to add device (input_register_device): %d\n", status);
        return status;
    }
    printk(KERN_INFO "Mark's Driver - Added device (input_register_device)\n");

    return 0;
}

// called when portal is connected
static int mouse_probe(struct hid_device *hid_device_struct, const struct hid_device_id *id){

    int status;  //Used for error handling
    dev_t device_number = MKDEV(MAJOR_NUMBER, 0);   //device number

    //initializes the input device for the mouse
    status = init_mouse_input(hid_device_struct, id);
    if (status){
        printk(KERN_ERR "Mark's Driver - Error while initilizing the input device%d\n",status);
        return status;
    }

    status = register_chrdev_region(device_number, 1,DEVICE_NAME);   //register device number
    if(status < 0){
        printk(KERN_ERR "Mark's Driver - Error registering device number: %d. Error code: %d\n", MAJOR_NUMBER, status);
        return status;   //return error code
    }
    printk(KERN_INFO "Mark's Driver - %s registered. Major number: %d\n", DEVICE_NAME, MAJOR_NUMBER);

    cdev_init(&marks_mouse_cdev, &fops);     //initilaze character device with our file operations
    marks_mouse_cdev.owner = THIS_MODULE;    //set owner field
    status = cdev_add(&marks_mouse_cdev, device_number, 1);
    if (status < 0) {
        printk(KERN_ERR "Mark's Driver - Error adding cdev: %d\n", status);
        unregister_chrdev_region(device_number, 1); //unregister device number
        return status;   //return error code
    }
    printk(KERN_INFO "Mark's Driver - Added cdev successfully\n");

    // Create device class
    device_class = class_create("marks_mouse_class");
    if (IS_ERR(device_class)) {
        cdev_del(&marks_mouse_cdev); //Remove character device from system
        unregister_chrdev_region(device_number, 1); //unregister device number
        printk(KERN_ERR "Mark's Driver - Failed to create device class\n");
        return PTR_ERR(device_class);
    }
    printk(KERN_INFO "Mark's Driver - Added device class successfully\n");

    //Create device
    //first null means no parent device
    //second null means no driver specific data needed
    device_struct_registering = device_create(device_class, NULL, device_number, NULL, DEVICE_NAME);
    if (IS_ERR(device_struct_registering)) {
        cdev_del(&marks_mouse_cdev); //Remove character device from system
        class_destroy(device_class);    //Remove class
        unregister_chrdev_region(device_number, 1); //unregister device number
        printk(KERN_ALERT "Mark's Driver - Failed to create the device\n");
        return PTR_ERR(device_struct_registering);
    }
    printk(KERN_INFO "Mark's Driver - Created device successfully\n");

    printk(KERN_NOTICE "Mark's Driver - Connected Mouse\n");
    return 0;
}

//cleans up when mouse unplugged
static void mouse_remove(struct hid_device *hid_device_struct){
    hid_hw_stop(hid_device_struct); //cancel pending data transfers and cleans up hid_hw_start

    input_unregister_device(input_device_struct);    //removes device from Linux input

    //remove proc files
    remove_proc();

    dev_t device_number = MKDEV(MAJOR_NUMBER, 0);     //device number
    device_destroy(device_class, device_number);    //destroy device
    printk(KERN_INFO "Mark's Driver - Destroy device\n");

    class_destroy(device_class);        //Remove class
    printk(KERN_INFO "Mark's Driver - Removed class\n");

    cdev_del(&marks_mouse_cdev);     //Remove character device from system
    printk(KERN_INFO "Mark's Driver - Removed character device\n");

    unregister_chrdev_region(device_number, 1);     //unregister device number
    printk(KERN_INFO "Mark's Driver - Device number unregistered\n");

    printk(KERN_NOTICE "Mark's Driver - Disconncted Mouse\n");
}

//logs mouse events
static int mouse_events(struct hid_device *hid_device_struct, struct hid_report *report, u8 *device_data, int data_array_size){

    // ensures at least 3 bytes (one for button states and 2 for movement)
    if (data_array_size < 3){
        printk(KERN_WARNING "Mark's Driver - mouse_events was call with less than 3 bytes\n");
        return 0;
    }

    //button states
    int button_states = device_data[0]; //each bit refers to a button pressed or not

    int free_space = BUFFER_SIZE - data_buffer_size;    //avoid overflow by calculating free space

    //checks if bit 0 == 1
    if (button_states & (1 << 0)) {
        printk(KERN_INFO "Mark's Driver - Left Click\n");
        // Ensures have enough space to log the message
        if (free_space > 0) {
            int date_written = snprintf(buffer + data_buffer_size, free_space, "Left Click\n");     // data into the buffer
            data_buffer_size += date_written;   //updates data_buffer_size
            free_space = BUFFER_SIZE - data_buffer_size;    //recalculate the free_space
        }else{
            printk(KERN_WARNING "Mark's Driver - Data not added to the buffer due to lack of space\n");
        }
        left_click++;
        button_status = 1;  // Indicates left-click
    }
    // checks if bit 1 == 1
    if (button_states & (1 << 1)) {
        printk(KERN_INFO "Mark's Driver - Right Click\n");
        // Ensures have enough space to log the message
        if (free_space > 0) {
            int date_written = snprintf(buffer + data_buffer_size, free_space, "Right Click\n");    // data into the buffer
            data_buffer_size += date_written;   //updates data_buffer_size
            free_space = BUFFER_SIZE - data_buffer_size;    //recalculate the free_space
        }else{
            printk(KERN_WARNING "Mark's Driver - Data not added to the buffer due to lack of space\n");
        }
        right_click++;
        button_status = 2;  // Indicates right-click
    }
    // checks if bit 2 == 1
    if (button_states & (1 << 2)) {
        printk(KERN_INFO "Mark's Driver - Scroll Click\n");
        // Ensures have enough space to log the message
        if (free_space > 0) {
            int date_written = snprintf(buffer + data_buffer_size, free_space, "Scroll Click\n");   // data into the buffer
            data_buffer_size += date_written;   //updates data_buffer_size
            free_space = BUFFER_SIZE - data_buffer_size;    //recalculate the free_space
        }else{
            printk(KERN_WARNING "Mark's Driver - Data not added to the buffer due to lack of space\n");
        }
        scroll_click++;
        button_status = 3;  // Indicates middle-click
    }

    //movement along x axis
    int x_movement = (int)((signed char)device_data[1]);   //relative movement along horizontally

    //movement along y axis
    int y_movement = (int)((signed char)device_data[2]);   //relative movement along vertically

    input_report_rel(input_device_struct, REL_X, x_movement);     //reports x movement
    input_report_rel(input_device_struct, REL_Y, y_movement);     //reports y movement
    input_sync(input_device_struct);                              //tells linux to now process the events being reported

    // log mouse movement when there is any
    if (x_movement != 0 || y_movement != 0) {
        printk_ratelimited(KERN_INFO "Mark's Driver - Mouse - X moved: %d, Y moved: %d\n", x_movement, y_movement);

        // Ensures have enough space to log the message
        if (free_space > 0) {
            int data_written = snprintf(buffer + data_buffer_size, free_space, "Mouse - X: %d, Y: %d\n", x_movement, y_movement);// data to buffer
            data_buffer_size += data_written;   //updates data_buffer_size
            free_space -= data_written;    //recalculate the free_space
        }else{
            printk_ratelimited(KERN_WARNING "Mark's Driver - Data not added to the buffer due to lack of space\n");
        }
    }

    return 0;
}

// Initialize the HID driver
static struct hid_driver hid_driver_mouse_table = {
    .name = DEVICE_NAME,            //driver name
    .id_table = hid_mouse_table,    //defines all devices supported
    .probe = mouse_probe,           //called when mouse is plugged in
    .remove = mouse_remove,         //called when mouse is unplugged
    .raw_event = mouse_events,      //called when mouse does something
};

//Called when driver is Loaded
static int __init driver_loaded(void){

    //Register Hid usb device
    int status = hid_register_driver(&hid_driver_mouse_table);

    // ensures the HID registration worked
    if (status) {
        printk(KERN_ERR "Mark's Driver - HID registration failed with error code of %d\n", status);
        return status;  //return error code
    }

    //Create proc files
    create_proc();

    printk(KERN_NOTICE "Mark's Driver - Hello is Loaded! - Major Device Number: %d\n", MAJOR_NUMBER);

    return 0;   // Return 0 means success
}

//Called when driver is Unloaded
static void __exit driver_unload(void){
    hid_unregister_driver(&hid_driver_mouse_table);
    printk(KERN_INFO "Mark's Driver - Unregester %s\n", hid_driver_mouse_table.name);
    printk(KERN_NOTICE "Mark's Driver - Goodbye is Unloaded!\n");
}

// Register module entry and exit points
module_init(driver_loaded);
module_exit(driver_unload);

//Licence and other info
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mark");
MODULE_DESCRIPTION("Kernal module for Markitecture project");
MODULE_VERSION("1.0");
