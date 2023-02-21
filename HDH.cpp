#include <linux/init.h>
#include <linux/module.h>
#include <linux/usb/input.h>
#include <linux/hid.h>

MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("USB Keyboard Driver");
MODULE_LICENSE("GPL");

static struct usb_device_id usb_kbd_id_table[] = {
    { USB_INTERFACE_INFO(USB_INTERFACE_CLASS_HID, USB_INTERFACE_SUBCLASS_BOOT,
        USB_INTERFACE_PROTOCOL_KEYBOARD) },
    { } /* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, usb_kbd_id_table);

static int usb_kbd_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
    struct usb_device *dev = interface_to_usbdev(intf);
    struct input_dev *input_dev;
    int error;

    /* Allocate input device */
    input_dev = input_allocate_device();
    if (!input_dev) {
        dev_err(&intf->dev, "Failed to allocate input device\n");
        return -ENOMEM;
    }

    /* Set input device properties */
    input_dev->name = "USB Keyboard";
    input_dev->phys = "usb-xxxx:xx:xx.x-xx.x/input0"; /* Replace with appropriate USB device path */
    input_dev->id.bustype = BUS_USB;
    input_dev->id.vendor  = le16_to_cpu(dev->descriptor.idVendor);
    input_dev->id.product = le16_to_cpu(dev->descriptor.idProduct);
    input_dev->id.version = le16_to_cpu(dev->descriptor.bcdDevice);

    /* Set input device capabilities */
    __set_bit(EV_KEY, input_dev->evbit);
    for (int i = 0; i < KEY_MAX; i++)
        __set_bit(i, input_dev->keybit);

    /* Register input device */
    error = input_register_device(input_dev);
    if (error) {
        dev_err(&intf->dev, "Failed to register input device\n");
        input_free_device(input_dev);
        return error;
    }

    /* Save the input device in the USB interface data */
    usb_set_intfdata(intf, input_dev);

    dev_info(&intf->dev, "USB Keyboard device attached\n");

    return 0;
}

static void usb_kbd_disconnect(struct usb_interface *intf)
{
    struct input_dev *input_dev = usb_get_intfdata(intf);

    /* Unregister and free the input device */
    input_unregister_device(input_dev);
    input_free_device(input_dev);

    /* Clear the USB interface data */
    usb_set_intfdata(intf, NULL);

    dev_info(&intf->dev, "USB Keyboard device disconnected\n");
}

static struct usb_driver usb_kbd_driver = {
    .name = "usbkbd",
    .probe = usb_kbd_probe,
    .disconnect = usb_kbd_disconnect,
    .id_table = usb_kbd_id_table,
};

module_usb_driver(usb_kbd_driver);
