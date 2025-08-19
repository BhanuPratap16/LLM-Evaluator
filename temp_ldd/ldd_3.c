#include <linux/module.h>
#include <linux/fs.h>       /* For file_operations, register_chrdev_region */
#include <linux/cdev.h>     /* For cdev, cdev_init, cdev_add, cdev_del */
#include <linux/device.h>   /* For class_create, device_create, device_destroy, class_destroy */
#include <linux/uaccess.h>  /* For copy_to_user, copy_from_user */
#include <linux/slab.h>     /* For kmalloc, kfree */
#include <linux/mutex.h>    /* For mutex */
#include <linux/types.h>    /* For size_t, ssize_t */
#include <linux/err.h>      /* For IS_ERR, PTR_ERR */
#include <linux/kernel.h>   /* For pr_info, pr_err, pr_warn and min_t */

#define DEVICE_NAME "simple_char_dev"
#define CLASS_NAME  "simple_char_class"
#define BUFFER_SIZE (1UL * 1024UL) /* 1KB buffer, defined as unsigned long to prevent narrowing warnings */

/*
 * Global variables, static to limit their scope to this file.
 */
static dev_t simple_char_dev_nr;
static struct class *simple_char_dev_class;
static struct cdev simple_char_cdev;

static char *simple_char_buffer;
/* Stores the maximum extent of data written into the buffer.
 * Read operations will not go beyond this length.
 * Write operations can extend this length up to BUFFER_SIZE.
 */
static size_t simple_char_buffer_data_len;
static DEFINE_MUTEX(simple_char_buffer_mutex); /* Protects buffer and data_len */

/*
 * The device open callback function.
 */
static int simple_char_open(struct inode *inode, struct file *file)
{
    /* No special operations needed for open for this simple driver,
     * as the buffer is global and initialized once.
     */
    pr_info("%s: Device opened\n", DEVICE_NAME);
    return 0;
}

/*
 * The device release (close) callback function.
 */
static int simple_char_release(struct inode *inode, struct file *file)
{
    pr_info("%s: Device closed\n", DEVICE_NAME);
    return 0;
}

/*
 * The device read callback function.
 * @file: Pointer to the file structure.
 * @buffer: Pointer to the user buffer to copy data to.
 * @len: The number of bytes the user wants to read.
 * @offset: The current read/write offset within the device.
 *
 * Returns the number of bytes read on success, 0 for EOF, or an error code on failure.
 */
static ssize_t simple_char_read(struct file *file, char __user *buffer,
                                size_t len, loff_t *offset)
{
    ssize_t bytes_read = 0;
    loff_t bytes_to_copy_ll; // Use loff_t for calculations with *offset

    /* Acquire mutex to protect access to the shared buffer and its length. */
    mutex_lock(&simple_char_buffer_mutex);

    /* If the requested offset is beyond the current data length, we are at EOF.
     * Cast simple_char_buffer_data_len to loff_t for safe comparison with *offset.
     */
    if (*offset >= (loff_t)simple_char_buffer_data_len) {
        goto out; /* Return 0 bytes, indicating EOF */
    }

    /*
     * Calculate how many bytes can actually be copied:
     * It's the minimum of:
     * 1. The requested length (len), cast to loff_t.
     * 2. The available data from the current offset to the end of actual data,
     *    calculated using loff_t for consistency.
     */
    bytes_to_copy_ll = (loff_t)simple_char_buffer_data_len - *offset;

    // Use min_t to ensure the type consistency for the minimum operation
    bytes_to_copy_ll = min_t(loff_t, (loff_t)len, bytes_to_copy_ll);

    // If no bytes to copy (e.g., requested length was 0 or calculation resulted in 0 or negative)
    if (bytes_to_copy_ll <= 0) {
        bytes_read = 0;
        goto out;
    }

    /* Copy data from kernel buffer to user space.
     * Cast bytes_to_copy_ll back to size_t for copy_to_user. This is safe as
     * bytes_to_copy_ll will not exceed BUFFER_SIZE (1KB), which fits in size_t.
     */
    if (copy_to_user(buffer, simple_char_buffer + *offset, (size_t)bytes_to_copy_ll)) {
        pr_err("%s: Failed to copy data to user space\n", DEVICE_NAME);
        bytes_read = -EFAULT; /* Bad address */
        goto out;
    }

    /* Update the file offset for the next read/write operation. */
    *offset += bytes_to_copy_ll;
    bytes_read = (ssize_t)bytes_to_copy_ll; /* Cast to ssize_t */

out:
    mutex_unlock(&simple_char_buffer_mutex);
    pr_info("%s: Read %zd bytes from offset %lld (data_len: %zu)\n",
            DEVICE_NAME, bytes_read, *offset - bytes_read, simple_char_buffer_data_len);
    return bytes_read;
}

/*
 * The device write callback function.
 * @file: Pointer to the file structure.
 * @buffer: Pointer to the user buffer to copy data from.
 * @len: The number of bytes the user wants to write.
 * @offset: The current read/write offset within the device.
 *
 * Returns the number of bytes written on success, or an error code on failure.
 */
static ssize_t simple_char_write(struct file *file, const char __user *buffer,
                                 size_t len, loff_t *offset)
{
    ssize_t bytes_written = 0;
    loff_t bytes_to_write_ll; // Use loff_t for calculations involving *offset

    /* Acquire mutex to protect access to the shared buffer and its length. */
    mutex_lock(&simple_char_buffer_mutex);

    /* If the requested offset is beyond the buffer capacity, we cannot write.
     * Cast BUFFER_SIZE to loff_t for safe comparison with *offset.
     */
    if (*offset >= (loff_t)BUFFER_SIZE) {
        pr_warn("%s: Cannot write: offset %lld is beyond buffer capacity %zu\n",
                DEVICE_NAME, *offset, BUFFER_SIZE); /* Use %zu for size_t BUFFER_SIZE */
        goto out; /* Return 0 bytes written, indicating no space. */
    }

    /*
     * Calculate available space from current offset to the end of the buffer.
     * Perform all calculations using loff_t to avoid mixed-type warnings.
     */
    bytes_to_write_ll = (loff_t)BUFFER_SIZE - *offset;

    /*
     * Determine the actual number of bytes to write.
     * It's the minimum of: requested length (len) and available space.
     * Use min_t to ensure type consistency for the minimum operation.
     */
    bytes_to_write_ll = min_t(loff_t, (loff_t)len, bytes_to_write_ll);

    // If no bytes to write (e.g., requested length was 0 or no space)
    if (bytes_to_write_ll <= 0) {
        bytes_written = 0;
        goto out;
    }

    /* Copy data from user space to kernel buffer.
     * Cast bytes_to_write_ll back to size_t for copy_from_user. This is safe as
     * bytes_to_write_ll will not exceed BUFFER_SIZE (1KB), which fits in size_t.
     */
    if (copy_from_user(simple_char_buffer + *offset, buffer, (size_t)bytes_to_write_ll)) {
        pr_err("%s: Failed to copy data from user space\n", DEVICE_NAME);
        bytes_written = -EFAULT; /* Bad address */
        goto out;
    }

    /* Update the file offset for the next read/write operation. */
    *offset += bytes_to_write_ll;
    bytes_written = (ssize_t)bytes_to_write_ll;

    /*
     * Update the simple_char_buffer_data_len to reflect the maximum extent of
     * valid data written into the buffer. This is crucial for read operations.
     * Compare *offset (loff_t) with simple_char_buffer_data_len (size_t) using
     * consistent types, then cast *offset to size_t for assignment.
     * This cast is safe because *offset is capped at BUFFER_SIZE (1KB).
     */
    if (*offset > (loff_t)simple_char_buffer_data_len)
        simple_char_buffer_data_len = (size_t)*offset;

out:
    mutex_unlock(&simple_char_buffer_mutex);
    pr_info("%s: Written %zd bytes to offset %lld (data_len: %zu)\n",
            DEVICE_NAME, bytes_written, *offset - bytes_written, simple_char_buffer_data_len);
    return bytes_written;
}

/*
 * File operations structure.
 * Defines the entry points for device file operations.
 * `noop_llseek` is used to let the VFS handle offset changes based on read/write.
 */
static const struct file_operations simple_char_fops = {
    .owner = THIS_MODULE,
    .open = simple_char_open,
    .release = simple_char_release,
    .read = simple_char_read,
    .write = simple_char_write,
    .llseek = noop_llseek,
};

/*
 * Module initialization function.
 */
static int __init simple_char_driver_init(void)
{
    int ret;

    pr_info("%s: Initializing simple character device driver\n", DEVICE_NAME);

    /* 1. Allocate a dynamic major number for our device. */
    ret = alloc_chrdev_region(&simple_char_dev_nr, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("%s: Failed to allocate character device region: %d\n", DEVICE_NAME, ret);
        return ret;
    }
    pr_info("%s: Device number allocated: Major = %d, Minor = %d\n",
            DEVICE_NAME, MAJOR(simple_char_dev_nr), MINOR(simple_char_dev_nr));

    /* 2. Create a device class. This will show up in /sys/class. */
    simple_char_dev_class = class_create(CLASS_NAME);
    if (IS_ERR(simple_char_dev_class)) {
        ret = (int)PTR_ERR(simple_char_dev_class); /* Explicitly cast PTR_ERR result to int */
        pr_err("%s: Failed to create device class: %d\n", DEVICE_NAME, ret);
        goto unregister_chrdev;
    }

    /* 3. Initialize and add the cdev structure.
     * This registers our file operations with the kernel.
     */
    cdev_init(&simple_char_cdev, &simple_char_fops);
    simple_char_cdev.owner = THIS_MODULE;
    ret = cdev_add(&simple_char_cdev, simple_char_dev_nr, 1);
    if (ret < 0) {
        pr_err("%s: Failed to add cdev: %d\n", DEVICE_NAME, ret);
        goto destroy_class;
    }

    /* 4. Create device file in /dev.
     * This automatically creates the device node (e.g., /dev/simple_char_dev).
     */
    if (IS_ERR(device_create(simple_char_dev_class, NULL, simple_char_dev_nr,
                             NULL, DEVICE_NAME))) {
        ret = (int)PTR_ERR(simple_char_dev_class); /* Explicitly cast PTR_ERR result to int */
        pr_err("%s: Failed to create device file: %d\n", DEVICE_NAME, ret);
        goto delete_cdev;
    }

    /* 5. Allocate the internal 1KB buffer for read/write operations. */
    simple_char_buffer = kmalloc(BUFFER_SIZE, GFP_KERNEL);
    if (!simple_char_buffer) {
        pr_err("%s: Failed to allocate %zu bytes for internal buffer\n", DEVICE_NAME, BUFFER_SIZE); /* Use %zu for size_t BUFFER_SIZE */
        ret = -ENOMEM;
        goto destroy_device;
    }
    simple_char_buffer_data_len = 0; /* Initially, the buffer contains no valid data. */
    pr_info("%s: Internal buffer allocated (size: %zu bytes)\n", DEVICE_NAME, BUFFER_SIZE); /* Use %zu for size_t BUFFER_SIZE */

    pr_info("%s: Simple character device driver initialized successfully\n", DEVICE_NAME);
    return 0;

/* Error handling and cleanup steps in reverse order of allocation/registration */
destroy_device:
    device_destroy(simple_char_dev_class, simple_char_dev_nr);
delete_cdev:
    cdev_del(&simple_char_cdev);
destroy_class:
    class_destroy(simple_char_dev_class);
unregister_chrdev:
    unregister_chrdev_region(simple_char_dev_nr, 1);
    return ret;
}

/*
 * Module exit function.
 */
static void __exit simple_char_driver_exit(void)
{
    pr_info("%s: Exiting simple character device driver\n", DEVICE_NAME);

    /* Free the allocated internal buffer. */
    if (simple_char_buffer) {
        kfree(simple_char_buffer);
        simple_char_buffer = NULL;
        pr_info("%s: Internal buffer freed\n", DEVICE_NAME);
    }

    /* Destroy the device file created in /dev. */
    device_destroy(simple_char_dev_class, simple_char_dev_nr);
    pr_info("%s: Device file destroyed\n", DEVICE_NAME);

    /* Delete the character device from the kernel. */
    cdev_del(&simple_char_cdev);
    pr_info("%s: cdev deleted\n", DEVICE_NAME);

    /* Destroy the device class. */
    class_destroy(simple_char_dev_class);
    pr_info("%s: Device class destroyed\n", DEVICE_NAME);

    /* Unregister the character device region. */
    unregister_chrdev_region(simple_char_dev_nr, 1);
    pr_info("%s: Character device region unregistered\n", DEVICE_NAME);

    pr_info("%s: Simple character device driver exited\n", DEVICE_NAME);
}

/*
 * Register the module's initialization and exit functions.
 */
module_init(simple_char_driver_init);
module_exit(simple_char_driver_exit);

/*
 * Module metadata.
 */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Bhanu");
MODULE_DESCRIPTION("A simple character device driver with 1KB internal buffer");
MODULE_VERSION("0.1");