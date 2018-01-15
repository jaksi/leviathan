#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(0x2433, 0xb200) },
	{ }
};

MODULE_DEVICE_TABLE(usb, id_table);

struct usb_kraken {
	struct usb_device *udev;
	struct usb_interface *interface;
	struct hrtimer update_timer;
	struct tasklet_struct update_tasklet;
	u8 *color_message, *pump_message, *fan_message, *status_message;
};

static int kraken_probe(struct usb_interface *interface, const struct usb_device_id *id);
static void kraken_disconnect(struct usb_interface *interface);

static struct usb_driver kraken_driver = {
	.name =		"kraken",
	.probe =	kraken_probe,
	.disconnect =	kraken_disconnect,
	.id_table =	id_table,
};

static int kraken_start_transaction(struct usb_kraken *kraken)
{
	return usb_control_msg(kraken->udev, usb_sndctrlpipe(kraken->udev, 0), 2, 0x40, 0x0001, 0, NULL, 0, 1000);
}

static int kraken_send_message(struct usb_kraken *kraken, u8 *message, int length)
{
	int sent;
	int retval = usb_bulk_msg(kraken->udev, usb_sndbulkpipe(kraken->udev, 2), message, length, &sent, 3000);
	if (retval != 0)
		return retval;
	if (sent != length)
		return -EIO;
	return 0;
}

static int kraken_receive_message(struct usb_kraken *kraken, u8 message[], int expected_length)
{
	int received;
	int retval = usb_bulk_msg(kraken->udev, usb_rcvbulkpipe(kraken->udev, 2), message, expected_length, &received, 3000);
	if (retval != 0)
		return retval;
	if (received != expected_length)
		return -EIO;
	return 0;
}

static void kraken_update(struct usb_kraken *kraken)
{
	int retval = 0;
	if (
		(retval = kraken_start_transaction(kraken)) ||
		(retval = kraken_send_message(kraken, kraken->color_message, 19)) ||
		(retval = kraken_receive_message(kraken, kraken->status_message, 32)) ||
		(retval = kraken_start_transaction(kraken)) ||
		(retval = kraken_send_message(kraken, kraken->pump_message, 2)) ||
		(retval = kraken_send_message(kraken, kraken->fan_message, 2)) ||
		(retval = kraken_receive_message(kraken, kraken->status_message, 32))
	   )
		dev_err(&kraken->udev->dev, "Failed to update: %d\n", retval);
	printk("kraken_update in_interrupt()=%lu in_irq=%lu in_softirq=%lu\n", in_interrupt(), in_irq(), in_softirq());
}

static ssize_t show_speed(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_kraken *kraken = usb_get_intfdata(intf);

	return sprintf(buf, "%u\n", kraken->pump_message[1]);
}

static ssize_t set_speed(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_kraken *kraken = usb_get_intfdata(intf);

	u8 speed;
	if (sscanf(buf, "%hhu", &speed) != 1 || speed < 30 || speed > 100)
		return -EINVAL;

	kraken->pump_message[1] = speed;
	kraken->fan_message[1] = speed;

	kraken_update(kraken);

	return count;
}

static DEVICE_ATTR(speed, S_IRUGO | S_IWUSR | S_IWGRP, show_speed, set_speed);

static ssize_t show_color(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_kraken *kraken = usb_get_intfdata(intf);

	return sprintf(buf, "%02x%02x%02x\n", kraken->color_message[1], kraken->color_message[2], kraken->color_message[3]);
}

static ssize_t set_color(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_kraken *kraken = usb_get_intfdata(intf);

	u8 r, g, b;
	if (sscanf(buf, "%02hhx%02hhx%02hhx", &r, &g, &b) != 3)
		return -EINVAL;

	kraken->color_message[1] = r;
	kraken->color_message[2] = g;
	kraken->color_message[3] = b;

	kraken_update(kraken);

	return count;
}

static DEVICE_ATTR(color, S_IRUGO | S_IWUSR | S_IWGRP, show_color, set_color);

static ssize_t show_alternate_color(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_kraken *kraken = usb_get_intfdata(intf);

	return sprintf(buf, "%02x%02x%02x\n", kraken->color_message[4], kraken->color_message[5], kraken->color_message[6]);
}

static ssize_t set_alternate_color(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_kraken *kraken = usb_get_intfdata(intf);

	u8 r, g, b;
	if (sscanf(buf, "%02hhx%02hhx%02hhx", &r, &g, &b) != 3)
		return -EINVAL;

	kraken->color_message[4] = r;
	kraken->color_message[5] = g;
	kraken->color_message[6] = b;

	kraken_update(kraken);

	return count;
}

static DEVICE_ATTR(alternate_color, S_IRUGO | S_IWUSR | S_IWGRP, show_alternate_color, set_alternate_color);

static ssize_t show_interval(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_kraken *kraken = usb_get_intfdata(intf);

	return sprintf(buf, "%u\n", kraken->color_message[11]);
}

static ssize_t set_interval(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_kraken *kraken = usb_get_intfdata(intf);

	u8 interval;
	if (sscanf(buf, "%hhu", &interval) != 1 || interval == 0)
		return -EINVAL;

	kraken->color_message[11] = interval; kraken->color_message[12] = interval;

	kraken_update(kraken);

	return count;
}

static DEVICE_ATTR(interval, S_IRUGO | S_IWUSR | S_IWGRP, show_interval, set_interval);

static ssize_t show_mode(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_kraken *kraken = usb_get_intfdata(intf);

	if (kraken->color_message[14] == 1)
		return sprintf(buf, "alternating\n");
	else if (kraken->color_message[15] == 1)
		return sprintf(buf, "blinking\n");
	else if (kraken->color_message[13] == 1)
		return sprintf(buf, "normal\n");
	else
		return sprintf(buf, "off\n");
}

static ssize_t set_mode(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_kraken *kraken = usb_get_intfdata(intf);

	if (strncasecmp(buf, "normal", strlen("normal")) == 0) {
		kraken->color_message[13] = 1;
		kraken->color_message[14] = 0;
		kraken->color_message[15] = 0;
	}
	else if (strncasecmp(buf, "alternating", strlen("alternating")) == 0) {
		kraken->color_message[13] = 1;
		kraken->color_message[14] = 1;
		kraken->color_message[15] = 0;
	}
	else if (strncasecmp(buf, "blinking", strlen("blinking")) == 0) {
		kraken->color_message[13] = 1;
		kraken->color_message[14] = 0;
		kraken->color_message[15] = 1;
	}
	else if (strncasecmp(buf, "off", strlen("off")) == 0) {
		kraken->color_message[13] = 0;
		kraken->color_message[14] = 0;
		kraken->color_message[15] = 0;
	}
	else
		return -EINVAL;

	kraken_update(kraken);

	return count;
}

static DEVICE_ATTR(mode, S_IRUGO | S_IWUSR | S_IWGRP, show_mode, set_mode);

static ssize_t show_temp(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_kraken *kraken = usb_get_intfdata(intf);

	kraken_update(kraken);

	return sprintf(buf, "%u\n", kraken->status_message[10]);
}

static DEVICE_ATTR(temp, S_IRUGO, show_temp, NULL);

static ssize_t show_pump(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_kraken *kraken = usb_get_intfdata(intf);

	kraken_update(kraken);

	return sprintf(buf, "%u\n", 256 * kraken->status_message[8] + kraken->status_message[9]);
}

static DEVICE_ATTR(pump, S_IRUGO, show_pump, NULL);

static ssize_t show_fan(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_kraken *kraken = usb_get_intfdata(intf);

	kraken_update(kraken);

	return sprintf(buf, "%u\n", 256 * kraken->status_message[0] + kraken->status_message[1]);
}

static DEVICE_ATTR(fan, S_IRUGO, show_fan, NULL);

static void kraken_remove_device_files(struct usb_interface *interface)
{
	device_remove_file(&interface->dev, &dev_attr_speed);
	device_remove_file(&interface->dev, &dev_attr_color);
	device_remove_file(&interface->dev, &dev_attr_alternate_color);
	device_remove_file(&interface->dev, &dev_attr_interval);
	device_remove_file(&interface->dev, &dev_attr_mode);
	device_remove_file(&interface->dev, &dev_attr_temp);
	device_remove_file(&interface->dev, &dev_attr_pump);
	device_remove_file(&interface->dev, &dev_attr_fan);
}

enum hrtimer_restart update_timer_function(struct hrtimer *timer_for_restart)
{
	struct usb_kraken *dev = container_of(timer_for_restart, struct usb_kraken, update_timer);
	ktime_t cur = ktime_get();
	hrtimer_forward(timer_for_restart, cur, ktime_set(1, 0));
	printk("update_timer_function in_interrupt()=%lu in_irq=%lu in_softirq=%lu\n", in_interrupt(), in_irq(), in_softirq());
	tasklet_schedule(&dev->update_tasklet);
	return HRTIMER_RESTART;
}

static void update_tasklet_function(unsigned long param)
{
	// Uncomment for a kernel lockup \o/
	//struct usb_kraken *dev = (struct usb_kraken *)param;
	//kraken_update(dev);
	printk("update_tasklet_function in_interrupt()=%lu in_irq=%lu in_softirq=%lu\n", in_interrupt(), in_irq(), in_softirq());
}

static int kraken_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct usb_kraken *dev = NULL;
	int retval = -ENOMEM;
	dev = kmalloc(sizeof(struct usb_kraken), GFP_KERNEL);
	if (!dev)
		goto error_dev;

	dev->udev = usb_get_dev(udev);
	usb_set_intfdata(interface, dev);

	dev->color_message = kmalloc(19*sizeof(u8), GFP_KERNEL);
	dev->pump_message = kmalloc(2*sizeof(u8), GFP_KERNEL);
	dev->fan_message = kmalloc(2*sizeof(u8), GFP_KERNEL);
	dev->status_message = kmalloc(32*sizeof(u8), GFP_KERNEL);
	if (
		(dev->color_message = kmalloc(19*sizeof(u8), GFP_KERNEL)) == NULL ||
		(dev->pump_message = kmalloc(2*sizeof(u8), GFP_KERNEL)) == NULL ||
		(dev->fan_message = kmalloc(2*sizeof(u8), GFP_KERNEL)) == NULL ||
		(dev->status_message = kmalloc(32*sizeof(u8), GFP_KERNEL)) == NULL
	) {
		retval = ENOMEM;
		goto error_messages;
	}

	dev->color_message[0] = 0x10;
	dev->color_message[1] = 0x00; dev->color_message[2] = 0x00; dev->color_message[3] = 0xff;
	dev->color_message[4] = 0x00; dev->color_message[5] = 0xff; dev->color_message[6] = 0x00;
	dev->color_message[7] = 0x00; dev->color_message[8] = 0x00; dev->color_message[9] = 0x00; dev->color_message[10] = 0x3c;
	dev->color_message[11] = 1; dev->color_message[12] = 1;
	dev->color_message[13] = 1; dev->color_message[14] = 0; dev->color_message[15] = 0;
	dev->color_message[16] = 0x00; dev->color_message[17] = 0x00; dev->color_message[18] = 0x01;

	dev->pump_message[0] = 0x13;
	dev->pump_message[1] = 50;

	dev->fan_message[0] = 0x12;
	dev->fan_message[1] = 50;

	if (
		(retval = device_create_file(&interface->dev, &dev_attr_speed)) ||
		(retval = device_create_file(&interface->dev, &dev_attr_color)) ||
		(retval = device_create_file(&interface->dev, &dev_attr_alternate_color)) ||
		(retval = device_create_file(&interface->dev, &dev_attr_interval)) ||
		(retval = device_create_file(&interface->dev, &dev_attr_mode)) ||
		(retval = device_create_file(&interface->dev, &dev_attr_temp)) ||
		(retval = device_create_file(&interface->dev, &dev_attr_pump)) ||
		(retval = device_create_file(&interface->dev, &dev_attr_fan))
	)
		goto error;

	retval = usb_control_msg(udev, usb_sndctrlpipe(udev, 0), 2, 0x40, 0x0002, 0, NULL, 0, 1000);
	if (retval)
		goto error;

	dev_info(&interface->dev, "Kraken connected\n");
	hrtimer_init(&dev->update_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	dev->update_timer.function = &update_timer_function;
	hrtimer_start(&dev->update_timer, ktime_set(1, 0), HRTIMER_MODE_REL);
	tasklet_init(&dev->update_tasklet, update_tasklet_function, (u64)dev);
	return 0;
error:
	kraken_remove_device_files(interface);
error_messages:
	kfree(dev->status_message);
	kfree(dev->fan_message);
	kfree(dev->pump_message);
	kfree(dev->color_message);

	usb_set_intfdata(interface, NULL);
	usb_put_dev(dev->udev);
	kfree(dev);
error_dev:
	return retval;
}

static void kraken_disconnect(struct usb_interface *interface)
{
	struct usb_kraken *dev = usb_get_intfdata(interface);

	kraken_remove_device_files(interface);

	usb_set_intfdata(interface, NULL);
	usb_put_dev(dev->udev);

	kfree(dev->status_message);
	kfree(dev->fan_message);
	kfree(dev->pump_message);
	kfree(dev->color_message);

	hrtimer_cancel(&dev->update_timer);
	kfree(dev);

	dev_info(&interface->dev, "Kraken disconnected\n");
}

module_usb_driver(kraken_driver);

MODULE_LICENSE("GPL");
