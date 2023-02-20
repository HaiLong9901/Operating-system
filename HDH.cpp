#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb/input.h>
#include <linux/hid.h>

static struct input_dev *myusb_kbd_dev;           //input_dev
static unsigned char *myusb_kbd_buf;     //Virtual address buffer
static dma_addr_t myusb_kbd_phyc;    //DMA buffer area;

static __le16 myusb_kbd_size;       //Packet length
static struct urb  *myusb_kbd_urb;     //urb

static const unsigned char usb_kbd_keycode[252] = {
    0,  0,  0,  0, 30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38,
    50, 49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44,  2,  3,
    4,  5,  6,  7,  8,  9, 10, 11, 28,  1, 14, 15, 57, 12, 13, 26,
    27, 43, 43, 39, 40, 41, 51, 52, 53, 58, 59, 60, 61, 62, 63, 64,
    65, 66, 67, 68, 87, 88, 99, 70,119,110,102,104,111,107,109,106,
    105,108,103, 69, 98, 55, 74, 78, 96, 79, 80, 81, 75, 76, 77, 71,
    72, 73, 82, 83, 86,127,116,117,183,184,185,186,187,188,189,190,
    191,192,193,194,134,138,130,132,128,129,131,137,133,135,136,113,
    115,114,  0,  0,  0,121,  0, 89, 93,124, 92, 94, 95,  0,  0,  0,
    122,123, 90, 91, 85,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    29, 42, 56,125, 97, 54,100,126,164,166,165,163,161,115,114,113,
    150,158,159,128,136,177,178,176,142,152,173,140
};       //The keyboard code table has a total of 252 data

 
void my_memcpy(unsigned char *dest,unsigned char *src,int len)      //Copy cache
{
       while(len--)
        {
            *dest++= *src++;
        }
}

static void myusb_kbd_irq(struct urb *urb) //Keyboard interrupt function
{
   static unsigned char buf1[8]={0,0,0,0,0,0,0,0};
   int i;
      /*Upload crtl, shift, atl, windows and other buttons*/
     for (i = 0; i < 8; i++)
     if(((myusb_kbd_buf[0]>>i)&1)!=((buf1[0]>>i)&1))
     {    
           input_report_key(myusb_kbd_dev, usb_kbd_keycode[i + 224], (myusb_kbd_buf[0]>> i) & 1);
           input_sync(myusb_kbd_dev); //Upload synchronization event
      }


     /*Upload normal button*/
    for(i=2;i<8;i++)
    if(myusb_kbd_buf[i]!=buf1[i])
    {
     if(myusb_kbd_buf[i] )      //Press event
    input_report_key(myusb_kbd_dev,usb_kbd_keycode[myusb_kbd_buf[i]], 1);   
    else  if(buf1[i])                                             //Release event
    input_report_key(myusb_kbd_dev,usb_kbd_keycode[buf1[i]], 0);
    input_sync(myusb_kbd_dev);   //Upload synchronization event
    }

  my_memcpy(buf1, myusb_kbd_buf, 8);       //update data    
  usb_submit_urb(myusb_kbd_urb, GFP_KERNEL);
}

static int myusb_kbd_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
      volatile unsigned char  i;
      struct usb_device *dev = interface_to_usbdev(intf); //equipment
      struct usb_endpoint_descriptor *endpoint;                            
      struct usb_host_interface *interface;    //Current interface
      int pipe;   //Endpoint pipeline
      interface=intf->cur_altsetting;  
      //The endpoint descriptor under the current interface                                                     
      endpoint = &interface->endpoint[0].desc;                                    
      printk("VID=%x,PID=%x\n",dev->descriptor.idVendor,dev->descriptor.idProduct);   

 /*  Allocate an input_dev structure  */
myusb_kbd_dev=input_allocate_device();

 /*  Set input_dev to support key events*/
       set_bit(EV_KEY, myusb_kbd_dev->evbit);
       set_bit(EV_REP, myusb_kbd_dev->evbit);//Support repeat press function

       for (i = 0; i < 252; i++)
       set_bit(usb_kbd_keycode[i], myusb_kbd_dev->keybit); //Add all keys
       clear_bit(0, myusb_kbd_dev->keybit);

 /*   Register the input_dev structure*/
       input_register_device(myusb_kbd_dev);

 /*   Set up USB keyboard data transfer */
 /* Create an endpoint pipe via usb_rcvintpipe()*/
       pipe=usb_rcvintpipe(dev,endpoint->bEndpointAddress); 

  /* Apply for USB buffer through usb_buffer_alloc()*/
      myusb_kbd_size=endpoint->wMaxPacketSize;
      myusb_kbd_buf=usb_buffer_alloc(dev,myusb_kbd_size,GFP_ATOMIC,&myusb_kbd_phyc);

  /* Apply and initialize the urb structure through usb_alloc_urb() and usb_fill_int_urb() */
       myusb_kbd_urb=usb_alloc_urb(0,GFP_KERNEL);
       usb_fill_int_urb (myusb_kbd_urb,  //urb structure
                         dev,        //usb device
                         pipe,       //Endpoint pipeline
                         myusb_kbd_buf,       //Buffer address
                         myusb_kbd_size,      //Data length
                         myusb_kbd_irq,      //Interrupt function
                         0,
                         endpoint->bInterval);  //Interruption time
 
  /* Because our 2440 supports DMA, we need to tell the urb structure to use the DMA buffer address*/
       myusb_kbd_urb->transfer_dma = myusb_kbd_phyc;//Set DMA address
       myusb_kbd_urb->transfer_flags   =URB_NO_TRANSFER_DMA_MAP;     //Set to use DMA address
/* Use usb_submit_urb() to submit urb*/
       usb_submit_urb(myusb_kbd_urb, GFP_KERNEL);   
       return 0;
}

static void myusb_kbd_disconnect(struct usb_interface *intf)
{
    struct usb_device *dev = interface_to_usbdev(intf);        //equipment
    usb_kill_urb(myusb_kbd_urb);
    usb_free_urb(myusb_kbd_urb);
    usb_buffer_free(dev, myusb_kbd_size,
				myusb_kbd_buf,myusb_kbd_phyc);
    //Log out of input_dev in the kernel
    input_unregister_device(myusb_kbd_dev); 
    input_free_device(myusb_kbd_dev);  //Release input_dev
}

static struct usb_device_id myusb_kbd_id_table [] = {
    { USB_INTERFACE_INFO(
         USB_INTERFACE_CLASS_HID, //Interface class: hid class
         USB_INTERFACE_SUBCLASS_BOOT, //Subclass: start device class
         USB_INTERFACE_PROTOCOL_KEYBOARD) }, //USB keyboard protocol 
};

static struct usb_driver myusb_kbd_drv = {
       .name            = "myusb_kbd",
       .probe           = myusb_kbd_probe,                        
       .disconnect     = myusb_kbd_disconnect,
       .id_table  = myusb_kbd_id_table,
};

/*Entry function*/
static int myusb_kbd_init(void)
{ 
       usb_register(&myusb_kbd_drv);
       return 0;
}
 
/*Export function*/
static void myusb_kbd_exit(void)
{
       usb_deregister(&myusb_kbd_drv);
}

module_init(myusb_kbd_init);
module_exit(myusb_kbd_exit);
MODULE_LICENSE("GPL");

