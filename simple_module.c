#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/jiffies.h>
#include <linux/timer.h>

static int major; //stores dynamically alloccated major number

//Read file operation
static ssize_t my_read (struct file *file, char __user *user, size_t length, loff_t *offset){
    printk(KERN_INFO "Mark's Driver - Read is called\n");   //Print if the file is being read

    return 0;   // Return 0 means success
}

//Open file operation
static int my_open(struct inode *inode, struct file *filp){
    printk(KERN_INFO "Mark's Driver - Major: %d, Minor: %d", imajor(inode), iminor(inode));  //print out the major and minor device number

    printk(KERN_INFO "Mark's Driver - filp->f_pos: %lld", filp->f_pos);    //print file position
    printk(KERN_INFO "Mark's Driver - filp->f_mode: %d\n", filp->f_mode);    //print out permissions
    printk(KERN_INFO "Mark's Driver - filp->f_flags %lld", filp->f_flags);   //print out the flags

    return 0;   // Return 0 means success
}

//Close File
static int my_close(struct inode *inode, struct file *filp){

    printk(KERN_INFO "Mark's Driver - File is closed");   //print out when file closed

    return 0;   // Return 0 means success
}

//file operations supported by device
static struct file_operations fops ={
    .read = my_read,
    .open = my_open,
    .release = my_close,
};

//Called when driver is Loaded
static int __init driverLoaded(void){

    major = register_chrdev(0, "simple_module", &fops); //Register device

    if(major < 0){
        printk(KERN_ERR "Mark's Driver - Error register chrdev\n");
        return major;   //return error code
    }

    printk(KERN_INFO "Mark's Driver is Loaded! - Major Device Number: %d\n", major);

    return 0;   // Return 0 means success
}

//Called when driver is Unloaded
static void __exit driverUnload(void){
    unregister_chrdev(major, "simple_module");    //Unegister device
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
