#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/string.h> /* For memset */

#define DRIVER_NAME "mychardev"
#define DEVICE_NAME "mychardev"
#define BUFFER_SIZE 1024

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Bhanu");
MODULE_DESCRIPTION("A simple character device driver with 1KB buffer.");

static dev_t mychardev_dev_num;
static struct cdev mychardev_cdev;
static struct class *mychardev_class;
static char mychardev_buffer[BUFFER_SIZE];
static DEFINE_MUTEX(mychardev_mutex); /* Protects buffer access */

/*
 * mychardev_open: Called when a process tries to open the device file.
 */
static int mychardev_open(struct inode *inode, struct file *file)
{
	printk(KERN_INFO "%s: Device opened.\n", DRIVER_NAME);
	return 0;
}

/*
 * mychardev_release: Called when a process closes the device file.
 */
static int mychardev_release(struct inode *inode, struct file *file)
{
	printk(KERN_INFO "%s: Device closed.\n", DRIVER_NAME);
	return 0;
}

/*
 * mychardev_read: Called when a process tries to read from the device file.
 */
static ssize_t mychardev_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
{
	ssize_t bytes_read = 0;
	loff_t avail_bytes;

	mutex_lock(&mychardev_mutex);

	if (*pos >= BUFFER_SIZE) {
		goto out;
	}

	/* Adjust count to not read beyond buffer size */
	// Calculate available bytes to read
	avail_bytes = (loff_t)BUFFER_SIZE - *pos; // Explicitly cast BUFFER_SIZE to loff_t
	if (count > (size_t)avail_bytes) {
		count = (size_t)avail_bytes;
	}

	if (copy_to_user(buf, mychardev_buffer + *pos, count)) {
		bytes_read = -EFAULT;
		goto out;
	}

	*pos += count;
	bytes_read = (ssize_t)count;

out:
	mutex_unlock(&mychardev_mutex);
	printk(KERN_INFO "%s: Read %zd bytes.\n", DRIVER_NAME, bytes_read);
	return bytes_read;
}

/*
 * mychardev_write: Called when a process tries to write to the device file.
 */
static ssize_t mychardev_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
	ssize_t bytes_written = 0;
	loff_t avail_space;

	mutex_lock(&mychardev_mutex);

	if (*pos >= BUFFER_SIZE) {
		goto out;
	}

	/* Adjust count to not write beyond buffer size */
	// Calculate available space to write
	avail_space = (loff_t)BUFFER_SIZE - *pos; // Explicitly cast BUFFER_SIZE to loff_t
	if (count > (size_t)avail_space) {
		count = (size_t)avail_space;
	}

	if (copy_from_user(mychardev_buffer + *pos, buf, count)) {
		bytes_written = -EFAULT;
		goto out;
	}

	*pos += count;
	bytes_written = (ssize_t)count;

out:
	mutex_unlock(&mychardev_mutex);
	printk(KERN_INFO "%s: Written %zd bytes.\n", DRIVER_NAME, bytes_written);
	return bytes_written;
}

/* File operations structure */
static const struct file_operations mychardev_fops = {
	.owner = THIS_MODULE,
	.open = mychardev_open,
	.release = mychardev_release,
	.read = mychardev_read,
	.write = mychardev_write,
	.llseek = noop_llseek,
};

/*
 * mychardev_init: Module initialization function.
 */
static int __init mychardev_init(void)
{
	int ret;

	printk(KERN_INFO "%s: Initializing...\n", DRIVER_NAME);

	/* Allocate a major/minor number */
	ret = alloc_chrdev_region(&mychardev_dev_num, 0, 1, DRIVER_NAME);
	if (ret < 0) {
		printk(KERN_ERR "%s: Failed to allocate char device region.\n", DRIVER_NAME);
		return ret;
	}

	printk(KERN_INFO "%s: Allocated device number %d:%d\n", DRIVER_NAME,
	       MAJOR(mychardev_dev_num), MINOR(mychardev_dev_num));

	/* Initialize the cdev structure */
	cdev_init(&mychardev_cdev, &mychardev_fops);
	mychardev_cdev.owner = THIS_MODULE;

	/* Add the character device to the system */
	ret = cdev_add(&mychardev_cdev, mychardev_dev_num, 1);
	if (ret < 0) {
		printk(KERN_ERR "%s: Failed to add cdev.\n", DRIVER_NAME);
		unregister_chrdev_region(mychardev_dev_num, 1);
		return ret;
	}

	/* Create device class and device file for udev */
	mychardev_class = class_create(DEVICE_NAME);
	if (IS_ERR(mychardev_class)) {
		printk(KERN_ERR "%s: Failed to create device class.\n", DRIVER_NAME);
		cdev_del(&mychardev_cdev);
		unregister_chrdev_region(mychardev_dev_num, 1);
		return (int)PTR_ERR(mychardev_class);
	}

	if (IS_ERR(device_create(mychardev_class, NULL, mychardev_dev_num, NULL, DEVICE_NAME))) {
		printk(KERN_ERR "%s: Failed to create device.\n", DRIVER_NAME);
		class_destroy(mychardev_class);
		cdev_del(&mychardev_cdev);
		unregister_chrdev_region(mychardev_dev_num, 1);
		return (int)PTR_ERR(mychardev_class);
	}

	/* Initialize buffer with zeros */
	memset(mychardev_buffer, 0, (size_t)BUFFER_SIZE); // Explicitly cast BUFFER_SIZE to size_t

	printk(KERN_INFO "%s: Module loaded.\n", DRIVER_NAME);
	return 0;
}

/*
 * mychardev_exit: Module exit function.
 */
static void __exit mychardev_exit(void)
{
	printk(KERN_INFO "%s: Exiting...\n", DRIVER_NAME);

	device_destroy(mychardev_class, mychardev_dev_num);
	class_destroy(mychardev_class);
	cdev_del(&mychardev_cdev);
	unregister_chrdev_region(mychardev_dev_num, 1);

	printk(KERN_INFO "%s: Module unloaded.\n", DRIVER_NAME);
}

module_init(mychardev_init);
module_exit(mychardev_exit);