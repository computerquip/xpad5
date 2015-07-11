#include "xusb.h"

#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/spinlock_types.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/input.h>

/* XUSB_MAX_CONTROLLERS can be set to any arbitrary number.
   We make it 4 to match XInput. */

/* TODO:
     - Handle different controller types. Not sure how or why though...
     - Possibly remove locking. Need to step through each line of code...
     - Need to have hardware! I can't test enough. I don't even know
        what parts are fragile and what parts aren't at this point.
   TODO before requesting for patches:
     - Clean up commit history.
     - Fix data race between work queue and spinlocks. */

#define XINPUT_LIMIT 4

/* Table and mapping of the buttons. */
static const u16 xinput_button_table[12] = {
	XINPUT_GAMEPAD_START,
	XINPUT_GAMEPAD_BACK,
	XINPUT_GAMEPAD_LEFT_THUMB,
	XINPUT_GAMEPAD_RIGHT_THUMB,
	XINPUT_GAMEPAD_LEFT_SHOULDER,
	XINPUT_GAMEPAD_RIGHT_SHOULDER,
	XINPUT_GAMEPAD_GUIDE,
	XINPUT_GAMEPAD_RESERVED,
	XINPUT_GAMEPAD_A,
	XINPUT_GAMEPAD_B,
	XINPUT_GAMEPAD_X,
	XINPUT_GAMEPAD_Y
};

static const size_t xinput_button_table_sz =
  sizeof(xinput_button_table) / sizeof(xinput_button_table[0]);

static const int xinput_to_codes[12] = {
	BTN_START,      BTN_BACK,
	BTN_THUMBL,     BTN_THUMBR,
	BTN_TL,         BTN_TR,
	BTN_MODE,       0,
	BTN_A,          BTN_B,
	BTN_X,          BTN_Y,
};

/* We don't hold a static number of input work.
   Thus, it doesn't make sense to hold it within
   the xusb_context structure. */
struct xusb_input_work {
	XINPUT_GAMEPAD input;
	struct input_dev *input_dev;
	struct work_struct work;
};

struct xusb_context {
	u8 index;

	void *user_data;
	struct xusb_driver *driver;
	struct input_dev *input_dev;

	struct work_struct register_work;
	struct work_struct unregister_work;

	struct xusb_device *device;
};

static struct workqueue_struct *xusb_wq;

static struct xusb_context *xusb_index[4] = { 0 };
static DEFINE_SPINLOCK(xusb_index_lock);

static void xusb_setup_analog(struct input_dev *input_dev, int code, s16 res)
{
	if (res <= 0)
		return;

	input_set_capability(input_dev, EV_ABS, code);
	input_set_abs_params(input_dev, code, -res, res, 0, 0);
}

static void xusb_setup_trigger(struct input_dev *input_dev, int code, u8 res)
{
	input_set_capability(input_dev, EV_ABS, code);
	input_set_abs_params(input_dev, code, 0, res, 0, 0);
}

static void xusb_setup_hatswitch(struct input_dev *input_dev, int code)
{
	/* No point in checking the res... we must do this explicitly */
	input_set_capability(input_dev, EV_ABS, code);
	input_set_abs_params(input_dev, code, -1, 1, 0, 0);
}

static void xusb_handle_register(struct work_struct *pwork)
{
	struct xusb_context *ctx =
	  container_of(pwork, struct xusb_context, register_work);

	XINPUT_GAMEPAD *Gamepad = &ctx->device->caps->Gamepad;

	struct input_dev* input_dev = input_allocate_device();

	if (!input_dev) {
		printk(KERN_ERR "Failed to allocate device!\n");

		return;
	}

	for (int i = 0; i < xinput_button_table_sz; ++i) {
		if (Gamepad->wButtons & xinput_button_table[i]) {
			input_set_capability(
			  input_dev, EV_KEY, xinput_to_codes[i]);
		}
	}

	/* Unfortunately, for the DPad to use a hatswitch,
	   we can't iterate over the bits that indicate
	   hat-switch capability. Perhaps a FIXME. */
	if ((Gamepad->wButtons & XINPUT_GAMEPAD_DPAD_UP) &&
	    (Gamepad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN)) {
		xusb_setup_hatswitch(input_dev, ABS_HAT0Y);
	}

	if ((Gamepad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT) &&
	    (Gamepad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT)) {
		xusb_setup_hatswitch(input_dev, ABS_HAT0X);
	}

	xusb_setup_trigger(input_dev, ABS_Z, Gamepad->bLeftTrigger);
	xusb_setup_trigger(input_dev, ABS_RZ, Gamepad->bRightTrigger);
	xusb_setup_analog(input_dev, ABS_X, Gamepad->sThumbLX);
	xusb_setup_analog(input_dev, ABS_Y, Gamepad->sThumbLY);
	xusb_setup_analog(input_dev, ABS_RX, Gamepad->sThumbRX);
	xusb_setup_analog(input_dev, ABS_RY, Gamepad->sThumbRY);

	input_dev->name = ctx->device->name;

	if (input_register_device(input_dev) != 0) {
		printk(KERN_ERR "Failed to register input device!\n");
		input_free_device(input_dev);
		return;
	}

	ctx->input_dev = input_dev;

	if (ctx->index != -1) { 
		ctx->driver->set_led(ctx->user_data,
	  	    XINPUT_LED_ON_1 + ctx->index);
	}
}

static void xusb_handle_unregister(struct work_struct *pwork)
{
	struct xusb_context *ctx =
	  container_of(pwork, struct xusb_context, unregister_work);

	input_unregister_device(ctx->input_dev);
	ctx->input_dev = 0;
	ctx->user_data = 0;
	ctx->driver = 0;

	kfree(ctx);
}

static void xusb_handle_input(struct work_struct *pwork)
{
	struct xusb_input_work *ctx =
	  container_of(pwork, struct xusb_input_work, work);

	u16 buttons;

	if (!ctx->input_dev) {
		printk(KERN_ERR "Attempt to handle input for invalid input device!");
		return;
	}

	buttons = ctx->input.wButtons;
	/* The Input Subsystem checks for reported features each
	   time we submit an event. Inefficient but works for our case. */
	for (int i = 0; i < xinput_button_table_sz; ++i) {
		input_report_key(
		  ctx->input_dev,
		  xinput_to_codes[i],
		  buttons & xinput_button_table[i]);
	}

	input_report_abs(ctx->input_dev, ABS_HAT0X,
		!!(buttons & XINPUT_GAMEPAD_DPAD_RIGHT) - !!(buttons & XINPUT_GAMEPAD_DPAD_LEFT));

	input_report_abs(ctx->input_dev, ABS_HAT0Y,
		!!(buttons & XINPUT_GAMEPAD_DPAD_DOWN) - !!(buttons & XINPUT_GAMEPAD_DPAD_UP));

	input_report_abs(ctx->input_dev, ABS_Z, ctx->input.bLeftTrigger);
	input_report_abs(ctx->input_dev, ABS_RZ, ctx->input.bRightTrigger);

	input_report_abs(ctx->input_dev, ABS_X, ctx->input.sThumbLX);
	input_report_abs(ctx->input_dev, ABS_Y, ctx->input.sThumbLY);
	input_report_abs(ctx->input_dev, ABS_RX, ctx->input.sThumbRX);
	input_report_abs(ctx->input_dev, ABS_RY, ctx->input.sThumbRY);

	input_sync(ctx->input_dev);

	kfree(ctx);
}

struct xusb_context *xusb_register_device(
  struct xusb_driver *driver,
  struct xusb_device *device,
  void *context)
{
	int index = -1;
	unsigned long flags;
	struct xusb_context *ctx;

	/* FIXME: Should be allocated from a pre-allocated pool
	          specific to the xusb module.  */
	ctx = kmalloc(sizeof(struct xusb_context), GFP_ATOMIC);

	spin_lock_irqsave(&xusb_index_lock, flags);

	for (int i = 0; i < XINPUT_LIMIT; ++i) {
		if (xusb_index[i] != 0) {
			index = i;
		}
	}

	if (index >= 0)
		xusb_index[index] = ctx;
	else
		printk("More than 4 XInput controllers connected.");

	spin_unlock_irqrestore(&xusb_index_lock, flags);

	ctx->index = index;
	ctx->driver = driver;
	ctx->device = device;
	ctx->user_data = ctx;

	INIT_WORK(&ctx->register_work, xusb_handle_register);
	INIT_WORK(&ctx->unregister_work, xusb_handle_unregister);

	queue_work(xusb_wq, &ctx->register_work);

	return ctx;
}

void xusb_unregister_device(struct xusb_context *ctx)
{
	unsigned long flags;

	if (ctx->index != -1) {
		spin_lock_irqsave(&xusb_index_lock, flags);
		xusb_index[ctx->index] = 0;
		spin_unlock_irqrestore(&xusb_index_lock, flags);
	}

	queue_work(xusb_wq, &ctx->unregister_work);
}

void xusb_report_input(struct xusb_context *ctx, const XINPUT_GAMEPAD *input)
{
	struct xusb_input_work *input_work =
	kmalloc(sizeof(struct xusb_input_work), GFP_ATOMIC);

	if (!input_work)
			return;

	input_work->input = *input;
	input_work->input_dev = ctx->input_dev;

	INIT_WORK(&input_work->work, xusb_handle_input);
	queue_work(xusb_wq, &input_work->work);
}

void xusb_flush(void)
{
	flush_workqueue(xusb_wq);
}

EXPORT_SYMBOL_GPL(xusb_report_input);
EXPORT_SYMBOL_GPL(xusb_unregister_device);
EXPORT_SYMBOL_GPL(xusb_register_device);
EXPORT_SYMBOL_GPL(xusb_flush);

static int __init xusb_init(void)
{

	xusb_wq = alloc_ordered_workqueue("xusb", 0);

	if (xusb_wq == NULL) {
		return -ENOMEM;
	}

	return 0;
}

static void __exit xusb_exit(void)
{
	destroy_workqueue(xusb_wq);
}


MODULE_AUTHOR("Zachary Lund <admin@computerquip`m>");
MODULE_DESCRIPTION("Common Xbox Controller Interface");
MODULE_LICENSE("GPL");

module_init(xusb_init);
module_exit(xusb_exit);
