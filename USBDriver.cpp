#define POLLING_RATE 250
#define ACCEL_RECIPROCAL 1
#define SCROLL_RATIO 5
#define BMAX 900

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb/input.h>
#include <linux/hid.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/unistd.h>
#include <linux/buffer_head.h>
#include <asm/segment.h>
#include <asm/uaccess.h>

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE(DRIVER_LICENSE);

// struct file *filehandle;
// char brightness_buff[6];
// static int brightness;

struct usb_mouse
{
    char name[128];
    char phys[64];
    struct usb_device *usbdev;
    struct input_dev *dev;
    // USB Request Block (URB)
    struct urb *irq;
    // Data from mouse
    signed char *data;
    dma_addr_t data_dma;
};

static void usb_mouse_irq(struct urb *urb)
{
    struct usb_mouse *mouse = urb->context;
    signed char *data = mouse->data;
    struct input_dev *dev = mouse->dev;
    int status;

    // acceleration happens here
    signed int pr = POLLING_RATE;
    signed int accel_r = ACCEL_RECIPROCAL * 1000;
    signed int delta_x = data[2];
    signed int delta_y = data[4];
    signed int rate = int_sqrt(delta_x * delta_x + delta_y * delta_y);
    signed int accel_x = (rate * delta_x) * pr / accel_r;
    signed int accel_y = (rate * delta_y) * pr / accel_r;

    delta_x += accel_x;
    delta_y += accel_y;

    switch (urb->status)
    {
    case 0: /* success */
        break;
    case -ECONNRESET: /* unlink */
    case -ENOENT:
    case -ESHUTDOWN:
        return;
        /* -EPIPE:  should clear the halt */
    default: /* error */
        goto resubmit;
    }
    input_report_key(dev, BTN_LEFT, data[1] & 0x01);
    input_report_key(dev, BTN_RIGHT, data[1] & 0x02);
    input_report_key(dev, BTN_MIDDLE, data[1] & 0x04);
    input_report_key(dev, BTN_SIDE, data[1] & 0x08);
    input_report_key(dev, BTN_EXTRA, data[1] & 0x10);

    input_report_rel(dev, REL_X, delta_x);
    input_report_rel(dev, REL_Y, delta_y);
    input_report_rel(dev, REL_WHEEL, data[6]<<SCROLL_RATIO);

    input_sync(dev);
    // filehandle = filp_open("/home/long/Documents/mousedriver/Info", 0, 0);
    // kernel_read((struct file *)filehandle, brightness_buff, 2, 0);
    // brightness = (brightness_buff[0] - '0') * 10;
    // brightness += (brightness_buff[1] - '0');
    // if ((data[1] & 0x01) > 0)
    // {
    //  brightness += 5;
    //  if (brightness > BMAX)
    //      brightness = BMAX;
    //  if (brightness < 500)
    //      brightness = 500;
    // }
    // else if ((data[1] & 0x02) > 0)
    // {
    //  brightness -= 5;
    //  if (brightness > BMAX)
    //      brightness = BMAX;
    //  if (brightness < 500)
    //      brightness = 500;
    // }
    // brightness_buff[0] = brightness / 10 + '0';
    // brightness_buff[1] = brightness % 10 + '0';
    // filp_close((struct file *)filehandle, NULL);
    // filehandle = filp_open("/home/long/Documents/mousedriver/Info", 1, 0);
    // kernel_write((struct file *)filehandle, brightness_buff, 2, 0);
    // filp_close((struct file *)filehandle, NULL);
resubmit:
    status = usb_submit_urb(urb, GFP_ATOMIC);
    if (status)
        dev_err(&mouse->usbdev->dev,
                "can't resubmit intr, %s-%s/input0, status %d\n",
                mouse->usbdev->bus->bus_name,
                mouse->usbdev->devpath, status);
}

static int usb_mouse_open(struct input_dev *dev)
{
    struct usb_mouse *mouse = input_get_drvdata(dev);

    mouse->irq->dev = mouse->usbdev;
    if (usb_submit_urb(mouse->irq, GFP_KERNEL))
        return -EIO;

    return 0;
}

static void usb_mouse_close(struct input_dev *dev)
{
    struct usb_mouse *mouse = input_get_drvdata(dev);

    usb_kill_urb(mouse->irq);
}

static int usb_mouse_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
    struct usb_device *dev = interface_to_usbdev(intf);
    struct usb_host_interface *interface;
    struct usb_endpoint_descriptor *endpoint;
    struct usb_mouse *mouse;
    struct input_dev *input_dev;
    int pipe, maxp;
    int error = -ENOMEM;

    interface = intf->cur_altsetting;

    // Get endpoint information
    if (interface->desc.bNumEndpoints != 1)
        return -ENODEV;

    endpoint = &interface->endpoint[0].desc;
    if (!usb_endpoint_is_int_in(endpoint))
        return -ENODEV;

    pipe = usb_rcvintpipe(dev, endpoint->bEndpointAddress);
    maxp = usb_maxpacket(dev, pipe, usb_pipeout(pipe));

    // Alloc srtuct usb_mouse
    mouse = kzalloc(sizeof(struct usb_mouse), GFP_KERNEL);
    input_dev = input_allocate_device();
    if (!mouse || !input_dev)
        goto fail1;

    mouse->data = usb_alloc_coherent(dev, 8, GFP_ATOMIC, &mouse->data_dma);
    if (!mouse->data)
        goto fail1;

    mouse->irq = usb_alloc_urb(0, GFP_KERNEL);
    if (!mouse->irq)
goto fail2;

    mouse->usbdev = dev;
    mouse->dev = input_dev;

    if (dev->manufacturer)
        strlcpy(mouse->name, dev->manufacturer, sizeof(mouse->name));

    if (dev->product)
    {
        if (dev->manufacturer)
            strlcat(mouse->name, " ", sizeof(mouse->name));
        strlcat(mouse->name, dev->product, sizeof(mouse->name));
    }

    if (!strlen(mouse->name))
        snprintf(mouse->name, sizeof(mouse->name),
                 "USB HIDBP Mouse %04x:%04x",
                 le16_to_cpu(dev->descriptor.idVendor),
                 le16_to_cpu(dev->descriptor.idProduct));

    usb_make_path(dev, mouse->phys, sizeof(mouse->phys));
    strlcat(mouse->phys, "/input0", sizeof(mouse->phys));

    input_dev->name = mouse->name;
    input_dev->phys = mouse->phys;
    usb_to_input_id(dev, &input_dev->id);
    input_dev->dev.parent = &intf->dev;

    input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REL);
    input_dev->keybit[BIT_WORD(BTN_MOUSE)] = BIT_MASK(BTN_LEFT) |
                                             BIT_MASK(BTN_RIGHT) | BIT_MASK(BTN_MIDDLE);
    input_dev->relbit[0] = BIT_MASK(REL_X) | BIT_MASK(REL_Y);
    input_dev->keybit[BIT_WORD(BTN_MOUSE)] |= BIT_MASK(BTN_SIDE) |
                                              BIT_MASK(BTN_EXTRA);
    input_dev->relbit[0] |= BIT_MASK(REL_WHEEL);
input_set_drvdata(input_dev, mouse);

    input_dev->open = usb_mouse_open;
    input_dev->close = usb_mouse_close;

    usb_fill_int_urb(mouse->irq, dev, pipe, mouse->data,
                     (maxp > 8 ? 8 : maxp),
                     usb_mouse_irq, mouse, endpoint->bInterval);
    mouse->irq->transfer_dma = mouse->data_dma;
    mouse->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

    error = input_register_device(mouse->dev);
    if (error)
        goto fail3;

    // Save data & alloc
    usb_set_intfdata(intf, mouse);
    return 0;

fail3:
    usb_free_urb(mouse->irq);
fail2:
    usb_free_coherent(dev, 8, mouse->data, mouse->data_dma);
fail1:
    input_free_device(input_dev);
    kfree(mouse);
    return error;
}

static void usb_mouse_disconnect(struct usb_interface *intf)
{
    struct usb_mouse *mouse = usb_get_intfdata(intf);

    usb_set_intfdata(intf, NULL);
    if (mouse)
    {
        usb_kill_urb(mouse->irq);
        input_unregister_device(mouse->dev);
        usb_free_urb(mouse->irq);
        usb_free_coherent(interface_to_usbdev(intf), 8, mouse->data, mouse->data_dma);
        kfree(mouse);
    }
}

static struct usb_device_id usb_mouse_id_table[] = {
    {USB_INTERFACE_INFO(USB_INTERFACE_CLASS_HID, USB_INTERFACE_SUBCLASS_BOOT,
                        USB_INTERFACE_PROTOCOL_MOUSE)},
    {} /* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, usb_mouse_id_table);

static struct usb_driver usb_mouse_driver = {
    .name = "leetmouse",
    .probe = usb_mouse_probe,
    .disconnect = usb_mouse_disconnect,
    .id_table = usb_mouse_id_table,
};

module_usb_driver(usb_mouse_driver);

