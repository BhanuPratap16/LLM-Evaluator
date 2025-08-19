#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h> /* For kmalloc/kfree */
#include <linux/uaccess.h> /* For copy_to_user/copy_from_user */
#include <linux/mutex.h> /* For mutex */
#include <linux/init.h> /* For __init and __exit */
#include <linux/string.h> /* For memset */

#define DEVICE_NAME "mychardev"
#define MY_CHAR_DEV_BUF_SIZE 1024 /* 1KB buffer */

/*
 * Module information
 */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Bhanu");
MODULE_DESCRIPTION("A simple character device driver with 1KB buffer.");
MODULE_VERSION("0.1");

/*
 * Device structure
 */
struct my_device {
	struct cdev cdev;
	char *buffer;
	size_t buffer_len; /* Current amount of data in the buffer */
	struct mutex dev_mutex; /* Mutex for synchronizing access to the buffer */
	dev_t dev_num;
};

static struct my_device *my_dev;
static int major_number;

/*
 * Device open operation
 */
static int my_char_dev_open(struct inode *inode, struct file *file)
{
	struct my_device *dev;

	dev = container_of(inode->i_cdev, struct my_device, cdev);
	file->private_data = dev; /* Store device structure in file's private data */

	if (mutex_lock_interruptible(&dev->dev_mutex))
		return -ERESTARTSYS;

	pr_info("%s: device opened.\n", DEVICE_NAME);
	return 0;
}

/*
 * Device release operation
 */
static int my_char_dev_release(struct inode *inode, struct file *file)
{
	struct my_device *dev = file->private_data;

	mutex_unlock(&dev->dev_mutex);

	pr_info("%s: device closed.\n", DEVICE_NAME);
	return 0;
}

/*
 * Device read operation
 */
static ssize_t my_char_dev_read(struct file *file, char __user *buf,
				size_t count, loff_t *ppos)
{
	struct my_device *dev = file->private_data;
	ssize_t bytes_read = 0;
	size_t data_to_read;

	if (*ppos >= dev->buffer_len) /* Reached end of current data */
		return 0;

	/* Determine how much data can actually be read */
	data_to_read = dev->buffer_len - *ppos;
	if (count < data_to_read)
		data_to_read = count;

	/* Copy data from kernel buffer to user buffer */
	if (copy_to_user(buf, dev->buffer + *ppos, data_to_read)) {
		pr_err("%s: Failed to copy data to user space.\n", DEVICE_NAME);
		return -EFAULT;
	}

	*ppos += data_to_read;
	bytes_read = data_to_read;

	pr_info("%s: read %zd bytes (offset: %lld).\n", DEVICE_NAME, bytes_read, *ppos);
	return bytes_read;
}

/*
 * Device write operation
 */
static ssize_t my_char_dev_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	struct my_device *dev = file->private_data;
	ssize_t bytes_written = 0;
	size_t space_available;

	if (*ppos >= MY_CHAR_DEV_BUF_SIZE) /* Buffer is full or offset beyond limit */
		return -ENOSPC; /* No space left on device */

	/* Determine how much space is available */
	space_available = MY_CHAR_DEV_BUF_SIZE - *ppos;
	if (count > space_available)
		count = space_available; /* Write only what fits */

	/* Copy data from user buffer to kernel buffer */
	if (copy_from_user(dev->buffer + *ppos, buf, count)) {
		pr_err("%s: Failed to copy data from user space.\n", DEVICE_NAME);
		return -EFAULT;
	}

	*ppos += count;
	bytes_written = count;

	/* Update actual data length if writing beyond current buffer_len */
	if (*ppos > dev->buffer_len)
		dev->buffer_len = *ppos;

	pr_info("%s: written %zd bytes (offset: %lld, current buffer_len: %zu).\n",
		DEVICE_NAME, bytes_written, *ppos, dev->buffer_len);
	return bytes_written;
}

/*
 * File operations structure
 */
static const struct file_operations my_char_dev_fops = {
	.owner = THIS_MODULE,
	.open = my_char_dev_open,
	.release = my_char_dev_release,
	.read = my_char_dev_read,
	.write = my_char_dev_write,
};

/*
 * Module initialization function
 */
static int __init my_char_dev_init(void)
{
	int ret;

	pr_info("%s: Initializing character device module.\n", DEVICE_NAME);

	/* 1. Allocate device structure */
	my_dev = kmalloc(sizeof(*my_dev), GFP_KERNEL);
	if (!my_dev) {
		pr_err("%s: Failed to allocate device structure.\n", DEVICE_NAME);
		return -ENOMEM;
	}
	memset(my_dev, 0, sizeof(*my_dev)); /* Clear the structure */

	/* 2. Allocate buffer */
	my_dev->buffer = kmalloc(MY_CHAR_DEV_BUF_SIZE, GFP_KERNEL);
	if (!my_dev->buffer) {
		pr_err("%s: Failed to allocate device buffer.\n", DEVICE_NAME);
		ret = -ENOMEM;
		goto free_dev_struct;
	}
	my_dev->buffer_len = 0; /* Initially, buffer is empty */
	memset(my_dev->buffer, 0, MY_CHAR_DEV_BUF_SIZE); /* Clear the buffer */

	/* 3. Initialize mutex */
	mutex_init(&my_dev->dev_mutex);

	/* 4. Allocate major and minor numbers */
	ret = alloc_chrdev_region(&my_dev->dev_num, 0, 1, DEVICE_NAME);
	if (ret < 0) {
		pr_err("%s: Failed to allocate character device region, error %d.\n",
			DEVICE_NAME, ret);
		goto free_buffer;
	}
	major_number = MAJOR(my_dev->dev_num);
	pr_info("%s: Allocated major number %d.\n", DEVICE_NAME, major_number);

	/* 5. Initialize cdev and add it to the system */
	cdev_init(&my_dev->cdev, &my_char_dev_fops);
	my_dev->cdev.owner = THIS_MODULE;

	ret = cdev_add(&my_dev->cdev, my_dev->dev_num, 1);
	if (ret < 0) {
		pr_err("%s: Failed to add cdev, error %d.\n", DEVICE_NAME, ret);
		goto unregister_region;
	}

	pr_info("%s: Character device module loaded successfully. (major %d)\n",
		DEVICE_NAME, major_number);
	return 0;

unregister_region:
	unregister_chrdev_region(my_dev->dev_num, 1);
free_buffer:
	kfree(my_dev->buffer);
free_dev_struct:
	kfree(my_dev);
	return ret;
}

/*
 * Module exit function
 */
static void __exit my_char_dev_exit(void)
{
	pr_info("%s: Exiting character device module.\n", DEVICE_NAME);

	if (my_dev) {
		/* Delete cdev */
		cdev_del(&my_dev->cdev);

		/* Unregister character device region */
		unregister_chrdev_region(my_dev->dev_num, 1);

		/* Free buffer */
		kfree(my_dev->buffer);

		/* Free device structure */
		kfree(my_dev);
	}

	pr_info("%s: Character device module unloaded.\n", DEVICE_NAME);
}

module_init(my_char_dev_init);
module_exit(my_char_dev_exit);