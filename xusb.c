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
     - Clean up input spawning code.
     - Clean up various error scenarios. */

#define XUSB_MAX_CONTROLLERS 4

/* Table and mapping of the buttons. */
static const u16 xinput_button_table[16] = {
	XINPUT_GAMEPAD_DPAD_UP,
	XINPUT_GAMEPAD_DPAD_DOWN,
	XINPUT_GAMEPAD_DPAD_LEFT,
	XINPUT_GAMEPAD_DPAD_RIGHT,
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

static const int xinput_to_codes[16] = {
	BTN_DPAD_UP,    BTN_DPAD_DOWN,
	BTN_DPAD_LEFT,  BTN_DPAD_RIGHT,
	BTN_START,      BTN_BACK,
	BTN_THUMBR,     BTN_THUMBL,
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
	bool active;

	void *context;
	struct xusb_driver *driver;
	struct input_dev *input_dev;

	struct work_struct register_work;
	struct work_struct unregister_work;

	XINPUT_CAPABILITIES caps;
};

static DEFINE_SPINLOCK(index_lock);

static int num_connected = 0;

static struct workqueue_struct *xusb_wq[XUSB_MAX_CONTROLLERS] = { 0 };
static struct xusb_context xusb_ctx[XUSB_MAX_CONTROLLERS] = {{ 0 }};

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

static void xusb_handle_register(struct work_struct *pwork)
{
	struct xusb_context *ctx =
	  container_of(pwork, struct xusb_context, register_work);

#ifndef DISABLE_INPUT
	XINPUT_GAMEPAD *Gamepad = &ctx->caps.Gamepad;
	int i = 0;

	struct input_dev* input_dev = input_allocate_device();

	if (!input_dev) {
		printk(KERN_ERR "Failed to allocate device!");

		return;
	}

	for (; i < xinput_button_table_sz; ++i) {
		if (ctx->caps.Gamepad.wButtons & xinput_button_table[i]) {
			input_set_capability(
			  input_dev, EV_KEY, xinput_to_codes[i]);
		}
	}

	xusb_setup_trigger(input_dev, ABS_Z, Gamepad->bLeftTrigger);
	xusb_setup_trigger(input_dev, ABS_RZ, Gamepad->bRightTrigger);
	xusb_setup_analog(input_dev, ABS_X, Gamepad->sThumbLX);
	xusb_setup_analog(input_dev, ABS_Y, Gamepad->sThumbLY);
	xusb_setup_analog(input_dev, ABS_RX, Gamepad->sThumbRX);
	xusb_setup_analog(input_dev, ABS_RY, Gamepad->sThumbRY);

	if (input_register_device(input_dev) != 0) {
		printk(KERN_ERR "Failed to register input device!");
		input_free_device(input_dev);
		return;
	}

	ctx->input_dev = input_dev;
#endif
}

static void xusb_handle_unregister(struct work_struct *pwork)
{
	struct xusb_context *ctx =
	  container_of(pwork, struct xusb_context, unregister_work);

#ifndef DISABLE_INPUT
	input_unregister_device(ctx->input_dev);
#endif
}

static void xusb_handle_input(struct work_struct *pwork)
{
	struct xusb_input_work *ctx =
	  container_of(pwork, struct xusb_input_work, work);

	int i = 0;

	/* The Input Subsystem checks for reported features each
	   time we submit an event. Inefficient but works for our case. */
#ifndef DISABLE_INPUT
	for (; i < xinput_button_table_sz; ++i) {
		input_report_key(
		  ctx->input_dev,
		  xinput_to_codes[i],
		  ctx->input.wButtons & xinput_button_table[i]);
	}

	input_report_abs(ctx->input_dev, ABS_Z, ctx->input.bLeftTrigger);
	input_report_abs(ctx->input_dev, ABS_RZ, ctx->input.bRightTrigger);

	input_report_abs(ctx->input_dev, ABS_X, ctx->input.sThumbLX);
	input_report_abs(ctx->input_dev, ABS_Y, ctx->input.sThumbLY);
	input_report_abs(ctx->input_dev, ABS_RX, ctx->input.sThumbRX);
	input_report_abs(ctx->input_dev, ABS_RY, ctx->input.sThumbRY);

	input_sync(ctx->input_dev);

#endif
	kfree(ctx);
}

int xusb_reserve_index()
{
	unsigned long flags;
	u8 index = -1;
	int i = 0;

	spin_lock_irqsave(&index_lock, flags);

	/* Reject it, we're maxed out */
	if (num_connected == XUSB_MAX_CONTROLLERS) {
		return -1;
	}

	for (; i < XUSB_MAX_CONTROLLERS; ++i) {
		if (xusb_ctx[i].active == false) {
			index = i;
			break;
		}
	}

	xusb_ctx[index].active = true;

	spin_unlock_irqrestore(&index_lock, flags);

	return index;
}

void xusb_release_index(int index)
{
	unsigned long flags;

	spin_lock_irqsave(&index_lock, flags);

	xusb_ctx[index].active = false;

	spin_unlock_irqrestore(&index_lock, flags);
}

int xusb_register_device(
  int index,
  struct xusb_driver *xusb_driver,
  const XINPUT_CAPABILITIES *caps,
  void *context)
{
	unsigned long flags;

	spin_lock_irqsave(&index_lock, flags);

	xusb_ctx[index].active = true;
	xusb_ctx[index].caps = *caps;
	xusb_ctx[index].driver = xusb_driver;
	xusb_ctx[index].context = context;

	printk("Registering a device for index %i", index);

	queue_work(xusb_wq[index], &xusb_ctx[index].register_work);

	spin_unlock_irqrestore(&index_lock, flags);

	return index;
}

void xusb_unregister_device(int index)
{
	unsigned long flags;

	printk("Unregistering a device for index %i", index);

	spin_lock_irqsave(&index_lock, flags);

	if (index < 0 || index > 3) {
		printk(KERN_ERR "Attempt to unregister invalid index!");
		goto finish;
	}

	if (xusb_ctx[index].active == false) {
		printk(KERN_ERR "Attempt to unregister inactive index!");
		goto finish;
	}

	xusb_ctx[index].active = false;
	xusb_ctx[index].context = 0;
	xusb_ctx[index].driver = 0;

	queue_work(xusb_wq[index], &xusb_ctx[index].unregister_work);

finish:
	spin_unlock_irqrestore(&index_lock, flags);
}

void xusb_report_input(int index, const XINPUT_GAMEPAD *input)
{
	struct xusb_input_work *input_work =
	kmalloc(sizeof(struct xusb_input_work), GFP_ATOMIC);

	unsigned long flags;

	spin_lock_irqsave(&index_lock, flags);

	input_work->input = *input;
	input_work->input_dev = xusb_ctx[index].input_dev;

	INIT_WORK(&input_work->work, xusb_handle_input);
	queue_work(xusb_wq[index], &input_work->work);

	spin_unlock_irqrestore(&index_lock, flags);
}

EXPORT_SYMBOL_GPL(xusb_report_input);
EXPORT_SYMBOL_GPL(xusb_reserve_index);
EXPORT_SYMBOL_GPL(xusb_unregister_device);
EXPORT_SYMBOL_GPL(xusb_register_device);
EXPORT_SYMBOL_GPL(xusb_release_index);

static int __init xusb_init(void)
{
	int i = 0, k = 0;

	for (; i < XUSB_MAX_CONTROLLERS; ++i) {
		xusb_wq[i] = alloc_ordered_workqueue("xusb%d", 0, i);

		if (xusb_wq[i] == NULL) {
			for (; k < i; ++k) {
				destroy_workqueue(xusb_wq[k]);
			}

			return -ENOMEM;
		}

		INIT_WORK(&xusb_ctx[i].register_work, xusb_handle_register);
		INIT_WORK(&xusb_ctx[i].unregister_work, xusb_handle_unregister);
	}

	return 0;
}

static void __exit xusb_exit(void)
{
	int i = 0;

	for (; i < XUSB_MAX_CONTROLLERS; ++i) {
		if (xusb_wq[i] != NULL)
			destroy_workqueue(xusb_wq[i]);
	}
}


MODULE_AUTHOR("Zachary Lund <admin@computerquip.com>");
MODULE_DESCRIPTION("Common Xbox Controller Interface");
MODULE_LICENSE("GPL");

module_init(xusb_init);
module_exit(xusb_exit);
