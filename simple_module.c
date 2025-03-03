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

#define DEVNR 511                 // Major device number
#define DEVNRNAME "Marks_Driver"  // Name that will show in /proc/devices/
#define BUFFER_SIZE 256

static struct cdev my_cdev;       // Character device
static char buffer[BUFFER_SIZE];  // Buffer for read/write

//Synchronization objects
static DEFINE_MUTEX(my_mutex); //mutex to ensure that only one thread can access the critical section
static DECLARE_WAIT_QUEUE_HEAD(read_q); // A wait queue for read operations
static DECLARE_WAIT_QUEUE_HEAD(write_q); //  wait queue for write operations

// Flag to indicate if there is data available in the buffer.
//   0 => Buffer is empty (space available for write)
//   1 => Buffer is full (data available for read)
static int data_ready = 0;

//Read file operation
static ssize_t my_read (struct file *file, char __user *user_buffer, size_t length, loff_t *offset){

    int to_copy;
    int not_copied;

    printk(KERN_INFO "Mark's Driver - Read is called, *off: %lld\n", *offset);   //Print if the file is being read

    // Check if the file was opened with read permissions.
    // FMODE_READ flag indicates the file was opened for reading.
    if (!(file->f_mode & FMODE_READ)) {
        printk(KERN_WARNING "Mark's Driver - Read attempted without read permissions\n");
        return -EACCES; // Return error code for permission denied
    }

    // Sleep until there is data available in the buffer
    if (wait_event_interruptible(read_q, data_ready != 0)){
        printk(KERN_INFO "Mark's Driver - Thread for Read Sleeping");
        return -ERESTARTSYS;
    }

    // Lock the mutex to protect access to the shared buffer and data_ready flag
    if (mutex_lock_interruptible(&my_mutex)){
        return -ERESTARTSYS;
    }

    to_copy = (length < BUFFER_SIZE) ? length : BUFFER_SIZE;    //copy size length or BUFFER_SIZE bytes (whichever is smaller)

    if(*offset >= to_copy){
        mutex_unlock(&my_mutex);
        return 0;   //return if the offset is bigger than or equal to copy
    }

    //Stupid assignment in an if statement. Works but very dumb
    if((not_copied = copy_to_user(user_buffer, buffer, to_copy))){    //returns the amount of bytes not copied
        mutex_unlock(&my_mutex);
        printk(KERN_ERR "Mark's Driver - Could not read - Reason unknown\n");
        return -EFAULT;
    }

    printk(KERN_INFO "Mark's Driver - Read: %d bytes\n", (to_copy - not_copied));   //Print if the file is being read
    *offset += (to_copy - not_copied);  //increment offset by the number of bytes read already

    // Unlock the mutex
    mutex_unlock(&my_mutex);

    // mark the buffer as free
    data_ready = 0;

    // Wake up any waiting writer processes
    wake_up_interruptible(&write_q);

    return to_copy - not_copied;  //number of bytes read successfully
}

//Write file operation
static ssize_t my_write (struct file *file, const char __user *user_buffer, size_t length, loff_t *offset){

    int to_copy;
    int not_copied;

    printk(KERN_INFO "Mark's Driver - Write is called\n");

    // Check if the file was opened with write permissions.
    // FMODE_WRITE flag indicates the file was opened for writing.
    if (!(file->f_mode & FMODE_WRITE)) {
        printk(KERN_WARNING "Mark's Driver - Write attempted without write permissions\n");
        return -EACCES; // Return error code for permission denied
    }

    /* Block until buffer is free (i.e., data_ready is 0) */
    if (wait_event_interruptible(write_q, data_ready == 0)){
        return -ERESTARTSYS;
    }

    //lock the mutex
    if (mutex_lock_interruptible(&my_mutex)){
        return -ERESTARTSYS;
    }

    to_copy = (length < BUFFER_SIZE) ? length : BUFFER_SIZE;    //copy length or BUFFER_SIZE bytes (whichever is smaller)


    if((not_copied = copy_to_user(buffer, user_buffer, to_copy))){    //returns the amount of bytes not copied
        mutex_unlock(&my_mutex);
        printk(KERN_ERR "Mark's Driver - Could not read - Reason unknown\n");
        return -EFAULT;
    }

    printk(KERN_INFO "Mark's Driver - Wrote: %d bytes\n", (to_copy - not_copied));   //Print if the file is being Written to

    // Unlock the mutex after writing
    mutex_unlock(&my_mutex);

    // Set the flag indicating that new data is available in the buffer.
    data_ready = 1;

    // Wake up any reader processes waiting for data
    wake_up_interruptible(&read_q);

    return to_copy - not_copied;  //number of bytes read successfully
}

//Open file operation
static int my_open(struct inode *inode, struct file *filp){
    printk(KERN_INFO "Mark's Driver - Major: %d, Minor: %d\n", imajor(inode), iminor(inode));  //print out the major and minor device number

    printk(KERN_INFO "Mark's Driver - filp->f_pos: %lld\n", filp->f_pos);    //print file position
    printk(KERN_INFO "Mark's Driver - filp->f_mode: %d\n", filp->f_mode);    //print out permissions
    printk(KERN_INFO "Mark's Driver - filp->f_flags %d\n", filp->f_flags);   //print out the flags

    return 0;   // Return 0 means success
}

//Close File
static int my_close(struct inode *inode, struct file *filp){

    printk(KERN_INFO "Mark's Driver - File is closed\n");   //print out when file closed

    return 0;   // Return 0 means success
}

//file operations supported by device driver
static struct file_operations fops ={
    .read = my_read,
    .open = my_open,
    .release = my_close,
    .write = my_write,
};

//Some global variables for the threads
static struct task_struct *thread1;
static struct task_struct *thread2;
static int t1 = 1, t2 = 2;

/**
 * @brief Function excucuted by threads
 *
 * @param thread_nr pointer to number of thread
 */
static int thread_function(void *thread_nr){

    unsigned int i = 0; //counter
    int t_nr = *(int *) thread_nr; //thread number

    //Loop while working
    while(!kthread_should_stop()){
        printk(KERN_INFO "Mark's Driver - Thread %d is executing! Counter = %d\n", t_nr, i++);
        ssleep(t_nr*10);//sleep
    }

    printk(KERN_INFO "Mark's Driver - Thread %d finished execution\n", t_nr);
    return 0;   // Return 0 means success

}

//Called when driver is Loaded
static int __init driverLoaded(void){

    int status; //needed for error checking
    dev_t devnr = MKDEV(DEVNR,0);   //device number

    status = register_chrdev_region(devnr, 1, DEVNRNAME);   //register device number
    if(status < 0){
        printk(KERN_ERR "Mark's Driver - Error registering device number!\n");
        return status;   //return error code
    }

    cdev_init(&my_cdev, &fops);     //initilaze character device
    my_cdev.owner = THIS_MODULE;    //set owner field

    status = cdev_add(&my_cdev, devnr, 1);
    if(status < 0){
        printk(KERN_ERR "Mark's Driver - Error adding cdev!\n");
        unregister_chrdev_region(devnr, 1); //unregister device number
        return status;   //return error code
    }

    printk(KERN_INFO "Mark's Driver is Loaded! - Major Device Number: %d\n", DEVNR);

    printk(KERN_INFO "Mark's Driver - Creating kthreads\n");
    thread1 = kthread_run(thread_function, &t1, "thread1");//create thread 1
    if(thread1 != NULL){
        printk(KERN_INFO "Mark's Driver - Thread 1 was created\n");
    }else{
        printk(KERN_WARNING "Mark's Driver - Thread 1 could not be created\n");
        return -1;
    }

    thread2 = kthread_run(thread_function, &t2, "thread2");//create thread 2
    if(thread2 != NULL){
        printk(KERN_INFO "Mark's Driver - Thread 2 was created\n");
    }else{
        printk(KERN_WARNING "Mark's Driver - Thread 2 could not be created\n");
        kthread_stop(thread1);//stop thread 1
        return -1;
    }
    printk(KERN_INFO "Mark's Driver - Both threads are running now\n");

    return 0;   // Return 0 means success
}

//Called when driver is Unloaded
static void __exit driverUnload(void){
    dev_t devnr = MKDEV(DEVNR,0);   //device number
    unregister_chrdev_region(devnr,1); //unregister device number
    printk(KERN_INFO "Mark's Driver - Device Number Unregister\n");

    // Stop both threads
    if (thread1) {
        kthread_stop(thread1);
    }
    if (thread2) {
        kthread_stop(thread2);
    }

    cdev_del(&my_cdev);
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
