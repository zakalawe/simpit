
#include "LEDDriver.h"

#include <linux/i2c-dev.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h> // for usleep
#include <sys/ioctl.h>

#include <iostream>

static const char *fileName = "/dev/i2c-1"; // change to /dev/i2c-0 if you are using a revision 0002 or 0003 model B


#define PCA9622_SUBADR1 0x18
#define PCA9622_SUBADR2 0x19
#define PCA9622_SUBADR3 0x1A
#define PCA9622_SUBADR3 0x1A
#define PCA9622_ALLCALL 0x1B

#define PCA9622_MODE1 0x0
#define PCA9622_MODE2 0x1

#define PCA9622_PWM0 0x02

#define PCA9622_LEDOUT0 0x14
#define PCA9622_LEDOUT1 0x15
#define PCA9622_LEDOUT2 0x16
#define PCA9622_LEDOUT3 0x17

#define LED0_ON_L 0x6
#define LED0_ON_H 0x7
#define LED0_OFF_L 0x8
#define LED0_OFF_H 0x9

#define ALLLED_ON_L 0xFA
#define ALLLED_ON_H 0xFB
#define ALLLED_OFF_L 0xFC
#define ALLLED_OFF_H 0xFD

// Set to true to print some debug messages, or false to disable them.
//#define ENABLE_DEBUG_OUTPUT

LEDDriver::LEDDriver(uint8_t addr)
{
  _i2caddr = addr;

  if ((_i2cHandle = open(fileName, O_RDWR)) < 0) {
		printf("Failed to open i2c port for read %s \n", strerror(errno));
		exit(1);
	}

	if (ioctl(_i2cHandle, I2C_SLAVE, _i2caddr) < 0) {
		printf("Failed to write to i2c port for read\n");
		exit(1);
	}
}


/**************************************************************************/
/*!
    @brief  Setups the I2C interface and hardware
*/
/**************************************************************************/
void LEDDriver::begin(void)
{
  reset();

  const uint8_t ALLCALL = 0x00; // all-call and subaddr disabled
  const uint8_t AUTO_INCREMENT = 0x00; // disable auto-increment
  const uint8_t SLEEP = 0x00; // disable sleep

  write8(PCA9622_MODE1, ALLCALL | AUTO_INCREMENT | SLEEP);

  const uint8_t DMBLNK = 0x00; // group-control = dimming
  const uint8_t INVRT = 0x00; // reserved
  const uint8_t MODE2_RESERVED = 0x05; // reserved, must be set

  write8(PCA9622_MODE2, DMBLNK | INVRT | MODE2_RESERVED);
}

/**************************************************************************/
/*!
    @brief  Sends a reset command to the PCA9622 chip over I2C
*/
/**************************************************************************/
void LEDDriver::reset(void)
{
  // not implemented right now
}

/**************************************************************************/
/*!
    @brief  Sets the PWM output of one of the PCA9685 pins
    @param  num One of the PWM output pins, from 0 to 15
    @param  on At what point in the 4096-part cycle to turn the PWM output ON
    @param  off At what point in the 4096-part cycle to turn the PWM output OFF
*/
/**************************************************************************/
void LEDDriver::setPWM(uint8_t num, uint8_t brightness)
{
#ifdef ENABLE_DEBUG_OUTPUT
  Serial.print("Setting PWM "); Serial.print(num); Serial.print(": "); Serial.print(brightness);
#endif
  write8(PCA9622_PWM0 + num, brightness);
}

void LEDDriver::setState(uint8_t num, uint8_t state)
{
#ifdef ENABLE_DEBUG_OUTPUT
  Serial.print("Setting state "); Serial.print(num); Serial.print(": "); Serial.print(state);
#endif
    uint8_t controlRegister = PCA9622_LEDOUT0 + (num >> 2);
    uint8_t controlOffset = (num & 0x3) * 2;
    uint8_t controlMask = ~(0x3 << controlOffset);

    uint8_t value = read8(controlRegister) & controlMask;
    write8(controlRegister, value | (state << controlOffset));
}

/*******************************************************************************************/

uint8_t LEDDriver::read8(uint8_t addr)
{
  uint8_t buf[1] = { addr };
  if ((write(_i2cHandle, buf, 1)) != 1) {
		printf("Failed to write to i2c device for read\n");
		exit(1);
	}

  if (read(_i2cHandle, buf, 1) != 1) { // Read back data into buf[]
		printf("Failed to read from slave\n");
		exit(1);
	}

  return buf[0];
}

void LEDDriver::write8(uint8_t addr, uint8_t d) {
  uint8_t buf[2] = { addr, d };
  if ((write(_i2cHandle, buf, 2)) != 2) {
		printf("Failed to write to i2c device for write\n");
		exit(1);
	}
}

LEDOutput::LEDOutput(LEDDriver* driver, uint8_t num) :
  _index(num),
  _driver(driver)
{
  setState(false); // sync initial state
}

void LEDOutput::setState(bool b)
{
  if (b == _state) {
    return;
  }

  _state = b;
  _driver->setState(_index, b ? PCA9622_STATE_ON : PCA9622_STATE_OFF);
}