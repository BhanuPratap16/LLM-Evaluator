#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>

#define DEVICE_NAME "simple_char_dev"
#define BUFFER_SIZE 1024 /* 1KB buffer */

/* Module metadata */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Bhanu");
MODULE_DESCRIPTION("A simple character device driver with 1KB buffer.");
MODULE_VERSION("0.1");

/* Global variables */
static dev_t dev_num;
static struct cdev *simple_cdev;
static char device_buffer[BUFFER_SIZE];
static loff_t current_buffer_data_size = 0; /* Tracks how much valid data is in the buffer */
static DEFINE_MUTEX(simple_char_dev_mutex); /* Protects buffer and data_size */

/*
 * simple_char_dev_open - Called when a device file is opened.
 * @inode: Inode structure of the device.
 * @file: File structure representing the opened file.
 *
 * Returns 0 on success, or a negative error code on failure.
 */
static int simple_char_dev_open(struct inode *inode, struct file *file)
{
	pr_info("%s opened\n", DEVICE_NAME);
	return 0;
}

/*
 * simple_char_dev_release - Called when the last file descriptor for the device is closed.
 * @inode: Inode structure of the device.
 * @file: File structure representing the opened file.
 *
 * Returns 0 on success, or a negative error code on failure.
 */
static int simple_char_dev_release(struct inode *inode, struct file *file)
{
	pr_info("%s closed\n", DEVICE_NAME);
	return 0;
}

/*
 * simple_char_dev_read - Reads data from the device.
 * @file: File structure representing the opened file.
 * @buf: User buffer to copy data into.
 * @count: Number of bytes to read.
 * @ppos: Pointer to the current file offset.
 *
 * Returns the number of bytes read on success, or a negative error code on failure.
 */
static ssize_t simple_char_dev_read(struct file *file, char __user *buf,
				    size_t count, loff_t *ppos)
{
	ssize_t bytes_read = 0;
	size_t bytes_to_transfer;

	if (mutex_lock_interruptible(&simple_char_dev_mutex))
		return -ERESTARTSYS;

	/* If current offset is beyond the data size, nothing to read */
	if (*ppos >= current_buffer_data_size) {
		goto unlock_and_return_zero;
	}

	/* Determine how many bytes can actually be read */
	// Calculate available data from current offset to end of valid data
	bytes_to_transfer = current_buffer_data_size - *ppos;

	// Limit the user's requested count to SSIZE_MAX to ensure it fits in ssize_t
	// when being compared or assigned.
	if (count > SSIZE_MAX) {
		count = SSIZE_MAX;
	}

	// Limit by the user's requested count (which is now capped at SSIZE_MAX)
	if (count < bytes_to_transfer) {
		bytes_to_transfer = count;
	}

	/*
	 * At this point, bytes_to_transfer is a size_t value that is
	 * guaranteed to be <= BUFFER_SIZE (1024) and also <= SSIZE_MAX
	 * (because 'count' was capped).
	 * Therefore, the cast to ssize_t below is safe and will not result
	 * in narrowing unless SSIZE_MAX were smaller than 1024, which is not typical.
	 */

	/* Copy data from kernel buffer to user space */
	if (copy_to_user(buf, device_buffer + *ppos, bytes_to_transfer)) {
		bytes_read = -EFAULT; /* Bad address */
		goto unlock_and_return;
	}

	bytes_read = (ssize_t)bytes_to_transfer; // Assign the safely determined value to ssize_t
	*ppos += bytes_read; /* Update file offset */

	pr_info("Read %ld bytes from %s at offset %lld\n", (long)bytes_read,
		DEVICE_NAME, (long long)*ppos - bytes_read);

unlock_and_return:
	mutex_unlock(&simple_char_dev_mutex);
	return bytes_read;

unlock_and_return_zero:
	mutex_unlock(&simple_char_dev_mutex);
	return 0;
}

/*
 * simple_char_dev_write - Writes data to the device.
 * @file: File structure representing the opened file.
 * @buf: User buffer to copy data from.
 * @count: Number of bytes to write.
 * @ppos: Pointer to the current file offset.
 *
 * Returns the number of bytes written on success, or a negative error code on failure.
 */
static ssize_t simple_char_dev_write(struct file *file, const char __user *buf,
				     size_t count, loff_t *ppos)
{
	ssize_t bytes_written = 0;
	size_t bytes_to_transfer;

	if (mutex_lock_interruptible(&simple_char_dev_mutex))
		return -ERESTARTSYS;

	/* If current offset is beyond the buffer capacity, no more space */
	if (*ppos >= BUFFER_SIZE) {
		bytes_written = -ENOSPC; /* No space left on device */
		goto unlock_and_return;
	}

	/* Determine how many bytes can actually be written */
	// Calculate available space from current offset to end of buffer
	bytes_to_transfer = BUFFER_SIZE - *ppos;

	// Limit the user's requested count to SSIZE_MAX to ensure it fits in ssize_t
	// when being compared or assigned.
	if (count > SSIZE_MAX) {
		count = SSIZE_MAX;
	}

	// Limit by the user's requested count (which is now capped at SSIZE_MAX)
	if (count < bytes_to_transfer) {
		bytes_to_transfer = count;
	}

	/*
	 * At this point, bytes_to_transfer is a size_t value that is
	 * guaranteed to be <= BUFFER_SIZE (1024) and also <= SSIZE_MAX
	 * (because 'count' was capped).
	 * Therefore, the cast to ssize_t below is safe.
	 */

	/* Copy data from user space to kernel buffer */
	if (copy_from_user(device_buffer + *ppos, buf, bytes_to_transfer)) {
		bytes_written = -EFAULT; /* Bad address */
		goto unlock_and_return;
	}

	bytes_written = (ssize_t)bytes_to_transfer; // Assign the safely determined value to ssize_t
	*ppos += bytes_written; /* Update file offset */

	/* Update the actual amount of data present in the buffer */
	if (*ppos > current_buffer_data_size)
		current_buffer_data_size = *ppos;

	pr_info("Written %ld bytes to %s at offset %lld\n", (long)bytes_written,
		DEVICE_NAME, (long long)*ppos - bytes_written);

unlock_and_return:
	mutex_unlock(&simple_char_dev_mutex);
	return bytes_written;
}

/* File operations structure */
static const struct file_operations simple_char_dev_fops = {
	.owner = THIS_MODULE,
	.open = simple_char_dev_open,
	.release = simple_char_dev_release,
	.read = simple_char_dev_read,
	.write = simple_char_dev_write,
};

/*
 * simple_char_dev_init - Module initialization function.
 *
 * Registers the character device.
 * Returns 0 on success, or a negative error code on failure.
 */
static int __init simple_char_dev_init(void)
{
	int ret;

	pr_info("Initializing %s module...\n", DEVICE_NAME);

	/* Allocate a major/minor number for the device */
	ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
	if (ret < 0) {
		pr_err("Failed to allocate char device region\n");
		return ret;
	}

	pr_info("Allocated device number: Major = %d, Minor = %d\n",
		MAJOR(dev_num), MINOR(dev_num));

	/* Allocate cdev structure */
	simple_cdev = cdev_alloc();
	if (!simple_cdev) {
		pr_err("Failed to allocate cdev structure\n");
		ret = -ENOMEM;
		goto unregister_region;
	}

	/* Initialize cdev and add it to the kernel */
	cdev_init(simple_cdev, &simple_char_dev_fops);
	simple_cdev->owner = THIS_MODULE;

	ret = cdev_add(simple_cdev, dev_num, 1);
	if (ret < 0) {
		pr_err("Failed to add cdev\n");
		goto destroy_cdev;
	}

	/* Initialize the mutex */
	mutex_init(&simple_char_dev_mutex);

	pr_info("%s module loaded successfully\n", DEVICE_NAME);
	return 0;

destroy_cdev:
	/* No need to free simple_cdev, cdev_del handles its embedded kobject */
	cdev_del(simple_cdev);
unregister_region:
	unregister_chrdev_region(dev_num, 1);
	return ret;
}

/*
 * simple_char_dev_exit - Module exit function.
 *
 * Unregisters the character device.
 */
static void __exit simple_char_dev_exit(void)
{
	pr_info("Exiting %s module...\n", DEVICE_NAME);

	/* Delete cdev and unregister device number */
	if (simple_cdev)
		cdev_del(simple_cdev);
	unregister_chrdev_region(dev_num, 1);

	pr_info("%s module unloaded\n", DEVICE_NAME);
}

module_init(simple_char_dev_init);
module_exit(simple_char_dev_exit);