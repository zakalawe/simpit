#ifndef LED_DRIVER_H
#define LED_DRIVER_H

#include <cstdint>

#define PCA9622_STATE_OFF 0x0
#define PCA9622_STATE_ON 0x1
#define PCA9622_STATE_PWM 0x2
#define PCA9622_STATE_PWM_GROUP 0x3

/**************************************************************************/
/*!
    @brief  Class that stores state and functions for interacting with PCA922 LED PWM chip
*/
/**************************************************************************/

class LEDDriver {
 public:
  LEDDriver(uint8_t addr = 0x31);

  void begin(void);
  void reset(void);

  void setState(uint8_t num, uint8_t state);
  void setPWM(uint8_t num, uint8_t brightness);

 private:
  uint8_t _i2caddr;
  int _i2cHandle = 0;

  uint8_t read8(uint8_t addr);
  void write8(uint8_t addr, uint8_t d);
};

#endif
