
#include <string>
#include <iostream>
#include <cstdlib>
#include <vector>
#include <cassert>
#include <algorithm>
#include <exception>

#include <unistd.h>
#include <ctime>
#include <signal.h>

#include "Adafruit_PWMServoDriver.h"

using namespace std;



// Depending on your servo make, the pulse width min and max may vary, you 
// want these to be as small/large as possible without hitting the hard stop
// for max range. You'll have to tweak them as necessary to match the servos you
// have!
#define SERVOMIN  150 // this is the 'minimum' pulse length count (out of 4096)
#define SERVOMAX  600 // this is the 'maximum' pulse length count (out of 4096)

Adafruit_PWMServoDriver* servoDriver = nullptr;


int main(int argc, char* argv[])
{
    servoDriver = new Adafruit_PWMServoDriver(0x40, 60.0); // I2C address
    servoDriver->begin();

  
    while (true) {
        for (int i=0; i<10; ++i) {
            servoDriver->setPWM(0, 0, 150 + (45 * i));
            ::sleep(1);
        }
    }

    return EXIT_SUCCESS;
}
