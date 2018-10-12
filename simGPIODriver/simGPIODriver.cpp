
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

#include "FGFSTelnetSocket.h"

using namespace std;

const uint8_t Gear_I2C_Address = 0x20;
const uint8_t Gear_Port = 0;
const uint8_t Gear_Unused_Port = 1;

const uint8_t Sixpack_I2C_Address = 0x21;
const uint8_t Sixpack_Lamp_Port = 0;
const uint8_t Sixpack_Button_Port = 1;

// Depending on your servo make, the pulse width min and max may vary, you 
// want these to be as small/large as possible without hitting the hard stop
// for max range. You'll have to tweak them as necessary to match the servos you
// have!
#define SERVOMIN  150 // this is the 'minimum' pulse length count (out of 4096)
#define SERVOMAX  600 // this is the 'maximum' pulse length count (out of 4096)

#if defined(LINUX_BUILD)

extern "C" {
#include "ABE_IoPi.h"
}

#include "Adafruit_PWMServoDriver.h"

Adafruit_PWMServoDriver* servoDriver = nullptr;

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

    set_port_pullups(Sixpack_I2C_Address, Sixpack_Button_Port, 0xff); // enable internal pullups 
    invert_port(Sixpack_I2C_Address, Sixpack_Button_Port, 0x0f); // invert output so bank will read as 0

// clear everything
    write_port(Sixpack_I2C_Address, Sixpack_Lamp_Port, 0);
    write_port(Gear_I2C_Address, Gear_Port, 0);


    servoDriver = new Adafruit_PWMServoDriver(0x40); // I2C address
    servoDriver->setPWMFreq(60); // 60Hz frequency for analogue servos
    servoDriver->begin();

}

#else
void write_port(char address, char port, char value)
{
}

void write_pin(char address, char port, char value)
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

const int defaultReconnectBackoff = 4;
const int keepAliveInterval = 10;

double gearPositionNorm[3] = {0.0, 0.0, 0.0};

const double gearDownAndLockedThreshold = 0.98;
const double gearUpAndLockedThreshold = 0.02;

std::vector<std::string> lampNames = {
    "master-caution", "fire-warn", "fuel", "ovht",
    "irs", "apu", "flt-cont", "elec"};

uint8_t lampBits = 0;

void setupSubscriptions(FGFSTelnetSocket& socket)
{
    socket.subscribe("/gear/gear[0]/position-norm");
    socket.subscribe("/gear/gear[1]/position-norm");
    socket.subscribe("/gear/gear[2]/position-norm");

    for (auto s : lampNames) {
        socket.subscribe("/instrumentation/weu/outputs/" + s + "-lamp");
    }
   
}

bool getInitialState(FGFSTelnetSocket& socket)
{
    int attempts = 5;
    bool ok;
    lampBits = 0;

    assert(lampNames.size() == 8);

    for (int attempt=0; attempt < attempts; ++attempt) {
        bool attemptOk = true;
        for (int i=0; i<3; ++i) {
            ok = socket.syncGetDouble("/gear/gear[" + to_string(i) + "]/position-norm",
                                    gearPositionNorm[i]);

            if (!ok) 
                attemptOk = false;
        }

        for (int i=0; i<8; ++i) {
            bool b;
            ok = socket.syncGetBool("/instrumentation/weu/outputs/" + lampNames.at(i) + "-lamp", b);
            if (!ok) {
                attemptOk = false;
                std::cerr << "failed to get initial state for:" << "/instrumentation/weu/outputs/" + lampNames.at(i) + "-lamp" << std::endl;
            }
            if (b) {
                lampBits |= (1 << i); // set the lamp bit
            }
        }

        if (attemptOk) { // all values in this attempt worked, we are done
            return true;
        }
    } // of attempts
    
    return false;
}

uint8_t lastLEDState = 0, lastLampLEDState = 0;
bool LEDUpdateRequired = true;
bool SixpackLEDUpdateRequired = true;

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

void updateLampLEDState()
{
    if (lastLampLEDState != lampBits) {
        lastLampLEDState = lampBits;
        std::cout << "lamp LED byte is now " << std::hex << (int) lampBits << std::endl;
        write_port(Sixpack_I2C_Address, Sixpack_Lamp_Port, (char) lampBits);

        write_pin(Sixpack_I2C_Address, 8, 1);

    }
}

enum class SpecialLEDState
{
    Connecting = 0,
    ConnectBackoff,
    ConnectBackoff2,
    DidConnect
};

void setSpecialLEDState(SpecialLEDState state)
{
    uint8_t ledByte = 0;
    switch (state) {
    case SpecialLEDState::Connecting:
        ledByte = 0x08;
        break;
    case SpecialLEDState::ConnectBackoff:
        ledByte = 0x40;
        break;
    case SpecialLEDState::ConnectBackoff2:
        ledByte = 0x10;
        break;
    case SpecialLEDState::DidConnect:
        ledByte = 0xf6;
        break;
    default:
        break;
    }
    write_port(Gear_I2C_Address, Gear_Port, ledByte);

    static uint8_t lampByte = 0;
    if (lampByte == 0)
        lampByte = 1;
    write_port(Sixpack_I2C_Address, Sixpack_Lamp_Port, lampByte);
    lampByte <<= 1;
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


bool weuButtons[3] = {false, false, false };

void pollWEUButtons(FGFSTelnetSocket& socket)
{
    const uint8_t inPins = read_port(Sixpack_I2C_Address, Sixpack_Button_Port);
    for (int b=0; b< 3; ++b) {
        int pinIndex = b + 1;
        bool state = (inPins & (1 << pinIndex));
        if (weuButtons[b] != state) {
            if (state && (b == 0)) {
                std::cerr << "Master caution push" << std::endl;
                socket.write("run weu-caution-button");
            } else if (state && (b == 2)) {
                std::cerr << "Fire warn push" << std::endl;
                socket.write("run weu-fire-button");
            } else if (b == 1) {
                if (state) {
                    std::cerr << "Recall push" << std::endl;
                    socket.write("run weu-recall-button");
                } else {
                    std::cerr << "Recall release" << std::endl;
                    socket.write("run weu-recall-button-off");
                }
            }

            weuButtons[b] = state;
        }
    }
}

const char* WEU_OUTPUT_PREFIX = "/instrumentation/weu/outputs/";
const int WEU_OUTPUT_PREFIX_LEN = 29;
const char* WEU_LAMP_SUFFIX = "-lamp=";
const int WEU_LAMP_SUFFIX_LEN = 6;

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
    } else if (message.find("/instrumentation/weu/outputs/") == 0) {
        const auto lampSuffix = message.find(WEU_LAMP_SUFFIX);
        const std::string lampName = message.substr(WEU_OUTPUT_PREFIX_LEN,
            lampSuffix - WEU_OUTPUT_PREFIX_LEN);

        auto it = std::find(lampNames.begin(), lampNames.end(), lampName);
        if (it == lampNames.end()) {
            std::cerr << "weird lamp name:" << lampName << std::endl;
            std::cerr << "full message was:" << message << std::endl;
        } else {
            try {
                int lampIndex = std::distance(lampNames.begin(), it);
                const auto v = message.substr(lampSuffix + WEU_LAMP_SUFFIX_LEN);
                if (v == "true") {
                    lampBits |= 1 << lampIndex;
                } else {
                    lampBits &= ~(1 << lampIndex); // clear the bit
                }
            } catch (std::exception& e) {
                std::cerr << "exception processing value data:" << message.substr(lampSuffix + WEU_LAMP_SUFFIX_LEN) << std::endl;
                std::cerr << "full message was:" << message << std::endl;
            }
        }

        SixpackLEDUpdateRequired = true;
    } else {
        if (message.find("subscribe") == 0) {
            // subscription confirmation, fine
        } else if (message == "/") {
            // this is the response to the 'pwd' query we use to keep
            // the socket alive.
        } else {
            std::cerr << "unhandled message:" << message << std::endl;
        }
    }
}

int pulselen = SERVOMIN;

void idleForTime(int timeSec)
{
    time_t endTime = time(nullptr) + timeSec;
    setSpecialLEDState(SpecialLEDState::ConnectBackoff);
    while (time(nullptr) < endTime) {
    #if defined(LINUX_BUILD)
        servoDriver->setPWM(0, 0, pulselen);
        pulselen++;
        if (pulselen >= SERVOMAX) {
            pulselen = SERVOMIN;
        }
#endif
        ::usleep(500 * 1000);
        setSpecialLEDState(SpecialLEDState::ConnectBackoff2);
        ::usleep(500 * 1000);
        setSpecialLEDState(SpecialLEDState::ConnectBackoff);
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
    signal(SIGPIPE, SIG_IGN);

    FGFSTelnetSocket socket;

    initGPIO();

    int reconnectBackoff = defaultReconnectBackoff;
    time_t lastReadTime = time(nullptr);

    while (true) {

#if defined(LINUX_BUILD)
        servoDriver->setPWM(0, 0, pulselen);
        pulselen++;
        if (pulselen >= SERVOMAX) {
            pulselen = SERVOMIN;
        }
#endif

        if (!socket.isConnected()) {
        //    std::cerr << "starting connection" << std::endl;
            setSpecialLEDState(SpecialLEDState::Connecting);
            if (!socket.connect(host, port)) {
                idleForTime(reconnectBackoff);
                reconnectBackoff = std::min(reconnectBackoff * 2, 30);
                continue;
            }

            // reset back-off after succesful connect
            reconnectBackoff = defaultReconnectBackoff;
            setSpecialLEDState(SpecialLEDState::DidConnect);

            if (!getInitialState(socket)) {
                std::cerr << "failed to get initial state, will re-try" << std::endl;
                socket.close();
                continue;
            }

            setupSubscriptions(socket);
            setSpecialLEDState(SpecialLEDState::DidConnect);
        }

        time_t nowSeconds = time(nullptr);
        if ((nowSeconds - lastReadTime) > keepAliveInterval) {
            // force a write to check for dead socket
            lastReadTime = nowSeconds;
            socket.write("pwd");
        }

        bool ok = socket.poll(pollHandler, 500 /* msec, so 20hz */);
        if (!ok) {
            // poll failed, close out
            socket.close();
            continue;
        }

        pollGearLeverState(socket);
        pollWEUButtons(socket);

        if (LEDUpdateRequired) {
            LEDUpdateRequired = false;
            updateGearLEDState();
        }

        if (SixpackLEDUpdateRequired) {
            SixpackLEDUpdateRequired = false;
            updateLampLEDState();
        }
        // check for kill signal
    }

    return EXIT_SUCCESS;
}
