
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

#include "LEDDriver.h"

using namespace std;


LEDDriver* ledDriver = nullptr;


int main(int argc, char* argv[])
{
    ledDriver = new LEDDriver(0x31); // I2C address
    ledDriver->begin();

    while (true) {
        for (int i=0; i<16; ++i) {
            ledDriver->setState(i, PCA9622_STATE_ON);
            ::sleep(1);
        }

         for (int i=0; i<16; ++i) {
            ledDriver->setState(i, PCA9622_STATE_OFF);
            ::sleep(1);
        }
    }

    return EXIT_SUCCESS;
}
