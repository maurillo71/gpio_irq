/*
  GPIO irq handling module test.
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>

#define MODULE_NAME "gpio_irq"

// gpio 3 is ENTER key on LCD7 beaglebone cape board.
#define IRQ_PIN 3
// gpio 147 is pin 29
#define TOGGLE_PIN 147 

struct gpio_irq_dev {
	dev_t devt;
	struct cdev cdev;
	struct device *device;
	struct class *class;
	struct semaphore sem;
	void *context;
	int irqCnt;
	int irq;
};

static struct gpio_irq_dev gpio_irq;


static irqreturn_t gpio_irq_handler(int irq, void *dev_id)
{
	// gpio_set_value(TOGGLE_PIN, 0);

	gpio_irq.irqCnt++;
	printk(KERN_ALERT "Interrupt!");
	// if (gpio_irq.context) {			
	// 	complete(gpio_irq.context);			
	// 	gpio_irq.context = 0;
	// }
	
	return IRQ_HANDLED;
}

static void do_latency_test(void)
{
	DECLARE_COMPLETION_ONSTACK(done);
	int result;

	result = request_irq(gpio_irq.irq,
				gpio_irq_handler,
				IRQF_TRIGGER_RISING,
				MODULE_NAME,
				&gpio_irq);

	if (result < 0) {
		printk(KERN_ALERT "request_irq failed: %d\n", result);
		return;
	}

	gpio_irq.context = &done;
	gpio_set_value(TOGGLE_PIN, 1);

	result = wait_for_completion_interruptible_timeout(&done, HZ / 2);

	if (result == 0) {
		printk(KERN_ALERT "Timed out waiting for interrupt.\n");
		printk(KERN_ALERT "Did you forget to jumper the pins?\n");
	}
	else {
		printk(KERN_ALERT "Interrupt processed\n");
	}

	free_irq(gpio_irq.irq, &gpio_irq);
}

static void do_toggle_test(void)
{
	int i;

	for (i = 0; i < 1000; i++) {
		gpio_set_value(TOGGLE_PIN, 1);
		gpio_set_value(TOGGLE_PIN, 0);
	}

	printk(KERN_ALERT "Toggle test complete\n");
}

static ssize_t gpio_irq_write(struct file *filp, const char __user *buff,
		size_t count, loff_t *f_pos)
{
	char cmd[4];
	ssize_t status;

	if (count == 0)
		return 0;

	if (down_interruptible(&gpio_irq.sem))
		return -ERESTARTSYS;

	if (copy_from_user(cmd, buff, 1)) {
		status = -EFAULT;
		goto gpio_irq_write_done;
	}
 
	// Nothing fancy, '1' is latency test, anything else is a toggle test.	
	if (cmd[0] == '1')
		do_latency_test();
	else
		do_toggle_test();		
			
	status = count;
	*f_pos += count;

gpio_irq_write_done:

	up(&gpio_irq.sem);

	return status;
}

static const struct file_operations gpio_irq_fops = {
	.owner = THIS_MODULE,
	.write = gpio_irq_write,
};

static int __init gpio_irq_init_cdev(void)
{
	int error;

	gpio_irq.devt = MKDEV(0, 0);

	error = alloc_chrdev_region(&gpio_irq.devt, 0, 1, MODULE_NAME);
	if (error)
		return error;

	cdev_init(&gpio_irq.cdev, &gpio_irq_fops);
	gpio_irq.cdev.owner = THIS_MODULE;

	error = cdev_add(&gpio_irq.cdev, gpio_irq.devt, 1);
	if (error) {
		unregister_chrdev_region(gpio_irq.devt, 1);
		return error;
	}	

	return 0;
}

static int __init gpio_irq_init_class(void)
{
	int ret;

	if (!gpio_irq.class) {
		gpio_irq.class = class_create(THIS_MODULE, MODULE_NAME);

		if (IS_ERR(gpio_irq.class)) {
			ret = PTR_ERR(gpio_irq.class);
			return ret;
		}
	}

	gpio_irq.device = device_create(gpio_irq.class, NULL, gpio_irq.devt, 
					NULL, MODULE_NAME);

	if (IS_ERR(gpio_irq.device)) {
		ret = PTR_ERR(gpio_irq.device);
		class_destroy(gpio_irq.class);
		return ret;
	}

	return 0;
}

static int __init gpio_irq_init_pins(void)
{
	if (gpio_request(IRQ_PIN, "irqpin")) {
		printk(KERN_ALERT "gpio_request failed\n");
		goto init_pins_fail_1;
	}

	if (gpio_direction_input(IRQ_PIN)) {
		printk(KERN_ALERT "gpio_direction_input failed\n");
		goto init_pins_fail_2;
	}

	gpio_irq.irq = gpio_to_irq(IRQ_PIN);

	return 0;

init_pins_fail_2:
	gpio_free(IRQ_PIN);

init_pins_fail_1:

	return -1;
}

static int __init gpio_irq_init(void)
{
	int result;

	sema_init(&gpio_irq.sem, 1);

	if (gpio_irq_init_pins() < 0)
		goto init_fail_1;

	result = request_irq(gpio_irq.irq,
				gpio_irq_handler,
				IRQF_TRIGGER_RISING,
				MODULE_NAME,
				&gpio_irq);

	if (result < 0) {
		printk(KERN_ALERT "request_irq failed: %d\n", result);
		goto init_fail_2;
	}

	gpio_irq.irqCnt = 0;
	return 0;

// init_fail_3:
// 	device_destroy(gpio_irq.class, gpio_irq.devt);
//   	class_destroy(gpio_irq.class);

// init_fail_2:
// 	cdev_del(&gpio_irq.cdev);
// 	unregister_chrdev_region(gpio_irq.devt, 1);

init_fail_2:
	gpio_free(IRQ_PIN);

init_fail_1:

	return -1;
}
module_init(gpio_irq_init);

static void __exit gpio_irq_exit(void)
{
	free_irq(gpio_irq.irq, &gpio_irq);

	// gpio_free(TOGGLE_PIN);
	gpio_free(IRQ_PIN);

	// device_destroy(gpio_irq.class, gpio_irq.devt);
  	// class_destroy(gpio_irq.class);

	// cdev_del(&gpio_irq.cdev);
	// unregister_chrdev_region(gpio_irq.devt, 1);
}
module_exit(gpio_irq_exit);


MODULE_AUTHOR("Mauro Gamba");
MODULE_DESCRIPTION("A module for testing gpio irq on Beaglebone");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION("0.1");
