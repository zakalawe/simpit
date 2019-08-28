
#include "LEDDriver.h"


LEDDriver::LEDDriver(uint8_t addr)
{

}

void LEDDriver::begin(void)
{
}


void LEDDriver::reset(void)
{
  // not implemented right now
}

void LEDDriver::setPWM(uint8_t num, uint8_t brightness)
{
}

void LEDDriver::setState(uint8_t num, uint8_t state)
{
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
}