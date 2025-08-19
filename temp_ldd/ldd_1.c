#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h> /* For copy_to_user, copy_from_user */
#include <linux/slab.h>    /* For kmalloc, kfree, kzalloc */

#define DRIVER_NAME "mychar_dev"
#define DEVICE_NAME "mychar"
#define BUFFER_SIZE 1024

/* Module Global Variables */
static dev_t my_device_nbr;
static struct cdev my_cdev;
static struct class *my_class;
static char *my_buffer;

/*
 * @brief This function is called when a device file is opened.
 * @param inode: Pointer to the inode structure.
 * @param file: Pointer to the file structure.
 * @return 0 on success, or a negative error code on failure.
 */
static int my_device_open(struct inode *inode, struct file *file)
{
    /*
     * We don't need to do anything specific here for a simple
     * buffer device.
     */
    pr_info("%s: Device opened\n", DRIVER_NAME);
    return 0;
}

/*
 * @brief This function is called when a device file is closed.
 * @param inode: Pointer to the inode structure.
 * @param file: Pointer to the file structure.
 * @return 0 on success, or a negative error code on failure.
 */
static int my_device_release(struct inode *inode, struct file *file)
{
    pr_info("%s: Device closed\n", DRIVER_NAME);
    return 0;
}

/*
 * @brief This function is called to read data from the device.
 * @param file: Pointer to the file structure.
 * @param user_buffer: Pointer to the user buffer to copy data into.
 * @param count: The number of bytes to read.
 * @param offset: Pointer to the file offset.
 * @return The number of bytes read on success, or a negative error code on failure.
 */
static ssize_t my_device_read(struct file *file, char __user *user_buffer,
                              size_t count, loff_t *offset)
{
    ssize_t bytes_to_read;

    /* Check if the offset is beyond the buffer size */
    if (*offset >= BUFFER_SIZE)
        return 0; /* End of file */

    /* Calculate the number of bytes available to read from the current offset */
    bytes_to_read = min((size_t)(BUFFER_SIZE - *offset), count);

    /* Copy data from kernel buffer to user buffer */
    if (copy_to_user(user_buffer, my_buffer + *offset, bytes_to_read)) {
        pr_err("%s: Failed to copy data to user space\n", DRIVER_NAME);
        return -EFAULT;
    }

    *offset += bytes_to_read;
    pr_info("%s: Read %zd bytes from offset %lld\n", DRIVER_NAME,
            bytes_to_read, *offset - bytes_to_read);

    return bytes_to_read;
}

/*
 * @brief This function is called to write data to the device.
 * @param file: Pointer to the file structure.
 * @param user_buffer: Pointer to the user buffer to copy data from.
 * @param count: The number of bytes to write.
 * @param offset: Pointer to the file offset.
 * @return The number of bytes written on success, or a negative error code on failure.
 */
static ssize_t my_device_write(struct file *file, const char __user *user_buffer,
                               size_t count, loff_t *offset)
{
    ssize_t bytes_to_write;

    /* Check if the offset is beyond the buffer size */
    if (*offset >= BUFFER_SIZE)
        return -ENOSPC; /* No space left on device */

    /* Calculate the number of bytes that can be written from the current offset */
    bytes_to_write = min((size_t)(BUFFER_SIZE - *offset), count);

    /* Copy data from user buffer to kernel buffer */
    if (copy_from_user(my_buffer + *offset, user_buffer, bytes_to_write)) {
        pr_err("%s: Failed to copy data from user space\n", DRIVER_NAME);
        return -EFAULT;
    }

    *offset += bytes_to_write;
    pr_info("%s: Written %zd bytes to offset %lld\n", DRIVER_NAME,
            bytes_to_write, *offset - bytes_to_write);

    return bytes_to_write;
}

/* File operations structure */
static const struct file_operations my_fops = {
    .owner   = THIS_MODULE,
    .open    = my_device_open,
    .release = my_device_release,
    .read    = my_device_read,
    .write   = my_device_write,
};

/*
 * @brief This function is called when the module is loaded.
 * @return 0 on success, or a negative error code on failure.
 */
static int __init my_char_driver_init(void)
{
    int ret;
    struct device *dev_ptr;

    pr_info("%s: Initializing the character device driver\n", DRIVER_NAME);

    /* 1. Allocate a major and minor number for the device */
    ret = alloc_chrdev_region(&my_device_nbr, 0, 1, DRIVER_NAME);
    if (ret < 0) {
        pr_err("%s: Failed to allocate char device region\n", DRIVER_NAME);
        goto cleanup_exit;
    }
    pr_info("%s: Device registered with Major:%d Minor:%d\n", DRIVER_NAME,
            MAJOR(my_device_nbr), MINOR(my_device_nbr));

    /* 2. Allocate and initialize the internal buffer */
    // Using kzalloc to allocate zero-initialized memory, removing the need for memset
    my_buffer = (char *)kzalloc(BUFFER_SIZE, GFP_KERNEL);
    if (!my_buffer) {
        pr_err("%s: Failed to allocate buffer\n", DRIVER_NAME);
        ret = -ENOMEM;
        goto cleanup_unregister_chrdev;
    }
    pr_info("%s: Internal buffer allocated (1KB) and zero-initialized\n", DRIVER_NAME);

    /* 3. Create a cdev structure and initialize it */
    cdev_init(&my_cdev, &my_fops);
    my_cdev.owner = THIS_MODULE;

    /* 4. Add the cdev to the kernel */
    ret = cdev_add(&my_cdev, my_device_nbr, 1);
    if (ret < 0) {
        pr_err("%s: Failed to add cdev\n", DRIVER_NAME);
        goto cleanup_free_buffer;
    }
    pr_info("%s: Cdev added\n", DRIVER_NAME);

    /* 5. Create a device class (for udev auto-creation of device file) */
    my_class = class_create(DRIVER_NAME);
    if (IS_ERR(my_class)) {
        pr_err("%s: Failed to create device class\n", DRIVER_NAME);
        ret = (int)PTR_ERR(my_class);
        goto cleanup_del_cdev;
    }
    pr_info("%s: Device class created\n", DRIVER_NAME);

    /* 6. Create the device file in /dev */
    dev_ptr = device_create(my_class, NULL, my_device_nbr, NULL, DEVICE_NAME);
    if (IS_ERR(dev_ptr)) {
        pr_err("%s: Failed to create device file %s\n", DRIVER_NAME,
               DEVICE_NAME);
        ret = (int)PTR_ERR(dev_ptr);
        goto cleanup_destroy_class;
    }
    pr_info("%s: Device file /dev/%s created\n", DRIVER_NAME, DEVICE_NAME);

    pr_info("%s: Character device driver loaded successfully\n", DRIVER_NAME);
    return 0;

cleanup_destroy_class:
    class_destroy(my_class);
cleanup_del_cdev:
    cdev_del(&my_cdev);
cleanup_free_buffer:
    kfree(my_buffer);
cleanup_unregister_chrdev:
    unregister_chrdev_region(my_device_nbr, 1);
cleanup_exit:
    pr_err("%s: Module initialization failed\n", DRIVER_NAME);
    return ret;
}

/*
 * @brief This function is called when the module is unloaded.
 */
static void __exit my_char_driver_exit(void)
{
    pr_info("%s: Exiting the character device driver\n", DRIVER_NAME);

    /* 1. Destroy the device file */
    device_destroy(my_class, my_device_nbr);
    pr_info("%s: Device file /dev/%s destroyed\n", DRIVER_NAME, DEVICE_NAME);

    /* 2. Destroy the device class */
    class_destroy(my_class);
    pr_info("%s: Device class destroyed\n", DRIVER_NAME);

    /* 3. Delete the cdev */
    cdev_del(&my_cdev);
    pr_info("%s: Cdev deleted\n", DRIVER_NAME);

    /* 4. Free the internal buffer */
    kfree(my_buffer);
    pr_info("%s: Internal buffer freed\n", DRIVER_NAME);

    /* 5. Unregister the character device region */
    unregister_chrdev_region(my_device_nbr, 1);
    pr_info("%s: Character device region unregistered\n", DRIVER_NAME);

    pr_info("%s: Character device driver unloaded\n", DRIVER_NAME);
}

module_init(my_char_driver_init);
module_exit(my_char_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Bhanu");
MODULE_DESCRIPTION("A simple character device driver with 1KB buffer.");
MODULE_VERSION("0.1");