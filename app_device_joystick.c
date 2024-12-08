/*******************************************************************************
Copyright 2016 Microchip Technology Inc. (www.microchip.com)

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

	http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

To request to license the code under the MLA license (www.microchip.com/mla_license),
please contact mla_licensing@microchip.com
*******************************************************************************/

#ifndef USBJOYSTICK_C
#define USBJOYSTICK_C

/** INCLUDES *******************************************************/
#include "usb.h"
#include "usb_device_hid.h"

#include "system.h"

#include "stdint.h"

/** DECLARATIONS ***************************************************/
// http://www.microsoft.com/whdc/archive/hidgame.mspx
#define HAT_SWITCH_NORTH			0x0
#define HAT_SWITCH_NORTH_EAST		0x1
#define HAT_SWITCH_EAST				0x2
#define HAT_SWITCH_SOUTH_EAST		0x3
#define HAT_SWITCH_SOUTH			0x4
#define HAT_SWITCH_SOUTH_WEST		0x5
#define HAT_SWITCH_WEST				0x6
#define HAT_SWITCH_NORTH_WEST		0x7
#define HAT_SWITCH_NULL				0x8

/** TYPE DEFINITIONS ************************************************/
typedef union _INTPUT_CONTROLS_TYPEDEF {
#if RPT_TYPE == 1
	// Simple (12 Buttons)
	struct {
		uint8_t dpad:4;

		uint8_t square:1;
		uint8_t x:1;
		uint8_t o:1;
		uint8_t triangle:1;

		uint8_t L1:1;
		uint8_t R1:1;
		uint8_t L2:1;
		uint8_t R2:1;

		uint8_t select:1;
		uint8_t start:1;
		uint8_t L3:1;
		uint8_t R3:1;
	} m;
	uint8_t val[2];
#elif RPT_TYPE == 2
	// Original (13 Buttons)
	struct {
		uint8_t square:1;
		uint8_t x:1;
		uint8_t o:1;
		uint8_t triangle:1;

		uint8_t L1:1;
		uint8_t R1:1;
		uint8_t L2:1;
		uint8_t R2:1;

		uint8_t select:1;
		uint8_t start:1;
		uint8_t L3:1;
		uint8_t R3:1;

		uint8_t home:1;
		uint8_t :3;		// filler

		uint8_t dpad;

		uint8_t X;
		uint8_t Y;
		uint8_t Z;
		uint8_t RZ;
	} m;
	uint8_t val[7];
#else
	// New (14 Buttons)
	struct {
		uint8_t X;
		uint8_t Y;
		uint8_t Z;
		uint8_t RZ;

		uint8_t dpad:4;

		uint8_t square:1;
		uint8_t x:1;
		uint8_t o:1;
		uint8_t triangle:1;

		uint8_t L1:1;
		uint8_t R1:1;
		uint8_t L2:1;
		uint8_t R2:1;

		uint8_t select:1;
		uint8_t start:1;
		uint8_t L3:1;
		uint8_t R3:1;

		uint8_t home:1;
		uint8_t tpad:1;
		uint8_t :6;		// filler
	} m;
	uint8_t val[7];
#endif
} INPUT_CONTROLS;

/** VARIABLES ******************************************************/
/* Some processors have a limited range of RAM addresses where the USB module
 * is able to access.  The following section is for those devices.  This section
 * assigns the buffers that need to be used by the USB module into those
 * specific areas.
 */
#if defined(FIXED_ADDRESS_MEMORY)
	#if defined(COMPILER_MPLAB_C18)
		#pragma udata JOYSTICK_DATA=JOYSTICK_DATA_ADDRESS
			INPUT_CONTROLS joystickInput;
		#pragma udata
	#elif defined(__XC8)
		INPUT_CONTROLS joystickInput JOYSTICK_DATA_ADDRESS;
	#endif
#else
	INPUT_CONTROLS joystickInput;
#endif

/// 最終データ
uint8_t lastData;

#if defined(EMULATE_XY)
/// 動作モード
uint8_t modeSwitch;
#endif	// EMULATE_XY

USB_VOLATILE USB_HANDLE lastTransmission;

/// 方向変換テーブル
static uint8_t hatTable[] = {
	HAT_SWITCH_NULL,		// 0000
	HAT_SWITCH_NORTH,		// 0001
	HAT_SWITCH_SOUTH,		// 0010
	HAT_SWITCH_NULL,		// 0011

	HAT_SWITCH_WEST,		// 0100
	HAT_SWITCH_NORTH_WEST,	// 0101
	HAT_SWITCH_SOUTH_WEST,	// 0110
	HAT_SWITCH_WEST,		// 0111

	HAT_SWITCH_EAST,		// 1000
	HAT_SWITCH_NORTH_EAST,	// 1001
	HAT_SWITCH_SOUTH_EAST,	// 1010
	HAT_SWITCH_EAST,		// 1011

	HAT_SWITCH_NULL,		// 1100
	HAT_SWITCH_NORTH,		// 1101
	HAT_SWITCH_SOUTH,		// 1110
	HAT_SWITCH_NULL,		// 1111
};

#ifdef EMULATE_XY
/// XY方向テーブル
static uint8_t xyTable[] = {
	0x80,	// N
	0xFF,	// NE
	0xFF,	// E
	0xFF,	// SE
	0x80,	// S
	0x00,	// SW
	0x00,	// W  N
	0x00,	// NW NE
	0x80,	// X  E
	0xFF,	//    SE
	0xFF,	//    S
	0xFF,	//    SW
	0x80,	//    W
	0x00,	//    NW
	0x80	//    X
};
#endif	// EMULATE_XY

/*********************************************************************
* Function: void APP_DeviceJoystickInitialize(void);
*
* Overview: Initializes the demo code
*
* PreCondition: None
*
* Input: None
*
* Output: None
*
********************************************************************/
void APP_DeviceJoystickInitialize(void)
{
	// initialize the variable holding the handle for the last
	// transmission
	lastTransmission = 0;

	// enable the HID endpoint
	USBEnableEndpoint(JOYSTICK_EP, USB_IN_ENABLED | USB_HANDSHAKE_ENABLED | USB_DISALLOW_SETUP);

	// lastData
	lastData = 0;

#ifdef EMULATE_XY
	// START+SELECT同時押しで電源投入した場合に動作を変更する
	modeSwitch = (PORTC & 15) == 0;
#endif	// EMULATE_XY

	// Initialize
#if RPT_TYPE == 1
	joystickInput.val[0] = HAT_SWITCH_NULL;
	joystickInput.val[1] = 0;
#elif RPT_TYPE == 2
	joystickInput.val[0] = 0;
	joystickInput.val[1] = 0;
	joystickInput.val[2] = HAT_SWITCH_NULL;
	joystickInput.val[3] = 0x80;
	joystickInput.val[4] = 0x80;
	joystickInput.val[5] = 0x80;
	joystickInput.val[6] = 0x80;
#else
	joystickInput.val[0] = 0x80;
	joystickInput.val[1] = 0x80;
	joystickInput.val[2] = 0x80;
	joystickInput.val[3] = 0x80;
	joystickInput.val[4] = HAT_SWITCH_NULL;
	joystickInput.val[5] = 0;
	joystickInput.val[6] = 0;
#endif
}

/*********************************************************************
* Function: void APP_DeviceJoystickTasks(void);
*
* Overview: Keeps the demo running.
*
* PreCondition: The demo should have been initialized and started via
*   the APP_DeviceJoystickInitialize() and APP_DeviceJoystickStart() demos
*   respectively.
*
* Input: None
*
* Output: None
*
********************************************************************/
void APP_DeviceJoystickTasks(void)
{
	// If the USB device isn't configured yet, we can't really do anything
	// else since we don't have a host to talk to.  So jump back to the
	// top of the while loop.
	if (USBGetDeviceState() < CONFIGURED_STATE) {
		return;
	}

	// If we are currently suspended, then we need to see if we need to
	// issue a remote wakeup.  In either case, we shouldn't process any
	// keyboard commands since we aren't currently communicating to the host
	// thus just continue back to the start of the while loop.
	if (USBIsDeviceSuspended()) {
		return;
	}

	// If the last transmission is not complete
	if (HIDTxHandleBusy(lastTransmission)) {
		return;
	}

	// データ入力
	uint8_t data = ~PORTC & 0x7F;

	// データが同一なら何もしない
	if (data == lastData) {
		return;
	}
	lastData = data;

	// 方向入力
	uint8_t dir = hatTable[data & 15];
	joystickInput.m.dpad = dir;

#ifdef EMULATE_XY
	if (modeSwitch == 0) {
		// アナログ方向入力のエミュレーション
		joystickInput.m.X = xyTable[dir];
		joystickInput.m.Y = xyTable[dir + 6];
	}
#endif // EMULATE_XY

	// ボタン入力
	joystickInput.m.x = data >> 4;
	joystickInput.m.o = data >> 5;
	joystickInput.m.square = data >> 6;

	// 方向入力(特殊)
	data &= data >> 1;
	joystickInput.m.select = data;
	joystickInput.m.start = data >> 2;

#if 0
	uint8_t t = dir;

	// ボタン入力
	t |= (data << 1) & (1 << 5);	// bit 5: x
	t |= (data << 1) & (1 << 6);	// bit 6: o
	t |= (data >> 2) & (1 << 4);	// bit 4: square
	joystickInput.val[0] = t;

	// 方向入力(特殊)
	data &= data >> 1;
	t = 0;
	t |= (data & 1) << 4;			// bit 4: select (SHARE)
	t |= (data & 4) << 3;			// bit 5: start (OPTION)
	joystickInput.val[1] = t;
#endif

	// Send the 8 byte packet over USB to the host.
	/// @note データは7バイトなのに8バイトと書かれているのは何故？
	lastTransmission = HIDTxPacket(JOYSTICK_EP, (uint8_t*)&joystickInput, sizeof(joystickInput));
}

#endif	// USBJOYSTICK_C
