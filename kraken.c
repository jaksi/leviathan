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
};

static int kraken_probe(struct usb_interface *interface, const struct usb_device_id *id);
static void kraken_disconnect(struct usb_interface *interface);

static struct usb_driver kraken_driver = {
	.name =		"kraken",
	.probe =	kraken_probe,
	.disconnect =	kraken_disconnect,
	.id_table =	id_table,
};

// I'm deeply sorry for this function
static void send_stuff(struct device *dev)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_kraken *kraken = usb_get_intfdata(intf);
	struct usb_device *udev = interface_to_usbdev(intf);

	u8 *pmsg = kmalloc(2*sizeof(u8), GFP_KERNEL);
	u8 *fmsg = kmalloc(2*sizeof(u8), GFP_KERNEL);
	u8 *cmsg = kmalloc(19*sizeof(u8), GFP_KERNEL);
	u8 *smsg = kmalloc(64*sizeof(u8), GFP_KERNEL);
	int len;
	pmsg[0] = 0x13;
	fmsg[0] = 0x12;
	pmsg[1] = kraken->speed;
	fmsg[1] = kraken->speed;

	cmsg[0] = 0x10;

	cmsg[1] = kraken->color.r;
	cmsg[2] = kraken->color.g;
	cmsg[3] = kraken->color.b;

	cmsg[4] = kraken->alternate_color.r;
	cmsg[5] = kraken->alternate_color.g;
	cmsg[6] = kraken->alternate_color.b;

	cmsg[7] = 0xff; cmsg[8] = 0x00; cmsg[9] = 0x00; cmsg[10] = 0x3c;

	cmsg[11] = kraken->interval; cmsg[12] = kraken->interval;

	cmsg[13] = kraken->mode == off ? 0 : 1;
	cmsg[14] = kraken->mode == alternating ? 1 : 0;
	cmsg[15] = kraken->mode == blinking ? 1 : 0;

	cmsg[16] = 0x01; cmsg[17] = 0x00; cmsg[18] = 0x01;

	// start
	usb_control_msg(udev, usb_sndctrlpipe(udev, 0), 2, 0x40, 0x0001, 0, NULL, 0, 1000);
	// pump
	usb_bulk_msg(udev, usb_sndbulkpipe(udev, 2), pmsg, 2, &len, 3000);
	// color
	usb_bulk_msg(udev, usb_sndbulkpipe(udev, 2), cmsg, 19, &len, 3000);
	// status
	usb_bulk_msg(udev, usb_rcvbulkpipe(udev, 2), smsg, 64, &len, 3000);

	// start
	usb_control_msg(udev, usb_sndctrlpipe(udev, 0), 2, 0x40, 0x0001, 0, NULL, 0, 1000);
	// fan
	usb_bulk_msg(udev, usb_sndbulkpipe(udev, 2), fmsg, 2, &len, 3000);
	// status
	usb_bulk_msg(udev, usb_rcvbulkpipe(udev, 2), smsg, 64, &len, 3000);

	kraken->fan = 256 * smsg[0] + smsg[1];
	kraken->pump = 256 * smsg[8] + smsg[9];
	kraken->temp = smsg[10];

	kfree(cmsg); kfree(fmsg); kfree(pmsg); kfree(smsg);
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

	kraken->speed = simple_strtoul(buf, NULL, 10);

	send_stuff(dev);

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

	u32 color = simple_strtoul(buf, NULL, 16);
	kraken->color.r = color >> 16 & 0xff;
	kraken->color.g = color >>  8 & 0xff;
	kraken->color.b = color >>  0 & 0xff;

	send_stuff(dev);

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

	u32 color = simple_strtoul(buf, NULL, 16);
	kraken->alternate_color.r = color >> 16 & 0xff;
	kraken->alternate_color.g = color >>  8 & 0xff;
	kraken->alternate_color.b = color >>  0 & 0xff;

	send_stuff(dev);

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

	kraken->interval = simple_strtoul(buf, NULL, 10);

	send_stuff(dev);

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
			return sprintf(buf, "there's trouble\n");
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

	send_stuff(dev);

	return count;
}

static DEVICE_ATTR(mode, S_IRUGO | S_IWUSR | S_IWGRP, show_mode, set_mode);

static ssize_t show_temp(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_kraken *kraken = usb_get_intfdata(intf);

	send_stuff(dev);

	return sprintf(buf, "%u\n", kraken->temp);
}

static ssize_t set_temp(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static DEVICE_ATTR(temp, S_IRUGO, show_temp, set_temp);

static ssize_t show_pump(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_kraken *kraken = usb_get_intfdata(intf);

	send_stuff(dev);

	return sprintf(buf, "%u\n", kraken->pump);
}

static ssize_t set_pump(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static DEVICE_ATTR(pump, S_IRUGO, show_pump, set_pump);

static ssize_t show_fan(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_kraken *kraken = usb_get_intfdata(intf);

	send_stuff(dev);

	return sprintf(buf, "%u\n", kraken->fan);
}

static ssize_t set_fan(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static DEVICE_ATTR(fan, S_IRUGO, show_fan, set_fan);

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
	dev->mode = alternating;
	dev->temp = 0;
	dev->pump = 0;
	dev->fan = 0;

	retval = device_create_file(&interface->dev, &dev_attr_speed);
	if (retval)
		goto error;
	retval = device_create_file(&interface->dev, &dev_attr_color);
	if (retval)
		goto error;
	retval = device_create_file(&interface->dev, &dev_attr_alternate_color);
	if (retval)
		goto error;
	retval = device_create_file(&interface->dev, &dev_attr_interval);
	if (retval)
		goto error;
	retval = device_create_file(&interface->dev, &dev_attr_mode);
	if (retval)
		goto error;
	retval = device_create_file(&interface->dev, &dev_attr_temp);
	if (retval)
		goto error;
	retval = device_create_file(&interface->dev, &dev_attr_pump);
	if (retval)
		goto error;
	retval = device_create_file(&interface->dev, &dev_attr_fan);
	if (retval)
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

	kfree(dev);

	dev_info(&interface->dev, "Kraken disconnected\n");
}

module_usb_driver(kraken_driver);

MODULE_LICENSE("GPL");
