#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(0x2433, 0xb200) },
	{ }
};

MODULE_DEVICE_TABLE(usb, id_table);

struct kraken_color {
	u8 r, g, b;
};

struct usb_kraken {
	struct usb_device *udev;
	struct usb_interface *interface;
	u8 speed;
	struct kraken_color color, alternate_color;
	u8 interval;
	enum {
		normal,
		alternating,
		blinking,
		off
	} mode;
	u8 temp;
	u16 pump, fan;
	u8 *pump_message, *fan_message, *color_message, *status_message;
};

static int kraken_probe(struct usb_interface *interface, const struct usb_device_id *id);
static void kraken_disconnect(struct usb_interface *interface);

static struct usb_driver kraken_driver = {
	.name =		"kraken",
	.probe =	kraken_probe,
	.disconnect =	kraken_disconnect,
	.id_table =	id_table,
};

static int kraken_start_transaction(struct usb_device *udev)
{
	return usb_control_msg(udev, usb_sndctrlpipe(udev, 0), 2, 0x40, 0x0001, 0, NULL, 0, 1000);
}

static int kraken_send_pump_message(struct usb_device *udev, struct usb_kraken *kraken)
{
	int len;
	int retval = usb_bulk_msg(udev, usb_sndbulkpipe(udev, 2), kraken->pump_message, 2, &len, 3000);
	if (retval != 0)
		return retval;
	if (len != 2)
		return -EIO;
	return 0;
}

static int kraken_send_fan_message(struct usb_device *udev, struct usb_kraken *kraken)
{
	int len;
	int retval = usb_bulk_msg(udev, usb_sndbulkpipe(udev, 2), kraken->fan_message, 2, &len, 3000);
	if (retval != 0)
		return retval;
	if (len != 2)
		return -EIO;
	return 0;
}

static int kraken_send_color_message(struct usb_device *udev, struct usb_kraken *kraken)
{
	int len;
	int retval = usb_bulk_msg(udev, usb_sndbulkpipe(udev, 2), kraken->color_message, 19, &len, 3000);
	if (retval != 0)
		return retval;
	if (len != 19)
		return -EIO;
	return 0;
}

static int kraken_receive_status_message(struct usb_device *udev, struct usb_kraken *kraken)
{
	int len;
	int retval = usb_bulk_msg(udev, usb_rcvbulkpipe(udev, 2), kraken->status_message, 64, &len, 3000);
	if (retval != 0)
		return retval;
	if (len != 32)
		return -EIO;
	return 0;
}

static void kraken_update(struct device *dev)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_kraken *kraken = usb_get_intfdata(intf);
	struct usb_device *udev = interface_to_usbdev(intf);

	int retval = 0;

	kraken->pump_message[1] = kraken->speed;
	kraken->fan_message[1] = kraken->speed;

	kraken->color_message[1] = kraken->color.r;
	kraken->color_message[2] = kraken->color.g;
	kraken->color_message[3] = kraken->color.b;

	kraken->color_message[4] = kraken->alternate_color.r;
	kraken->color_message[5] = kraken->alternate_color.g;
	kraken->color_message[6] = kraken->alternate_color.b;

	kraken->color_message[11] = kraken->interval; kraken->color_message[12] = kraken->interval;

	kraken->color_message[13] = kraken->mode == off ? 0 : 1;
	kraken->color_message[14] = kraken->mode == alternating ? 1 : 0;
	kraken->color_message[15] = kraken->mode == blinking ? 1 : 0;

	if (
		(retval = kraken_start_transaction(udev)) ||
		(retval = kraken_send_pump_message(udev, kraken)) ||
		(retval = kraken_send_color_message(udev, kraken)) ||
		(retval = kraken_receive_status_message(udev, kraken)) ||
		(retval = kraken_start_transaction(udev)) ||
		(retval = kraken_send_fan_message(udev, kraken)) ||
		(retval = kraken_receive_status_message(udev, kraken))
	   )
		dev_err(dev, "Failed to update: %d\n", retval);

	kraken->fan = 256 * kraken->status_message[0] + kraken->status_message[1];
	kraken->pump = 256 * kraken->status_message[8] + kraken->status_message[9];
	kraken->temp = kraken->status_message[10];
}

static ssize_t show_speed(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_kraken *kraken = usb_get_intfdata(intf);

	return sprintf(buf, "%u\n", kraken->speed);
}

static ssize_t set_speed(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_kraken *kraken = usb_get_intfdata(intf);

	u8 speed;
	if (sscanf(buf, "%hhu", &speed) != 1 || speed < 30 || speed > 100 || speed % 5 != 0)
		return -EINVAL;

	kraken->speed = speed;

	kraken_update(dev);

	return count;
}

static DEVICE_ATTR(speed, S_IRUGO | S_IWUSR | S_IWGRP, show_speed, set_speed);

static ssize_t show_color(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_kraken *kraken = usb_get_intfdata(intf);

	return sprintf(buf, "%02x%02x%02x\n", kraken->color.r, kraken->color.g, kraken->color.b);
}

static ssize_t set_color(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_kraken *kraken = usb_get_intfdata(intf);

	u8 r, g, b;
	if (sscanf(buf, "%02hhx%02hhx%02hhx", &r, &g, &b) != 3)
		return -EINVAL;

	kraken->color.r = r;
	kraken->color.g = g;
	kraken->color.b = b;

	kraken_update(dev);

	return count;
}

static DEVICE_ATTR(color, S_IRUGO | S_IWUSR | S_IWGRP, show_color, set_color);

static ssize_t show_alternate_color(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_kraken *kraken = usb_get_intfdata(intf);

	return sprintf(buf, "%02x%02x%02x\n", kraken->alternate_color.r, kraken->alternate_color.g, kraken->alternate_color.b);
}

static ssize_t set_alternate_color(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_kraken *kraken = usb_get_intfdata(intf);

	u8 r, g, b;
	if (sscanf(buf, "%02hhx%02hhx%02hhx", &r, &g, &b) != 3)
		return -EINVAL;

	kraken->alternate_color.r = r;
	kraken->alternate_color.g = g;
	kraken->alternate_color.b = b;

	kraken_update(dev);

	return count;
}

static DEVICE_ATTR(alternate_color, S_IRUGO | S_IWUSR | S_IWGRP, show_alternate_color, set_alternate_color);

static ssize_t show_interval(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_kraken *kraken = usb_get_intfdata(intf);

	return sprintf(buf, "%u\n", kraken->interval);
}

static ssize_t set_interval(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_kraken *kraken = usb_get_intfdata(intf);

	u8 interval;
	if (sscanf(buf, "%hhu", &interval) != 1 || interval == 0)
		return -EINVAL;

	kraken->interval = interval;

	kraken_update(dev);

	return count;
}

static DEVICE_ATTR(interval, S_IRUGO | S_IWUSR | S_IWGRP, show_interval, set_interval);

static ssize_t show_mode(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_kraken *kraken = usb_get_intfdata(intf);

	switch (kraken->mode)
	{
		case normal:
			return sprintf(buf, "normal\n");
		case alternating:
			return sprintf(buf, "alternating\n");
		case blinking:
			return sprintf(buf, "blinking\n");
		case off:
			return sprintf(buf, "off\n");
		default:
			return sprintf(buf, "unknown\n");
	}
}

static ssize_t set_mode(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_kraken *kraken = usb_get_intfdata(intf);

	if (strncasecmp(buf, "normal", strlen("normal")) == 0)
		kraken->mode = normal;
	else if (strncasecmp(buf, "alternating", strlen("alternating")) == 0)
		kraken->mode = alternating;
	else if (strncasecmp(buf, "blinking", strlen("blinking")) == 0)
		kraken->mode = blinking;
	else if (strncasecmp(buf, "off", strlen("off")) == 0)
		kraken->mode = off;
	else
		return -EINVAL;

	kraken_update(dev);

	return count;
}

static DEVICE_ATTR(mode, S_IRUGO | S_IWUSR | S_IWGRP, show_mode, set_mode);

static ssize_t show_temp(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_kraken *kraken = usb_get_intfdata(intf);

	kraken_update(dev);

	return sprintf(buf, "%u\n", kraken->temp);
}

static DEVICE_ATTR(temp, S_IRUGO, show_temp, NULL);

static ssize_t show_pump(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_kraken *kraken = usb_get_intfdata(intf);

	kraken_update(dev);

	return sprintf(buf, "%u\n", kraken->pump);
}

static DEVICE_ATTR(pump, S_IRUGO, show_pump, NULL);

static ssize_t show_fan(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_kraken *kraken = usb_get_intfdata(intf);

	kraken_update(dev);

	return sprintf(buf, "%u\n", kraken->fan);
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

static int kraken_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct usb_kraken *dev = NULL;
	int retval = -ENOMEM;
	dev = kmalloc(sizeof(struct usb_kraken), GFP_KERNEL);
	if (!dev)
		goto error_mem;

	dev->udev = usb_get_dev(udev);
	usb_set_intfdata(interface, dev);

	dev->speed = 50;
	dev->color.r = 255; dev->color.g = 0; dev->color.b = 0;
	dev->alternate_color.r = 0; dev->alternate_color.g = 255; dev->alternate_color.b = 0;
	dev->interval = 1;
	dev->mode = normal;
	dev->temp = 0;
	dev->pump = 0;
	dev->fan = 0;

	// TODO: memleak if either of these allocations fail
	dev->pump_message = kmalloc(2 * sizeof(u8), GFP_KERNEL);
	if (!dev->pump_message)
		goto error_mem;
	dev->pump_message[0] = 0x13;

	dev->fan_message = kmalloc(2 * sizeof(u8), GFP_KERNEL);
	if (!dev->fan_message)
		goto error_mem;
	dev->fan_message[0] = 0x12;

	dev->color_message = kmalloc(19 * sizeof(u8), GFP_KERNEL);
	if (!dev->color_message)
		goto error_mem;
	dev->color_message[0] = 0x10;
	dev->color_message[7] = 0xff; dev->color_message[8] = 0x00; dev->color_message[9] = 0x00; dev->color_message[10] = 0x3c;
	dev->color_message[16] = 0x01; dev->color_message[17] = 0x00; dev->color_message[18] = 0x01;

	dev->status_message = kmalloc(64 * sizeof(u8), GFP_KERNEL);
	if (!dev->status_message)
		goto error_mem;

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
	return 0;
error:
	kraken_remove_device_files(interface);

	usb_set_intfdata(interface, NULL);
	usb_put_dev(dev->udev);

	kfree(dev->status_message);
	kfree(dev->color_message);
	kfree(dev->fan_message);
	kfree(dev->pump_message);
	kfree(dev);
error_mem:
	return retval;
}

static void kraken_disconnect(struct usb_interface *interface)
{
	struct usb_kraken *dev = usb_get_intfdata(interface);

	kraken_remove_device_files(interface);

	usb_set_intfdata(interface, NULL);
	usb_put_dev(dev->udev);

	kfree(dev->status_message);
	kfree(dev->color_message);
	kfree(dev->fan_message);
	kfree(dev->pump_message);
	kfree(dev);

	dev_info(&interface->dev, "Kraken disconnected\n");
}

module_usb_driver(kraken_driver);

MODULE_LICENSE("GPL");
