#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/spinlock.h>
#include <linux/slab.h>

#define DRIVER_NAME "mychardev"
#define DEVICE_NAME "mychardev"
#define BUFFER_SIZE 1024

/* Module metadata */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Bhanu");
MODULE_DESCRIPTION("A simple 1KB buffer character device driver");
MODULE_VERSION("0.1");

static dev_t my_dev_number;
static struct class *my_class;
static struct cdev my_cdev;

static char my_buffer[BUFFER_SIZE];
static unsigned int my_buffer_len; /* Current amount of data in the buffer */
static spinlock_t my_buffer_lock;

/*
 * @brief Opens the device file.
 * @param inode Pointer to the inode structure.
 * @param file Pointer to the file structure.
 * @return 0 on success, or a negative error code.
 */
static int my_open(struct inode *inode, struct file *file)
{
	if (!try_module_get(THIS_MODULE))
		return -ENODEV; /* Failed to get module reference */

	printk(KERN_INFO "%s: Device opened\n", DEVICE_NAME);
	return 0;
}

/*
 * @brief Releases the device file.
 * @param inode Pointer to the inode structure.
 * @param file Pointer to the file structure.
 * @return 0 on success.
 */
static int my_release(struct inode *inode, struct file *file)
{
	module_put(THIS_MODULE);
	printk(KERN_INFO "%s: Device closed\n", DEVICE_NAME);
	return 0;
}

/*
 * @brief Reads data from the device buffer.
 * @param file Pointer to the file structure.
 * @param __user_buf Pointer to user space buffer.
 * @param count Number of bytes to read.
 * @param ppos Pointer to the file offset.
 * @return Number of bytes read, or a negative error code.
 */
static ssize_t my_read(struct file *file, char __user *__user_buf,
		       size_t count, loff_t *ppos)
{
	unsigned long flags;
	ssize_t bytes_read = 0;
	size_t bytes_to_copy;

	spin_lock_irqsave(&my_buffer_lock, flags);

	/* Check if read position is beyond current data length */
	if (*ppos >= my_buffer_len) {
		goto out; /* Return 0 (EOF) */
	}

	/* Calculate how many bytes can be copied to user */
	bytes_to_copy = min((size_t)count, (size_t)(my_buffer_len - *ppos));

	spin_unlock_irqrestore(&my_buffer_lock, flags);

	/* Copy data to user space */
	if (copy_to_user(__user_buf, my_buffer + *ppos, bytes_to_copy)) {
		printk(KERN_ERR "%s: Failed to copy data to user space.\n",
		       DEVICE_NAME);
		return -EFAULT;
	}

	*ppos += bytes_to_copy;
	bytes_read = bytes_to_copy;

	printk(KERN_INFO "%s: Read %zd bytes from offset %lld\n",
	       DEVICE_NAME, bytes_read, (long long)*ppos - bytes_read);

	return bytes_read;

out:
	spin_unlock_irqrestore(&my_buffer_lock, flags);
	return bytes_read;
}

/*
 * @brief Writes data to the device buffer.
 * @param file Pointer to the file structure.
 * @param __user_buf Pointer to user space buffer.
 * @param count Number of bytes to write.
 * @param ppos Pointer to the file offset.
 * @return Number of bytes written, or a negative error code.
 */
static ssize_t my_write(struct file *file, const char __user *__user_buf,
			size_t count, loff_t *ppos)
{
	unsigned long flags;
	ssize_t bytes_written = 0;
	size_t bytes_to_copy;
	unsigned int new_buffer_len;

	spin_lock_irqsave(&my_buffer_lock, flags);

	/* Check if write position is beyond buffer capacity */
	if (*ppos >= BUFFER_SIZE) {
		printk(KERN_WARNING "%s: Attempted to write beyond buffer capacity at offset %lld\n",
		       DEVICE_NAME, (long long)*ppos);
		bytes_written = -ENOSPC;
		goto out;
	}

	/* Calculate how many bytes can be copied into the buffer */
	bytes_to_copy = min((size_t)count, (size_t)(BUFFER_SIZE - *ppos));

	spin_unlock_irqrestore(&my_buffer_lock, flags);

	/* Copy data from user space */
	if (copy_from_user(my_buffer + *ppos, __user_buf, bytes_to_copy)) {
		printk(KERN_ERR "%s: Failed to copy data from user space.\n",
		       DEVICE_NAME);
		return -EFAULT;
	}

	spin_lock_irqsave(&my_buffer_lock, flags);
	*ppos += bytes_to_copy;
	bytes_written = bytes_to_copy;

	/* Update logical buffer length if data extends beyond current length */
	new_buffer_len = *ppos;
	if (new_buffer_len > my_buffer_len)
		my_buffer_len = new_buffer_len;

	spin_unlock_irqrestore(&my_buffer_lock, flags);

	printk(KERN_INFO "%s: Written %zd bytes to offset %lld\n",
	       DEVICE_NAME, bytes_written, (long long)*ppos - bytes_written);

	return bytes_written;

out:
	spin_unlock_irqrestore(&my_buffer_lock, flags);
	return bytes_written;
}

/* File operations structure */
static const struct file_operations my_fops = {
	.owner = THIS_MODULE,
	.open = my_open,
	.release = my_release,
	.read = my_read,
	.write = my_write,
	.llseek = no_llseek, /* Simple devices often don't need complex seek */
};

/*
 * @brief Initializes the character device driver.
 * @return 0 on success, or a negative error code.
 */
static int __init my_chardev_init(void)
{
	int ret;
	struct device *dev_ret;

	printk(KERN_INFO "%s: Initializing character device module.\n",
	       DRIVER_NAME);

	/* 1. Allocate a major/minor number region */
	ret = alloc_chrdev_region(&my_dev_number, 0, 1, DRIVER_NAME);
	if (ret < 0) {
		printk(KERN_ERR "%s: Failed to allocate char device region.\n",
		       DRIVER_NAME);
		return ret;
	}
	printk(KERN_INFO "%s: Allocated major %d, minor %d\n", DRIVER_NAME,
	       MAJOR(my_dev_number), MINOR(my_dev_number));

	/* 2. Initialize the cdev structure */
	cdev_init(&my_cdev, &my_fops);
	my_cdev.owner = THIS_MODULE;

	/* 3. Add the character device to the system */
	ret = cdev_add(&my_cdev, my_dev_number, 1);
	if (ret < 0) {
		printk(KERN_ERR "%s: Failed to add cdev.\n", DRIVER_NAME);
		goto unregister_region;
	}

	/* 4. Create a device class (for /sys/class) */
	my_class = class_create(THIS_MODULE, DRIVER_NAME);
	if (IS_ERR(my_class)) {
		printk(KERN_ERR "%s: Failed to create device class.\n",
		       DRIVER_NAME);
		ret = PTR_ERR(my_class);
		goto cdev_del;
	}

	/* 5. Create a device file in /dev */
	dev_ret = device_create(my_class, NULL, my_dev_number, NULL,
				DEVICE_NAME);
	if (IS_ERR(dev_ret)) {
		printk(KERN_ERR "%s: Failed to create device.\n", DRIVER_NAME);
		ret = PTR_ERR(dev_ret);
		goto class_destroy;
	}

	/* Initialize buffer and spinlock */
	memset(my_buffer, 0, BUFFER_SIZE);
	my_buffer_len = 0; /* No data in buffer initially */
	spin_lock_init(&my_buffer_lock);

	printk(KERN_INFO "%s: Module loaded successfully.\n", DRIVER_NAME);
	return 0;

class_destroy:
	class_destroy(my_class);
cdev_del:
	cdev_del(&my_cdev);
unregister_region:
	unregister_chrdev_region(my_dev_number, 1);
	return ret;
}

/*
 * @brief Cleans up and exits the character device driver.
 */
static void __exit my_chardev_exit(void)
{
	printk(KERN_INFO "%s: Exiting character device module.\n", DRIVER_NAME);

	/* 1. Destroy device file */
	device_destroy(my_class, my_dev_number);
	/* 2. Destroy device class */
	class_destroy(my_class);
	/* 3. Delete cdev */
	cdev_del(&my_cdev);
	/* 4. Unregister major/minor number */
	unregister_chrdev_region(my_dev_number, 1);

	printk(KERN_INFO "%s: Module unloaded successfully.\n", DRIVER_NAME);
}

module_init(my_chardev_init);
module_exit(my_chardev_exit);