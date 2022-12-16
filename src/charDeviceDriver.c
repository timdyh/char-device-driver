/*
 *  chardev.c: Creates a read-only char device that says how many times
 *  you've read from the dev file
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <asm/uaccess.h>	/* for put_user */
#include <charDeviceDriver.h>
#include "ioctl.h"

MODULE_LICENSE("GPL");

/* 
 * This function is called whenever a process tries to do an ioctl on our
 * device file. We get two extra parameters (additional to the inode and file
 * structures, which all device functions get): the number of the ioctl called
 * and the parameter given to the ioctl function.
 *
 * If the ioctl is write or read/write (meaning output is returned to the
 * calling process), the ioctl call returns the output of this function.
 *
 */


DEFINE_MUTEX  (devLock);
static int counter = 0;

static long device_ioctl(struct file *file,	/* see include/linux/fs.h */
		 unsigned int ioctl_num,	/* number and param for ioctl */
		 unsigned long ioctl_param)
{

	/* 
	 * Switch according to the ioctl called 
	 */
	if (ioctl_num == RESET_COUNTER) {
	    counter = 0; 
	    /* 	    return 0; */
	    return 5; /* can pass integer as return value */
	}

	else {
	    /* no operation defined - return failure */
	    return -EINVAL;

	}
}

int is_list_empty(void)
{
	return head == tail;
}

void list_init(void)
{
	Node* p;
	mutex_lock (&devLock);

	p = (Node*)kmalloc(sizeof(Node), GFP_KERNEL);
	p->next = NULL;
	head = tail = p;
	list_len = 0;

	mutex_unlock (&devLock);
}

void list_destroy(void)
{
	Node* p;
	mutex_lock (&devLock);

	p = head->next;
	while (p != NULL) {
		Node* q = p->next;
		kfree(p);
		p = q;
		list_len--;
	}

	kfree(head);

	mutex_unlock (&devLock);
}

Node* make_node(const char *buff, size_t len)
{
	Node* p = (Node*)kmalloc(sizeof(Node) + len, GFP_KERNEL);
	int result = copy_from_user(p->buf, buff, len);
	if (result > 0) {
		kfree(p);
		return NULL;
	}

	p->buf[len] = '\0';
	p->next = NULL;
	return p;
}

int list_push_back(Node* p)
{
	mutex_lock (&devLock);

	// double check
	if (list_len >= MAX_MSG_NUM) {
		mutex_unlock (&devLock);
		return -EBUSY;
	}

	tail->next = p;
	tail = tail->next;

	list_len++;

	mutex_unlock (&devLock);
	return SUCCESS;
}

Node* list_pop_front(void)
{
	Node* p;
	mutex_lock (&devLock);

	if (is_list_empty()) {
		mutex_unlock (&devLock);
		return NULL;
	}

	p = head->next;
	head->next = p->next;
	if (head->next == NULL)
		tail = head;

	list_len--;

	mutex_unlock (&devLock);
	return p;
}

/*
 * This function is called when the module is loaded
 */
int init_module(void)
{
    Major = register_chrdev(0, DEVICE_NAME, &fops);

	if (Major < 0) {
	  printk(KERN_ALERT "Registering char device failed with %d\n", Major);
	  return Major;
	}

	printk(KERN_INFO "I was assigned major number %d. To talk to\n", Major);
	printk(KERN_INFO "the driver, create a dev file with\n");
	printk(KERN_INFO "'mknod /dev/%s c %d 0'.\n", DEVICE_NAME, Major);
	printk(KERN_INFO "Try various minor numbers. Try to cat and echo to\n");
	printk(KERN_INFO "the device file.\n");
	printk(KERN_INFO "Remove the device file and module when done.\n");

	list_init();

	return SUCCESS;
}

/*
 * This function is called when the module is unloaded
 */
void cleanup_module(void)
{
	list_destroy();
	/*  Unregister the device */
	unregister_chrdev(Major, DEVICE_NAME);
}

/*
 * Methods
 */

/* 
 * Called when a process tries to open the device file, like
 * "cat /dev/mycharfile"
 */
static int device_open(struct inode *inode, struct file *file)
{
    
    mutex_lock (&devLock);
    if (Device_Open) {
	mutex_unlock (&devLock);
	return -EBUSY;
    }
    Device_Open++;
    mutex_unlock (&devLock);
    // sprintf(msg, "I already told you %d times Hello world!\n", counter++);
    try_module_get(THIS_MODULE);
    
    return SUCCESS;
}

/* Called when a process closes the device file. */
static int device_release(struct inode *inode, struct file *file)
{
    mutex_lock (&devLock);
	Device_Open--;		/* We're now ready for our next caller */
	mutex_unlock (&devLock);
	/* 
	 * Decrement the usage count, or else once you opened the file, you'll
	 * never get get rid of the module. 
	 */
	module_put(THIS_MODULE);

	return 0;
}

/* 
 * Called when a process, which already opened the dev file, attempts to
 * read from it.
 */
static ssize_t device_read(struct file *filp,	/* see include/linux/fs.h   */
			   char *buffer,	/* buffer to fill with data */
			   size_t length,	/* length of the buffer     */
			   loff_t * offset)
{
	/* result of function calls */
	int result;

	Node* p = list_pop_front();
	if (p == NULL)
		return -EAGAIN;

	/* 
	 * Actually put the data into the buffer 
	 */
	if (strlen(p->buf) + 1 < length)
	    length = strlen(p->buf) + 1;
	result = copy_to_user(buffer, p->buf, length);
	kfree(p);

	if (result > 0)
	    return -EFAULT; /* copy failed */
	/* 
	 * Most read functions return the number of bytes put into the buffer
	 */
	return length;
}

/* Called when a process writes to dev file: echo "hi" > /dev/hello  */
static ssize_t
device_write(struct file *filp, const char *buff, size_t len, loff_t * off)
{
	int ret;
	Node* p;

	if (len > MAX_MSG_SIZE) {
		return -EINVAL;
	}

	mutex_lock (&devLock);
	if (list_len >= MAX_MSG_NUM) {
		mutex_unlock (&devLock);
		return -EBUSY;
	}
	mutex_unlock (&devLock);

	p = make_node(buff, len);
	if (p == NULL)
		return -EFAULT;

	ret = list_push_back(p);
	if (ret != SUCCESS) {
		kfree(p);
		return ret;
	}

	return len;
}
