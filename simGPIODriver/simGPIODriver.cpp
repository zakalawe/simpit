
#include <string>
#include <iostream>
#include <cstdlib>
#include <unistd.h>

#include "FGFSTelnetSocket.h"

using namespace std;

const uint8_t Gear_I2C_Address = 0x20;
const uint8_t Gear_Port = 0;
const uint8_t Gear_Unused_Port = 1;

const uint8_t Sixpack_I2C_Address = 0x21;
const uint8_t Sixpack_Lamp_Port = 0;
const uint8_t Sixpack_Button_Port = 1;

#if defined(LINUX_BUILD)

extern "C" {
#include "ABE_IoPi.h"
}

void initGPIO()
{
    IOPi_init(Gear_I2C_Address); // initialise one of the io pi buses on i2c address 0x20
    set_port_direction(Gear_I2C_Address, Gear_Port, 0x03); // set the direction for bank 0 on address 0x20 to be outputs
    set_port_direction(Gear_I2C_Address, Gear_Unused_Port, 0x00); // set the direction for bank 1 on address 0x20 to be outputs


    set_port_pullups(Gear_I2C_Address, Gear_Port, 0x03); // enable internal pullups for bank 0
    invert_port(Gear_I2C_Address, Gear_Port, 0x03); // invert output so bank will read as 0

    IOPi_init(Sixpack_I2C_Address);
    set_port_direction(Sixpack_I2C_Address, Sixpack_Lamp_Port, 0x0);
    set_port_direction(Sixpack_I2C_Address, Sixpack_Button_Port, 0xff);

    write_port(Gear_I2C_Address, Gear_Port, 0);
}

#else
void write_port(char address, char port, char value)
{
}

char read_port(char address, char port)
{
    return 0;
}

void initGPIO()
{
    std::cout << "dummy GPIO init"<< std::endl;
}
#endif

const std::string fgfsHost = "simpc.local";
const int fgfsPort = 5501;

const int defaultReconnectBackoff = 1000;

double gearPositionNorm[3] = {0.0, 0.0, 0.0};

const double gearDownAndLockedThreshold = 0.98;
const double gearUpAndLockedThreshold = 0.02;

void setupSubscriptions(FGFSTelnetSocket& socket)
{
    socket.subscribe("/gear/gear[0]/position-norm");
    socket.subscribe("/gear/gear[1]/position-norm");
    socket.subscribe("/gear/gear[2]/position-norm");
}

void getInitialState(FGFSTelnetSocket& socket)
{
    for (int i=0; i<3; ++i) {
        gearPositionNorm[i] = socket.syncGetDouble("/gear/gear[" + to_string(i) + "]/position-norm");
    }
}

uint8_t lastLEDState = 0;
bool LEDUpdateRequired = true;

void updateGearLEDState()
{
    uint8_t ledByte = 0;
    size_t offset = 2; // skip IOs 0 and 1 which are the switches

    for (int i=0; i<3; ++i) {
        const double p = gearPositionNorm[i];
        bool isDownAndLocked = (p >= gearDownAndLockedThreshold);
        bool gearUnsafe = (p > gearUpAndLockedThreshold) &&
                (p <= gearDownAndLockedThreshold);

        if (gearUnsafe)
            ledByte |= 1 << (offset);
        if (isDownAndLocked)
            ledByte |= 1 << (offset + 1);
        offset += 2;
    }

    if (lastLEDState != ledByte) {
        // output
        std::cout << "LED byte is now " << std::oct << (int) ledByte << std::endl;
        lastLEDState = ledByte;
        write_port(Gear_I2C_Address, Gear_Port, ledByte);
    }
}

bool lastGearUpState = false;
bool lastGearDownState = false;

void pollGearLeverState(FGFSTelnetSocket& socket)
{
    const uint8_t inPins = read_port(Gear_I2C_Address, Gear_Port);
    const bool isUpSwitch = inPins & 0x1;
    const bool isDownSwitch = inPins & 0x2;

    if (lastGearDownState != isDownSwitch) {
        if (isDownSwitch) {
            socket.set("/controls/gear/gear-down", "1");
        }
        lastGearDownState = isDownSwitch;
    } else if (lastGearUpState != isUpSwitch) {
        if (isUpSwitch) {
            socket.set("/controls/gear/gear-down", "0");
        }
        lastGearUpState = isUpSwitch;
    }
}

void pollHandler(const std::string& message)
{
    // Telnet code doesn't send index for [0]
    if (message.find("/gear/gear/position-norm=") == 0) {
        gearPositionNorm[0] = std::stod(message.substr(25));
        LEDUpdateRequired = true;
    } else if (message.find("/gear/gear[0]/position-norm=") == 0) {
        gearPositionNorm[0] = std::stod(message.substr(28));
        LEDUpdateRequired = true;
    } else if (message.find("/gear/gear[1]/position-norm=") == 0) {
        gearPositionNorm[1] = std::stod(message.substr(28));
        LEDUpdateRequired = true;
    } else if (message.find("/gear/gear[2]/position-norm=") == 0) {
        gearPositionNorm[2] = std::stod(message.substr(28));
        LEDUpdateRequired = true;
    } else {
        std::cerr << "unhandled message:" << message << std::endl;
    }
}

int main(int argc, char* argv[])
{
    // arg override if needed
    std::string host = fgfsHost;
    if (argc > 1) {
        host = argv[1];
    }

    int port = fgfsPort;
    if (argc > 2) {
        port = std::stoi(std::string(argv[2]));
    }

    // install signal handlers?

    FGFSTelnetSocket socket;

    initGPIO();

    int reconnectBackoff = defaultReconnectBackoff;

    while (true) {
        if (!socket.isConnected()) {
            if (!socket.connect(host, port)) {
                ::usleep(reconnectBackoff * 1000);
                reconnectBackoff = std::min(reconnectBackoff * 2, 1000 * 30);
                continue;
            }

            // reset back-off after succesful connect
            reconnectBackoff = defaultReconnectBackoff;

            getInitialState(socket);
            setupSubscriptions(socket);
        }

        socket.poll(pollHandler, 50 /* msec, so 20hz */);
        pollGearLeverState(socket);

        if (LEDUpdateRequired) {
            LEDUpdateRequired = false;
            updateGearLEDState();
        }
        // check for kill signal
    }

    return EXIT_SUCCESS;
}
