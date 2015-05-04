#include "xusb.h"
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>

MODULE_AUTHOR("Zachary Lund <admin@computerquip.com>");
MODULE_DESCRIPTION("Xbox 360 Wireless Adapter Driver");
MODULE_LICENSE("GPL");

#define XBOX360WR_PACKET_SIZE 32

static XINPUT_CAPABILITIES xbox360wr_gamepad_caps = {
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

/* There's a finite amount of devices that we can
   match up to a table instead of dynamically
   generating the data on the fly. We're not
   actually provided with capability information
   anyways... the best we can get is an identifier
   for the device connected and even then, we're
   barely managing that with a serial we don't
   even know is reliable.*/
static struct xusb_device xbox360wr_devices[] = {
	{
		"Xbox 360 Wireless Receiver",
		&xbox360wr_gamepad_caps,
	}
};

struct xbox360wr_context {
	int index;

	struct usb_interface *usb_intf;
	struct urb *in;
	int pipe_out; /* I don't like the pipe... */
};

/* There's a lot of oddities with the outward packets.
   We don't know why or how they are...
   They're just from observing the packets from the
   Microsoft driver */

static void xbox360wr_set_vibration(
  void *data, XINPUT_VIBRATION ff)
{
#define VIBRATION_PACKET_SIZE 12
	struct xbox360wr_context *ctx = data;
	struct usb_device *usb_dev = interface_to_usbdev(ctx->usb_intf);

	int actual_length, error;

	u8 packet[VIBRATION_PACKET_SIZE] = {
		0x00, 0x01, 0x0F, 0xC0, 0x00,
		ff.wLeftMotorSpeed, ff.wRightMotorSpeed,
		0x00, 0x00, 0x00, 0x00, 0x00
	};

	error = usb_interrupt_msg(usb_dev, ctx->pipe_out,
	  packet, VIBRATION_PACKET_SIZE, &actual_length, 0);

	if (error) {
		printk(KERN_ERR "Error during submission."
		"Error code: %d - Actual Length %d\n", error, actual_length);
	}
}

static void xbox360wr_set_led(
  void *data, enum XINPUT_LED_STATUS led_status)
{
#define LED_PACKET_SIZE 10
	struct xbox360wr_context *ctx = data;
	struct usb_device *usb_dev = interface_to_usbdev(ctx->usb_intf);

	int actual_length, error;

	/* unknown_byte is probably to do with some sort of
	   message synchronization for things like timers.
	   It *seems* to work fine without those extra
	   packets however. I'm not sure about side effects. */
	const int unknown_byte = 0x08;

	u8 packet[LED_PACKET_SIZE] = {
		0x00, 0x00, unknown_byte, led_status + 0x40,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};

	error = usb_interrupt_msg(usb_dev, ctx->pipe_out,
	  packet, LED_PACKET_SIZE, &actual_length, 0);

	if (error) {
		printk(KERN_ERR "Error during submission."
		"Error code: %d - Actual Length %d\n", error, actual_length);
	}

}

static void xbox360wr_query_presence(struct xbox360wr_context *ctx)
{
#define PRESENCE_PACKET_SIZE 12
	int actual_length, error;
	struct usb_device *usb_dev = interface_to_usbdev(ctx->usb_intf);

	u8 packet[PRESENCE_PACKET_SIZE] = {
		0x08, 0x00, 0x0F, 0xC0,
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00
	};

	error =
	  usb_interrupt_msg(usb_dev, ctx->pipe_out,
	    packet, PRESENCE_PACKET_SIZE, &actual_length, 0);

	if (error) {
		printk(KERN_ERR "Error during submission."
		  "Error code: %d - Actual Length %d\n", error, actual_length);
	}

	/* Can't really do anything here... */
}

static struct xusb_driver xbox360wr_driver = {
	.set_led = xbox360wr_set_led,
	.set_vibration = xbox360wr_set_vibration
};

static void xpad360_parse_input(void *data, XINPUT_GAMEPAD *out)
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
static void xbox360wr_receive(struct urb* urb)
{
	struct xbox360wr_context *ctx = urb->context;
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

	/* Event from Adapter */
	if (data[0] == 0x08) {
		switch (data[1]) {
		case 0x00:
			/* Disconnect */

			/* This might happen if we request a
			   presence packet while we're disconnected */
			if (ctx->index < 0)
				break;

			xusb_unregister_device(ctx->index);
			ctx->index = -1;
			break;

		case 0xC0:
			/* Connect w/ Headset (attachment?) */
			/* We don't handle attachments. TODO */
		case 0x80: {
			/* Connect */

			/* Might happen if a presence packet is sent
			   while we're already connected */
			if (ctx->index != -1)
			 	break;

			ctx->index = xusb_register_device( /* HARDCODED FIXME */
				&xbox360wr_driver, &xbox360wr_devices[0], ctx);

			break;
		}

		case 0x40:
			/* Headset Connected (attachment?) */
			/* We don't handle attachments. TODO */
			break;
		}
	}
	/* Event from Controller */
	else if (data[0] == 0x00) {
		u16 header = le16_to_cpup((__le16*)&data[1]);

		switch (header) {
		case 0x0000: /* Unknown! */
			break;

		case 0x0001: { /* Input Event */
			XINPUT_GAMEPAD input;
			xpad360_parse_input(&data[6], &input);
			xusb_report_input(ctx->index, &input);
			break;
		}
		case 0x000A:
			/* Occurs after Headset Connection packet (0x40)
			   An arbitrarily sized description string
			   delimited by a series of 0xFF bytes. */
		case 0x0009:
			/* Occurs right after 0x000A. First two bytes are unknown.
			   14 bytes past that is the serial of the attachment. */
			break;
		case 0x01F8:
			/* Seems to be a PING or PONG type event. */
		case 0x02F8:
			/* Seems to complement 0x01F8 */
			break;
		case 0x000F:
			/* Announcement Packet. Unknown layout!
			   Occurs right after Controller Connection Packet (0x80)*/
			break;
		default:
			printk(KERN_ERR "Unknown packet receieved. Header was %#.8x\n", header);
		}
	}

finish:
	usb_submit_urb(urb, GFP_ATOMIC);
}

/* The wireless adapter will throw four interfaces at us,
   each one representing a controller. Initialization is
   very similar to the wired version. So much so, I'd imagine
   we can combine the code. However, I wont' do that for now
   for the sake of debugging and usability at this moment. */
static int xbox360wr_probe(struct usb_interface *intf,
	const struct usb_device_id *id)
{
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct usb_endpoint_descriptor *ep =
		&intf->cur_altsetting->endpoint[0].desc;

	const int pipe = usb_rcvintpipe(usb_dev, ep->bEndpointAddress);
	struct xbox360wr_context *ctx;

	int error = 0;
	void *in_buffer;
	dma_addr_t in_dma;

	ctx = kmalloc(sizeof(struct xbox360wr_context), GFP_KERNEL);

	if (!ctx) {
		return -ENOMEM;
	}

	usb_set_intfdata(intf, ctx);
	ctx->usb_intf = intf;
	ctx->index = -1;
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
		usb_dev, XBOX360WR_PACKET_SIZE,
		GFP_KERNEL, &in_dma);

	if (!in_buffer) {
		error = -ENOMEM;
		goto fail_alloc_coherent;
	}

	usb_fill_int_urb(
		ctx->in, usb_dev,
		pipe, in_buffer, XBOX360WR_PACKET_SIZE,
		xbox360wr_receive, ctx, ep->bInterval);

	error = usb_submit_urb(ctx->in, GFP_KERNEL);
	if (error) {
		error = -ENOMEM;
		goto fail_in_submit;
	}

	/* This will force the controller to resend connection packets.
	   This is useful in the case we activate the module after the
	   adapter has been plugged in, as it won't automatically
	   send us info about the controllers. */
	xbox360wr_query_presence(ctx);

	return 0;

fail_in_submit:
	usb_free_coherent(usb_dev, XBOX360WR_PACKET_SIZE, in_buffer, in_dma);
fail_alloc_coherent:
	usb_free_urb(ctx->in);
fail_urb_alloc:
	kfree(ctx);

	return error;
}

static void xbox360wr_disconnect(struct usb_interface *intf)
{
	struct xbox360wr_context *ctx = usb_get_intfdata(intf);
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct urb *in_urb = ctx->in;

	usb_kill_urb(in_urb);
	usb_free_coherent(usb_dev, XBOX360WR_PACKET_SIZE,
	in_urb->transfer_buffer, in_urb->transfer_dma);
	usb_free_urb(in_urb);

	if (ctx->index >= 0) {
		xusb_unregister_device(ctx->index);
		xusb_finish(ctx->index);
	}

	if (usb_dev->state != USB_STATE_NOTATTACHED)
		xbox360wr_set_led(ctx, XINPUT_LED_ALTERNATING);

	kfree(ctx);
}

static const struct usb_device_id xbox360wr_table[] = {
	{ USB_DEVICE_INTERFACE_PROTOCOL(0x045E, 0x0719, 129) },
	{}
};

static struct usb_driver xbox360wr_usb_driver = {
	.name = "xbox360wr",
	.id_table = xbox360wr_table,
	.probe = xbox360wr_probe,
	.disconnect = xbox360wr_disconnect,
	.soft_unbind = 1
};

module_usb_driver(xbox360wr_usb_driver);
