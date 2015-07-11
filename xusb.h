#pragma once

#include <linux/types.h>

#define XINPUT_DEVTYPE_GAMEPAD          0x01

#define XINPUT_DEVSUBTYPE_GAMEPAD       0x01
#define XINPUT_DEVSUBTYPE_WHEEL         0x02
#define XINPUT_DEVSUBTYPE_ARCADE_STICK  0x03
#define XINPUT_DEVSUBTYPE_FLIGHT_SICK   0x04
#define XINPUT_DEVSUBTYPE_DANCE_PAD     0x05
#define XINPUT_DEVSUBTYPE_GUITAR        0x06
#define XINPUT_DEVSUBTYPE_DRUM_KIT      0x08

/* FIXME! These should correspond to the packets!
   DO NOT ASSUME THESE FOLLOW THE PACKETS SINCE
   THESE ARENT DEFINED IN THE PUBLIC HEADER. */
#define XINPUT_CAPS_FFB_SUPPORTED       0x0001
/* END WARNING */

#define XINPUT_CAPS_VOICE_SUPPORTED     0x0004

#define XINPUT_GAMEPAD_DPAD_UP          0x0001
#define XINPUT_GAMEPAD_DPAD_DOWN        0x0002
#define XINPUT_GAMEPAD_DPAD_LEFT        0x0004
#define XINPUT_GAMEPAD_DPAD_RIGHT       0x0008
#define XINPUT_GAMEPAD_START            0x0010
#define XINPUT_GAMEPAD_BACK             0x0020
#define XINPUT_GAMEPAD_LEFT_THUMB       0x0040
#define XINPUT_GAMEPAD_RIGHT_THUMB      0x0080
#define XINPUT_GAMEPAD_LEFT_SHOULDER    0x0100
#define XINPUT_GAMEPAD_RIGHT_SHOULDER   0x0200
#define XINPUT_GAMEPAD_GUIDE            0x0400
#define XINPUT_GAMEPAD_RESERVED         0x0800
#define XINPUT_GAMEPAD_A                0x1000
#define XINPUT_GAMEPAD_B                0x2000
#define XINPUT_GAMEPAD_X                0x4000
#define XINPUT_GAMEPAD_Y                0x8000

#define XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE  7849
#define XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE 8689
#define XINPUT_GAMEPAD_TRIGGER_THRESHOLD    30

#define XINPUT_FLAG_GAMEPAD             0x00000001

#define BATTERY_DEVTYPE_GAMEPAD         0x00
#define BATTERY_DEVTYPE_HEADSET         0x01

#define BATTERY_TYPE_DISCONNECTED       0x00    // This device is not connected
#define BATTERY_TYPE_WIRED              0x01    // Wired device, no battery
#define BATTERY_TYPE_ALKALINE           0x02    // Alkaline battery source
#define BATTERY_TYPE_NIMH               0x03    // Nickel Metal Hydride battery source
#define BATTERY_TYPE_UNKNOWN            0xFF    // Cannot determine the battery type

#define BATTERY_LEVEL_EMPTY             0x00
#define BATTERY_LEVEL_LOW               0x01
#define BATTERY_LEVEL_MEDIUM            0x02
#define BATTERY_LEVEL_FULL              0x03


#define XUSER_MAX_COUNT                 4
#define XUSER_INDEX_ANY                 0x000000FF

#define VK_PAD_A                        0x5800
#define VK_PAD_B                        0x5801
#define VK_PAD_X                        0x5802
#define VK_PAD_Y                        0x5803
#define VK_PAD_RSHOULDER                0x5804
#define VK_PAD_LSHOULDER                0x5805
#define VK_PAD_LTRIGGER                 0x5806
#define VK_PAD_RTRIGGER                 0x5807

#define VK_PAD_DPAD_UP                  0x5810
#define VK_PAD_DPAD_DOWN                0x5811
#define VK_PAD_DPAD_LEFT                0x5812
#define VK_PAD_DPAD_RIGHT               0x5813
#define VK_PAD_START                    0x5814
#define VK_PAD_BACK                     0x5815
#define VK_PAD_LTHUMB_PRESS             0x5816
#define VK_PAD_RTHUMB_PRESS             0x5817

#define VK_PAD_LTHUMB_UP                0x5820
#define VK_PAD_LTHUMB_DOWN              0x5821
#define VK_PAD_LTHUMB_RIGHT             0x5822
#define VK_PAD_LTHUMB_LEFT              0x5823
#define VK_PAD_LTHUMB_UPLEFT            0x5824
#define VK_PAD_LTHUMB_UPRIGHT           0x5825
#define VK_PAD_LTHUMB_DOWNRIGHT         0x5826
#define VK_PAD_LTHUMB_DOWNLEFT          0x5827

#define VK_PAD_RTHUMB_UP                0x5830
#define VK_PAD_RTHUMB_DOWN              0x5831
#define VK_PAD_RTHUMB_RIGHT             0x5832
#define VK_PAD_RTHUMB_LEFT              0x5833
#define VK_PAD_RTHUMB_UPLEFT            0x5834
#define VK_PAD_RTHUMB_UPRIGHT           0x5835
#define VK_PAD_RTHUMB_DOWNRIGHT         0x5836
#define VK_PAD_RTHUMB_DOWNLEFT          0x5837

#define XINPUT_KEYSTROKE_KEYDOWN        0x0001
#define XINPUT_KEYSTROKE_KEYUP          0x0002
#define XINPUT_KEYSTROKE_REPEAT         0x0004

typedef struct _XINPUT_VIBRATION {
	u16 wLeftMotorSpeed;
	u16 wRightMotorSpeed;
} XINPUT_VIBRATION, *PXINPUT_VIBRATION;

typedef struct _XINPUT_GAMEPAD {
	u16 wButtons;
	u8  bLeftTrigger;
	u8  bRightTrigger;
	s16 sThumbLX;
	s16 sThumbLY;
	s16 sThumbRX;
	s16 sThumbRY;
} XINPUT_GAMEPAD, *PXINPUT_GAMEPAD;

typedef struct _XINPUT_CAPABILITIES {
	u8  Type;
	u8  SubType;
	u16 Flags;
	XINPUT_GAMEPAD   Gamepad;
	XINPUT_VIBRATION Vibration;
} XINPUT_CAPABILITIES, *PXINPUT_CAPABILITIES;

/* Driver-level definitions. */
struct xusb_context; /* Opaque type. */

enum XINPUT_LED_STATUS {
	XINPUT_LED_OFF,
	XINPUT_LED_ALL_BLINKING,
	XINPUT_LED_FLASH_ON_1,
	XINPUT_LED_FLASH_ON_2,
	XINPUT_LED_FLASH_ON_3,
	XINPUT_LED_FLASH_ON_4,
	XINPUT_LED_ON_1,
	XINPUT_LED_ON_2,
	XINPUT_LED_ON_3,
	XINPUT_LED_ON_4,
	XINPUT_LED_ROTATING,
	XINPUT_LED_SECTIONAL_BLINKING,
	XINPUT_LED_SLOW_SECTIONAL_BLINKING,
	XINPUT_LED_ALTERNATING
};

/* This interface follows closely the XInput API. We'll also
   support XInput-like requests in due time to provide WINE support. */

/*
   The Xbox controllers aren't very complicated. Input, LED, and Vibration
   is handled in a single USB interface via two endpoints, one read, one write.

   Voice is handled via separate USB interface that we do not mess with yet!
   As far as I know though, it's just very low quality raw PCM data being
   sent through the headphones... and raw PCM data being received from the mic.
 */

struct xusb_driver {
	/* Synonymous to a write callback. */
	void (*set_led)(void *, enum XINPUT_LED_STATUS);
	void (*set_vibration)(void *, XINPUT_VIBRATION);
};

struct xusb_device {
	const char *name;
	XINPUT_CAPABILITIES *caps;
};

/* The XUSB driver is driven by an single threaded workqueue.
   Each of these functions are generally driven by a work item
   submitted to that queue to help ease synchronization issues.
   In any case, the following functions are SAFE to call in
   interrupt context.

   Input events must be linearly processed anyways. Best case scenario
   is we have a second single threaded workqueue to handle
   connect/disconnect/write functionality... but we can't start
   accepting input events until we our device setup is finished.

   Perhaps we can have a single-threaded workqueue per controller?
   This driver will only allow 4 controllers max so no need for complex
   or flexible design here.
 */

struct xusb_context* xusb_register_device(
  struct xusb_driver *driver,
  struct xusb_device *device,
  void *context);

void xusb_unregister_device(struct xusb_context* ctx);

void xusb_report_input(struct xusb_context* ctx, const XINPUT_GAMEPAD *input);

void xusb_flush(void);
