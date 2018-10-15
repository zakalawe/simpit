/*************************************************** 
  This is a library for our Adafruit 16-channel PWM & Servo driver

  Pick one up today in the adafruit shop!
  ------> http://www.adafruit.com/products/815

  These displays use I2C to communicate, 2 pins are required to  
  interface.

  Adafruit invests time and resources providing this open source code, 
  please support Adafruit and open-source hardware by purchasing 
  products from Adafruit!

  Written by Limor Fried/Ladyada for Adafruit Industries.  
  BSD license, all text above must be included in any redistribution
 ****************************************************/

#include "Adafruit_PWMServoDriver.h"

#include <linux/i2c-dev.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h> // for usleep

#include <iostream>

static const char *fileName = "/dev/i2c-1"; // change to /dev/i2c-0 if you are using a revision 0002 or 0003 model B

// Set to true to print some debug messages, or false to disable them.
//#define ENABLE_DEBUG_OUTPUT


/**************************************************************************/
/*! 
    @brief  Instantiates a new PCA9685 PWM driver chip with the I2C address on the Wire interface. On Due we use Wire1 since its the interface on the 'default' I2C pins.
    @param  addr The 7-bit I2C address to locate this chip, default is 0x40
*/
/**************************************************************************/
Adafruit_PWMServoDriver::Adafruit_PWMServoDriver(uint8_t addr, float freq) 
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

// prescale compuation
  freq *= 0.9;  // Correct for overshoot in the frequency setting (see issue #11).
  float prescaleval = 25000000;
  prescaleval /= 4096;
  prescaleval /= freq;
  prescaleval -= 1;

  _prescale = floor(prescaleval + 0.5);
  std::cerr << "Final prescale:" << std::hex << (int) _prescale << std::endl;
}


/**************************************************************************/
/*! 
    @brief  Setups the I2C interface and hardware
*/
/**************************************************************************/
void Adafruit_PWMServoDriver::begin(void) 
{
  reset();

  const uint8_t ALLCALL = 0x01; 
  const uint8_t AUTO_INCREMENT = 0x20; 
  const uint8_t SLEEP = 0x10; 

  write8(PCA9685_MODE2, 0x04); // OUTDRV
  write8(PCA9685_MODE1, ALLCALL); 

  write8(PCA9685_MODE1, SLEEP); // go to sleep
  usleep(1000); // wait for oscillator to shut down
  write8(PCA9685_PRESCALE, _prescale); // set the prescaler
  write8(PCA9685_MODE1, ALLCALL | AUTO_INCREMENT | 0x80); // wake up
  usleep(1000); // wait for oscillator to come back up
}

/**************************************************************************/
/*! 
    @brief  Sends a reset command to the PCA9685 chip over I2C
*/
/**************************************************************************/
void Adafruit_PWMServoDriver::reset(void) 
{    
  write8(PCA9685_MODE1, 0x80);
  usleep(1000);
}

/**************************************************************************/
/*! 
    @brief  Sets the PWM output of one of the PCA9685 pins
    @param  num One of the PWM output pins, from 0 to 15
    @param  on At what point in the 4096-part cycle to turn the PWM output ON
    @param  off At what point in the 4096-part cycle to turn the PWM output OFF
*/
/**************************************************************************/
void Adafruit_PWMServoDriver::setPWM(uint8_t num, uint16_t on, uint16_t off) {
#ifdef ENABLE_DEBUG_OUTPUT
  Serial.print("Setting PWM "); Serial.print(num); Serial.print(": "); Serial.print(on); Serial.print("->"); Serial.println(off);
#endif

// note this requires auto-increment to be active
  uint8_t buf[5] = {LED0_ON_L+4*num,
    on & 0xff, 
    on >> 8, 
    off & 0xff, 
    off >> 8 };

  if ((write(_i2cHandle, buf, 5)) != 5) {
		printf("Failed to write to i2c device for write\n");
		exit(1);
	}
}

/**************************************************************************/
/*! 
    @brief  Helper to set pin PWM output. Sets pin without having to deal with on/off tick placement and properly handles a zero value as completely off and 4095 as completely on.  Optional invert parameter supports inverting the pulse for sinking to ground.
    @param  num One of the PWM output pins, from 0 to 15
    @param  val The number of ticks out of 4096 to be active, should be a value from 0 to 4095 inclusive.
    @param  invert If true, inverts the output, defaults to 'false'
*/
/**************************************************************************/
void Adafruit_PWMServoDriver::setPin(uint8_t num, uint16_t val, bool invert)
{
  // Clamp value between 0 and 4095 inclusive.
  val = std::min(val, (uint16_t)4095);
  if (invert) {
    if (val == 0) {
      // Special value for signal fully on.
      setPWM(num, 4096, 0);
    }
    else if (val == 4095) {
      // Special value for signal fully off.
      setPWM(num, 0, 4096);
    }
    else {
      setPWM(num, 0, 4095-val);
    }
  }
  else {
    if (val == 4095) {
      // Special value for signal fully on.
      setPWM(num, 4096, 0);
    }
    else if (val == 0) {
      // Special value for signal fully off.
      setPWM(num, 0, 4096);
    }
    else {
      setPWM(num, 0, val);
    }
  }
}

/*******************************************************************************************/

uint8_t Adafruit_PWMServoDriver::read8(uint8_t addr) 
{
  uint8_t buf[1] = { addr };
  if ((write(_i2cHandle, buf, 1)) != 1) {
		printf("Failed to write to i2c device for write\n");
		exit(1);
	}

  if (read(_i2cHandle, buf, 1) != 1) { // Read back data into buf[]
		printf("Failed to read from slave\n");
		exit(1);
	}

  return buf[0];
}

void Adafruit_PWMServoDriver::write8(uint8_t addr, uint8_t d) {
  uint8_t buf[2] = { addr, d };
  if ((write(_i2cHandle, buf, 2)) != 2) {
		printf("Failed to write to i2c device for write\n");
		exit(1);
	}
}
