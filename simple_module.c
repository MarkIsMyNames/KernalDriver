#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/delay.h>


#define DEVNR 511   //Major device number
#define DEVNRNAME "simple_module"   //Name that will show in /proc/devices/

static struct cdev my_cdev;     //character device
static char buffer[256];       //buffer to read and write to

//Read file operation
static ssize_t my_read (struct file *file, char __user *user_buffer, size_t length, loff_t *offset){
    int not_copied, to_copy = (length < 256) ? length : 256; //copy size length or 256 bytes (whichever is smaller)

    printk(KERN_INFO "Mark's Driver - Read is called, *off: %lld\n", *offset);   //Print if the file is being read

    // Check if the file was opened with read permissions.
    // FMODE_READ flag indicates the file was opened for reading.
    if (!(file->f_mode & FMODE_READ)) {
        printk(KERN_WARNING "Mark's Driver - Read attempted without read permissions\n");
        return -EACCES; // Return error code for permission denied
    }

    if(*offset >= to_copy){
        return 0;   //return if the offset is bigger than or equal to copy
    }

    if((not_copied = copy_to_user(user_buffer, buffer, to_copy))){    //returns the amount of bytes not copied
        printk(KERN_ERR "Mark's Driver - Could not read - Reason unknown\n");
        return -EFAULT;
    }

    printk(KERN_INFO "Mark's Driver - Read: %d bytes\n", (to_copy - not_copied));   //Print if the file is being read

    *offset += (to_copy - not_copied);  //increment offset by the number of bytes read already

    return to_copy - not_copied;  //number of bytes read successfully
}

//Write file operation
static ssize_t my_write (struct file *file, const char __user *user_buffer, size_t length, loff_t *offset){
    int not_copied, to_copy = (length < 256) ? length : 256; //copy length or 256 bytes (whichever is smaller)

    printk(KERN_INFO "Mark's Driver - Write is called\n");   //Print if the file is being written to

    // Check if the file was opened with write permissions.
    // FMODE_WRITE flag indicates the file was opened for writing.
    if (!(file->f_mode & FMODE_WRITE)) {
        printk(KERN_WARNING "Mark's Driver - Write attempted without write permissions\n");
        return -EACCES; // Return error code for permission denied
    }

    if((not_copied = copy_to_user(buffer, user_buffer, to_copy))){    //returns the amount of bytes not copied
        printk(KERN_ERR "Mark's Driver - Could not read - Reason unknown\n");
        return -EFAULT;
    }

    printk(KERN_INFO "Mark's Driver - Wrote: %d bytes\n", (to_copy - not_copied));   //Print if the file is being Written to

    return to_copy - not_copied;  //number of bytes read successfully
}

//Open file operation
static int my_open(struct inode *inode, struct file *filp){
    printk(KERN_INFO "Mark's Driver - Major: %d, Minor: %d\n", imajor(inode), iminor(inode));  //print out the major and minor device number

    printk(KERN_INFO "Mark's Driver - filp->f_pos: %lld\n", filp->f_pos);    //print file position
    printk(KERN_INFO "Mark's Driver - filp->f_mode: %x\n", filp->f_mode);    //print out permissions
    printk(KERN_INFO "Mark's Driver - filp->f_flags %x\n", filp->f_flags);   //print out the flags

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
        ssleep(t_nr);//sleep
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

    thread1 = kthread_run(thread_function, &t2, "thread2");//create thread 2
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
    kthread_stop(thread1);//stop thread 1
    kthread_stop(thread2);//stop thread 2
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
