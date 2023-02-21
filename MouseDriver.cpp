#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/hid.h>

MODULE_LICENSE("GPL");

static struct usb_device_id usb_mouse_id_table [] = {
    { USB_INTERFACE_INFO(
        USB_INTERFACE_CLASS_HID, 
        USB_INTERFACE_SUBCLASS_BOOT,
        USB_INTERFACE_PROTOCOL_MOUSE) },
    { } /* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, usb_mouse_id_table);

static int usb_mouse_probe(struct usb_interface *interface, 
    const struct usb_device_id *id)
{
    printk(KERN_INFO "USB Mouse device detected\n");
    return 0;
}

static void usb_mouse_disconnect(struct usb_interface *interface)
{
    printk(KERN_INFO "USB Mouse device disconnected\n");
}

static struct usb_driver usb_mouse_driver = {
    .name = "usb_mouse_driver",
    .id_table = usb_mouse_id_table,
    .probe = usb_mouse_probe,
    .disconnect = usb_mouse_disconnect,
};

static int __init usb_mouse_init(void)
{
    int ret = usb_register(&usb_mouse_driver);
    if (ret)
        printk(KERN_ERR "usb_register failed. Error number %d", ret);

    return ret;
}

static void __exit usb_mouse_exit(void)
{
    usb_deregister(&usb_mouse_driver);
}

module_init(usb_mouse_init);
module_exit(usb_mouse_exit);
