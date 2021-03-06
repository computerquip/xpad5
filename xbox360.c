#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include "xusb.h"

MODULE_AUTHOR("Zachary Lund <admin@computerquip.com>");
MODULE_DESCRIPTION("Wired Xbox 360 Controller Driver");
MODULE_LICENSE("GPL");

#define XBOX360_PACKET_SIZE 32

struct xbox360_context {
	struct xusb_context *xusb_ctx;

	struct usb_interface *usb_intf;
	struct urb *in;
	int pipe_out;
};

static XINPUT_CAPABILITIES xbox360_gamepad_caps = {
	.Type = XINPUT_DEVTYPE_GAMEPAD,
	.SubType = XINPUT_DEVSUBTYPE_GAMEPAD,
	.Flags = XINPUT_CAPS_FFB_SUPPORTED,
	.Gamepad = {
		.wButtons =
			XINPUT_GAMEPAD_DPAD_UP |
			XINPUT_GAMEPAD_DPAD_DOWN |
			XINPUT_GAMEPAD_DPAD_LEFT |
			XINPUT_GAMEPAD_DPAD_RIGHT |
			XINPUT_GAMEPAD_START |
			XINPUT_GAMEPAD_BACK |
			XINPUT_GAMEPAD_LEFT_THUMB |
			XINPUT_GAMEPAD_RIGHT_THUMB |
			XINPUT_GAMEPAD_LEFT_SHOULDER |
			XINPUT_GAMEPAD_RIGHT_SHOULDER |
			XINPUT_GAMEPAD_GUIDE |
			XINPUT_GAMEPAD_A |
			XINPUT_GAMEPAD_B |
			XINPUT_GAMEPAD_X |
			XINPUT_GAMEPAD_Y,
		.bLeftTrigger = 255,
		.bRightTrigger = 255,
		.sThumbLX = 32767,
		.sThumbLY = 32767,
		.sThumbRX = 32767,
		.sThumbRY = 32767
	},
	.Vibration = {
		.wLeftMotorSpeed = 65535,
		.wRightMotorSpeed = 65535
	}
};

static struct xusb_device xbox360_devices[] = {
	{
		"Microsoft X-Box 360 pad",
		&xbox360_gamepad_caps
	}
};

static struct usb_device_id xbox360_table[] = {
	{ USB_DEVICE_INTERFACE_PROTOCOL(0x045E, 0x028e, 1) },
	{}
};

static int xbox360_send(struct xbox360_context *ctx, void *data, int size)
{
	struct usb_device *usb_dev = interface_to_usbdev(ctx->usb_intf);
	int actual_length = 0;
	int error;

	if (usb_dev->state == USB_STATE_NOTATTACHED)
		return 0;

	error = usb_interrupt_msg(usb_dev, ctx->pipe_out,
	  data, size, &actual_length, 0);

	if (error) {
		printk(KERN_ERR "Error during submission."
		"Error code: %d - Actual Length %d\n", error, actual_length);
	}

	return actual_length;
}

static void xbox360_set_vibration(
  void *data, XINPUT_VIBRATION ff)
{
	printk(KERN_INFO "Setting vibration!\n");
}

static void xbox360_set_led(
  void *data, enum XINPUT_LED_STATUS led_status)
{
	struct xbox360_context *ctx = data;

	u8 packet[] = { 0x01, 0x03, led_status };

	xbox360_send(ctx, packet, sizeof(packet));
}

static void xpad360_parse_input(void *data, PXINPUT_GAMEPAD out)
{
	u8 *buffer = data;

	out->wButtons = le16_to_cpup((__le16*)&buffer[0]);
	out->bLeftTrigger = buffer[2];
	out->bRightTrigger = buffer[3];
	out->sThumbLX = (__s16)le16_to_cpup((__le16*)&buffer[4]);
	out->sThumbLY = (__s16)le16_to_cpup((__le16*)&buffer[6]);
	out->sThumbRX = (__s16)le16_to_cpup((__le16*)&buffer[8]);
	out->sThumbRY = (__s16)le16_to_cpup((__le16*)&buffer[10]);
}

/* Interrupt for incoming URB.  */
static void xbox360_receive(struct urb* urb)
{
	struct xbox360_context *context = urb->context;
	u8 *data = urb->transfer_buffer;

	switch (urb->status) {
	case 0:
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		return;
	default:
		goto finish;
	}

	/* Packets arrive respective to how the switch is laid out. */
	switch(le16_to_cpup((u16*)&data[0])) {
	case 0x0301: /* LED status */ /* What can we do with this? */
		break;
	case 0x0302: /* Unknown! */
	case 0x0303: /* Unknown! */
		break;
	case 0x0308: /* Attachment */
		break;
	case 0x1400: {
		XINPUT_GAMEPAD input;
		xpad360_parse_input(&data[2], &input);
		xusb_report_input(context->xusb_ctx, &input);
		break;
	}
	}

finish:
	usb_submit_urb(urb, GFP_ATOMIC);
}

static struct xusb_driver xbox360_driver = {
	.set_led = xbox360_set_led,
	.set_vibration = xbox360_set_vibration
};

static int xbox360_probe(struct usb_interface *intf,
	const struct usb_device_id *id)
{
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct usb_endpoint_descriptor *ep =
		&intf->cur_altsetting->endpoint[0].desc;

	const int pipe = usb_rcvintpipe(usb_dev, ep->bEndpointAddress);
	struct xbox360_context *ctx;

	int error = 0;
	void *in_buffer;
	dma_addr_t in_dma;

	ctx = kmalloc(sizeof(struct xbox360_context), GFP_KERNEL);

	if (!ctx) {
		return -ENOMEM;
	}

	usb_set_intfdata(intf, ctx);
	ctx->usb_intf = intf;
	ctx->pipe_out =
	  usb_sndintpipe(usb_dev,
	    intf->cur_altsetting->endpoint[1].desc.bEndpointAddress);

	ctx->in = usb_alloc_urb(0, GFP_KERNEL);
	if (!ctx->in) {
		error = -ENOMEM;
		goto fail_urb_alloc;
	}

	in_buffer =
	usb_alloc_coherent(
		usb_dev, XBOX360_PACKET_SIZE,
		GFP_KERNEL, &in_dma);

	if (!in_buffer) {
		error = -ENOMEM;
		goto fail_alloc_coherent;
	}

	usb_fill_int_urb(
		ctx->in, usb_dev,
		pipe, in_buffer, XBOX360_PACKET_SIZE,
		xbox360_receive, ctx, ep->bInterval);

	error = usb_submit_urb(ctx->in, GFP_KERNEL);
	if (error) {
		error = -ENOMEM;
		goto fail_in_submit;
	}

	ctx->xusb_ctx =
	  xusb_register_device(
	    &xbox360_driver,
	    &xbox360_devices[id - xbox360_table], ctx);

	if (ctx->xusb_ctx < 0) {
		error = -ENODEV;
		goto fail_xusb;
	}

	return 0;

fail_xusb:
	usb_kill_urb(ctx->in);
fail_in_submit:
	usb_free_coherent(usb_dev, XBOX360_PACKET_SIZE, in_buffer, in_dma);
fail_alloc_coherent:
	usb_free_urb(ctx->in);
fail_urb_alloc:
	kfree(ctx);

	return error;
}

static void xbox360_disconnect(struct usb_interface *intf)
{
	struct xbox360_context *ctx = usb_get_intfdata(intf);
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct urb *in_urb = ctx->in;

	usb_kill_urb(in_urb);
	usb_free_coherent(usb_dev, XBOX360_PACKET_SIZE,
	  in_urb->transfer_buffer, in_urb->transfer_dma);
	usb_free_urb(in_urb);
	xusb_unregister_device(ctx->xusb_ctx);

	xbox360_set_led(ctx, XINPUT_LED_ROTATING);

	xusb_flush();

	kfree(ctx);
}

static struct usb_driver xbox360_usb_driver = {
	.name = "xbox360",
	.id_table = xbox360_table,
	.probe = xbox360_probe,
	.disconnect = xbox360_disconnect,
	.soft_unbind = 1
};

module_usb_driver(xbox360_usb_driver);
