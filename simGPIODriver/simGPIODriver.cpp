
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

const uint8_t GPIO_I2C_Address = 0x20;
const uint8_t Glare_Switch_Port = 0;
const uint8_t Gear_Switch_Port = 1;

const uint8_t MIP1_I2C_Address = 0x21;
const uint8_t MIP1_N1_Port = 0;
const uint8_t MIP1_Speeds_Port = 1;

const uint8_t MIP2_I2C_Address = 0x22;
const uint8_t MIP2_Lamp_Port = 0;
const uint8_t MIP2_Unused_Port = 1;

#if defined(LINUX_BUILD)

extern "C" {
#include "ABE_IoPi.h"
}

#include "Adafruit_PWMServoDriver.h"
#include "LEDDriver.h"

Adafruit_PWMServoDriver* servoDriver = nullptr;
LEDDriver* ledDriver = nullptr;

void initGPIO()
{

//    servoDriver = new Adafruit_PWMServoDriver(0x40, 60); // I2C address
//    servoDriver->begin();
}

#else
void write_port(char address, char port, char value)
{
}

void write_pin(char address, char port, char value)
{
}


uint8_t read_port(uint8_t address, uint8_t port)
{
    return 0;
}

void IOPi_init(uint8_t address)
{

}

void set_port_direction(uint8_t address, uint8_t port, uint8_t val)
{

}

void set_port_pullups(uint8_t address, uint8_t port, uint8_t val)
{

}

void initGPIO()
{
    std::cout << "dummy GPIO init"<< std::endl;
}
#endif

using Callback = std::function<void(bool)>;

enum class Trigger
{
    AnyEdge,
    High,
    Low,
};

class InputBinding
{
public:
    InputBinding(uint8_t addr, uint8_t port, uint8_t bit, Callback cb, Trigger t = Trigger::AnyEdge) :
        _address(addr),
        _port(port),
        _bit(bit),
        _trigger(t),
        _callback(cb)
    {

    }

    void update(uint8_t val)
    {
        const uint8_t mask = (1 << _bit);
        const bool s = (val & mask);
        if (s != _lastState) {
            if ((_trigger == Trigger::AnyEdge)
                || (_trigger == Trigger::High && s)
                || (_trigger == Trigger::Low && !s))
            {
                _callback(s);
            }
            _lastState = s;
        }
    }

    uint8_t address() const
    {
        return _address;
    }

    uint8_t port() const
    {
        return _port;
    }

    uint8_t bit() const
    {
        return _bit;
    }
private:
    const uint8_t _address;
    const uint8_t _port;
    const uint8_t _bit;

    Trigger _trigger = Trigger::AnyEdge;
    bool _lastState = false;

    Callback _callback;
};

using InputBindingVec = std::vector<InputBinding>;

class GPIOPoller
{
public:
    GPIOPoller(uint8_t addr) :
        _address(addr)
    {

    }

    void addBinding(const InputBinding& b)
    {
        assert(b.address() == _address);
        _bindings.push_back(b);
        _portInputMask[b.port()] |= 1 << b.bit();
    }

    void open()
    {
        IOPi_init(_address); // initialise one of the io pi buses on i2c address 0x20
        set_port_direction(_address, 0, _portInputMask[0]);
        set_port_direction(_address, 1, _portInputMask[1]);
        set_port_pullups(_address, 0, _portInputMask[0]);
        set_port_pullups(_address, 1, _portInputMask[1]);
    }

    void update()
    {
        const uint8_t inPins0 = read_port(_address, 0);
        const uint8_t inPins1 = read_port(_address, 1);

        if (inPins0 != _portStates[0]) {
            _portStates[0] = inPins0;

            for (auto& b : _bindings) {
                if (b.port() == 0) {
                    b.update(inPins0);
                }
            }
        }

        if (inPins1 != _portStates[1]) {
            _portStates[1] = inPins1;

            for (auto& b : _bindings) {
                if (b.port() == 1) {
                    b.update(inPins1);
                }
            }
        }
    }
private:
    const uint8_t _address;
    uint8_t _portStates[2] = {0,0};
    uint8_t _portInputMask[2] = {0,0};
    InputBindingVec _bindings;
};


const std::string fgfsHost = "simpc.local";
const int fgfsPort = 5501;

const int defaultReconnectBackoff = 4;
const int keepAliveInterval = 10;

double gearPositionNorm[3] = {0.0, 0.0, 0.0};
double flapPositionNorm = 0.0;

const double gearDownAndLockedThreshold = 0.98;
const double gearUpAndLockedThreshold = 0.02;

std::vector<std::string> lampNames = {
    "master-caution", "fire-warn", "fuel", "ovht",
    "irs", "apu", "flt-cont", "elec"};

uint8_t lampBits = 0;

FGFSTelnetSocket* global_fgSocket = nullptr;

void setupSubscriptions()
{
    global_fgSocket->subscribe("/gear/gear[0]/position-norm");
    global_fgSocket->subscribe("/gear/gear[1]/position-norm");
    global_fgSocket->subscribe("/gear/gear[2]/position-norm");

    for (auto s : lampNames) {
        global_fgSocket->subscribe("/instrumentation/weu/outputs/" + s + "-lamp");
    }

    global_fgSocket->subscribe("/surface-positions/flap-pos-norm[0]");
}

bool getInitialState()
{
    int attempts = 5;
    bool ok;
    lampBits = 0;

    assert(lampNames.size() == 8);

    for (int attempt=0; attempt < attempts; ++attempt) {
        bool attemptOk = true;
        for (int i=0; i<3; ++i) {
            ok = global_fgSocket->syncGetDouble("/gear/gear[" + to_string(i) + "]/position-norm",
                                    gearPositionNorm[i]);
            if (!ok)
                attemptOk = false;
        }

        ok = global_fgSocket->syncGetDouble("/surface-positions/flap-pos-norm[0]", flapPositionNorm);
        if (!ok)
            attemptOk = false;

        for (int i=0; i<8; ++i) {
            bool b;
            ok = global_fgSocket->syncGetBool("/instrumentation/weu/outputs/" + lampNames.at(i) + "-lamp", b);
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
       // write_port(Gear_I2C_Address, Gear_Port, ledByte);
    }
}

void updateLampLEDState()
{
    if (lastLampLEDState != lampBits) {
        lastLampLEDState = lampBits;
        std::cout << "lamp LED byte is now " << std::hex << (int) lampBits << std::endl;
       // write_port(Sixpack_I2C_Address, Sixpack_Lamp_Port, (char) lampBits);
    }
}

int pwmValueForFlapPos()
{
    if (flapPositionNorm <= 0.625) {
        double normLow = flapPositionNorm / 0.625;
        return 150 + static_cast<int>(normLow * 220);
    }

    double normHigh = (flapPositionNorm - 0.625) / 0.375;
    return 370 + static_cast<int>(normHigh * 110);
}

void updateFlapPosition()
{
    static int lastPWM = 0;
    int pwm = pwmValueForFlapPos();
    if (pwm == lastPWM)
        return;

    lastPWM = pwm;
#if defined(LINUX_BUILD)
    servoDriver->setPWM(0, 0, pwm);
#endif
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

   // write_port(Gear_I2C_Address, Gear_Port, ledByte);

    static uint8_t lampByte = 0;
    if (lampByte == 0)
        lampByte = 1;
    //write_port(Sixpack_I2C_Address, Sixpack_Lamp_Port, lampByte);
    lampByte <<= 1;
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
    } else if (message.find("/surface-positions/flap-pos-norm=") == 0) {
        flapPositionNorm =  std::stod(message.substr(33));
        updateFlapPosition();
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

void idleForTime(int timeSec)
{
    time_t endTime = time(nullptr) + timeSec;
    setSpecialLEDState(SpecialLEDState::ConnectBackoff);
    while (time(nullptr) < endTime) {
        ::usleep(500 * 1000);
        setSpecialLEDState(SpecialLEDState::ConnectBackoff2);
        ::usleep(500 * 1000);
        setSpecialLEDState(SpecialLEDState::ConnectBackoff);
    }
}

void defineGearSixpackInputs(GPIOPoller& i)
{
    i.addBinding(InputBinding{0x20, 0, 0, [](bool b) {
            std::cerr << "Fire warn push" << std::endl;
            global_fgSocket->write("run weu-fire-button");
    }, Trigger::High});

    i.addBinding(InputBinding{0x20, 0, 1, [](bool b) {
            std::cerr << "Master Caution push" << std::endl;
            global_fgSocket->write("run weu-caution-button");
    }, Trigger::High});

// master caution recall push
    i.addBinding(InputBinding{0x20, 0, 2, [](bool b) {
            std::cerr << "recall push" << std::endl;
            global_fgSocket->write("run weu-recall-button");
    }, Trigger::High});

// release binding for master-caution recall
    i.addBinding(InputBinding{0x20, 0, 2, [](bool b) {
            std::cerr << "recall release" << std::endl;
            global_fgSocket->write("run weu-recall-button-off");
    }, Trigger::Low});

// MIP autobrake settings
    i.addBinding(InputBinding{0x20, 0, 3, [](bool b) {
            std::cerr << "AB off" << std::endl;
            global_fgSocket->set("/controls/brakes/autobrake", "0");
    }, Trigger::High});

    i.addBinding(InputBinding{0x20, 0, 4, [](bool b) {
            std::cerr << "AB RTO" << std::endl;
            global_fgSocket->set("/controls/brakes/autobrake", "-1");
    }, Trigger::High});

    i.addBinding(InputBinding{0x20, 0, 5, [](bool b) {
            std::cerr << "AB 1" << std::endl;
            global_fgSocket->set("/controls/brakes/autobrake", "1");
    }, Trigger::High});

    i.addBinding(InputBinding{0x20, 0, 6, [](bool b) {
            std::cerr << "AB 2" << std::endl;
            global_fgSocket->set("/controls/brakes/autobrake", "2");
    }, Trigger::High});

    i.addBinding(InputBinding{0x20, 0, 7, [](bool b) {
            std::cerr << "AB 3" << std::endl;
            global_fgSocket->set("/controls/brakes/autobrake", "3");
    }, Trigger::High});

// gear port
    // 0 and 1 are outputs
    i.addBinding(InputBinding{0x20, 1, 2, [](bool b) {
        std::cerr << "gear down" << std::endl;
        global_fgSocket->set("/controls/gear/gear-down", "1");
    }, Trigger::High});

    i.addBinding(InputBinding{0x20, 1, 3, [](bool b) {
        std::cerr << "gear up" << std::endl;
        global_fgSocket->set("/controls/gear/gear-down", "0");
    }, Trigger::High});

    i.addBinding(InputBinding{0x20, 1, 4, [](bool b) {
        std::cerr << "gear off" << std::endl;
           // global_fgSocket->set("/controls/gear/gear-down", "0");
    }, Trigger::High});

// MIP reuse
    i.addBinding(InputBinding{0x20, 1, 5, [](bool b) {
                std::cerr << "AB MAX" << std::endl;
                global_fgSocket->set("/controls/brakes/autobrake", "4");
        }, Trigger::High});

    i.addBinding(InputBinding{0x20, 1, 6, [](bool b) {
                std::cerr << "SPD Less-than" << std::endl;
        }, Trigger::High});

    i.addBinding(InputBinding{0x20, 1, 7, [](bool b) {
                std::cerr << "SPD set" << std::endl;
        }, Trigger::High});
}

void defineMIPInputs(GPIOPoller& i)
{
    i.addBinding(InputBinding{0x21, 0, 0, [](bool b) {
        std::cerr << "N1 1" << std::endl;
    }, Trigger::High});

    i.addBinding(InputBinding{0x21, 0, 1, [](bool b) {
        std::cerr << "N1 2" << std::endl;
    }, Trigger::High});

    i.addBinding(InputBinding{0x21, 0, 2, [](bool b) {
        std::cerr << "N1 Both" << std::endl;
    }, Trigger::High});

    i.addBinding(InputBinding{0x21, 0, 3, [](bool b) {
        std::cerr << "N1 auto" << std::endl;
    }, Trigger::High});

    i.addBinding(InputBinding{0x21, 0, 4, [](bool b) {
        // N1 encoder
        std::cerr << "N1 encoder A " << b << std::endl;
    }});

    i.addBinding(InputBinding{0x21, 0, 5, [](bool b) {
        // N1 encoder
        std::cerr << "N1 encoder B " << b << std::endl;
    }});

    i.addBinding(InputBinding{0x21, 0, 6, [](bool b) {
        std::cerr << "Fuel-flow used" << std::endl;
    }, Trigger::High});

    i.addBinding(InputBinding{0x21, 0, 7, [](bool b) {
        std::cerr << "Fuel-flow reset" << std::endl;
    }, Trigger::High});

// second port
    i.addBinding(InputBinding{0x21, 1, 0, [](bool b) {
        std::cerr << "Speed AUTO" << std::endl;
    }, Trigger::High});

    i.addBinding(InputBinding{0x21, 1, 1, [](bool b) {
        std::cerr << "Speed V1" << std::endl;
    }, Trigger::High});

    i.addBinding(InputBinding{0x21, 1, 2, [](bool b) {
        std::cerr << "Speed Vr" << std::endl;
    }, Trigger::High});

    i.addBinding(InputBinding{0x21, 1, 3, [](bool b) {
        std::cerr << "Speed WT" << std::endl;
    }, Trigger::High});

    i.addBinding(InputBinding{0x21, 1, 4, [](bool b) {
        std::cerr << "Speed encoder A " << b << std::endl;
    }});

    i.addBinding(InputBinding{0x21, 1, 5, [](bool b) {
        std::cerr << "Speed encoder B " << b << std::endl;
    }});

    i.addBinding(InputBinding{0x21, 1, 6, [](bool b) {
        std::cerr << "MFD ENG" << std::endl;
    }, Trigger::High});

    i.addBinding(InputBinding{0x21, 1, 7, [](bool b) {
        std::cerr << "MFD SYS" << std::endl;
    }, Trigger::High});
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

    global_fgSocket = new FGFSTelnetSocket;

    GPIOPoller gearSixpackInputs(0x20);
    GPIOPoller mipInputs(0x21);

    defineGearSixpackInputs(gearSixpackInputs);
    defineMIPInputs(mipInputs);

    gearSixpackInputs.open();
    mipInputs.open();

    int reconnectBackoff = defaultReconnectBackoff;
    time_t lastReadTime = time(nullptr);

    while (true) {
        if (!global_fgSocket->isConnected()) {
        //    std::cerr << "starting connection" << std::endl;
            setSpecialLEDState(SpecialLEDState::Connecting);
            if (!global_fgSocket->connect(host, port)) {
                idleForTime(reconnectBackoff);
                reconnectBackoff = std::min(reconnectBackoff * 2, 30);
                continue;
            }

            // reset back-off after succesful connect
            reconnectBackoff = defaultReconnectBackoff;
            setSpecialLEDState(SpecialLEDState::DidConnect);

            if (!getInitialState()) {
                std::cerr << "failed to get initial state, will re-try" << std::endl;
                global_fgSocket->close();
                continue;
            }

            setupSubscriptions();
            setSpecialLEDState(SpecialLEDState::DidConnect);
            updateFlapPosition();
        }

        time_t nowSeconds = time(nullptr);
        if ((nowSeconds - lastReadTime) > keepAliveInterval) {
            // force a write to check for dead socket
            lastReadTime = nowSeconds;
            global_fgSocket->write("pwd");
        }

        bool ok = global_fgSocket->poll(pollHandler, 500 /* msec, so 20hz */);
        if (!ok) {
            // poll failed, close out
            global_fgSocket->close();
            continue;
        }

        gearSixpackInputs.update();
        mipInputs.update();

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
