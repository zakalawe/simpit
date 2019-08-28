
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

#include <argp.h> // from Glibc or Homebrew 'argp-standalone'

#include "FGFSTelnetSocket.h"
#include "GPIO.h"
#include "LEDDriver.h"

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

LEDDriver* global_ledDriver = nullptr;
bool global_testMode = false;

#if defined(LINUX_BUILD)

#include "Adafruit_PWMServoDriver.h"

Adafruit_PWMServoDriver* servoDriver = nullptr;

void initGPIO()
{

//    servoDriver = new Adafruit_PWMServoDriver(0x40, 60); // I2C address
//    servoDriver->begin();
}

#else

void initGPIO()
{
    std::cout << "dummy GPIO init"<< std::endl;
}
#endif



std::string fgfsHost = "simpc.local";
int fgfsPort = 5501;

const int defaultReconnectBackoff = 4;
const int keepAliveInterval = 10;

double gearPositionNorm[3] = {0.0, 0.0, 0.0};
double flapPositionNorm = 0.0;

const double gearDownAndLockedThreshold = 0.98;
const double gearUpAndLockedThreshold = 0.02;

std::vector<std::string> lampNames = {
    "master-caution", "fire-warn", "fuel", "ovht",
    "irs", "apu", "flt-cont", "elec"};

FGFSTelnetSocket* global_fgSocket = nullptr;

LEDOutput* gearLamps[6];
LEDOutput* sixpackLamps[6];
OutputBindingRef fireCautionLamps[2];
OutputBindingRef afdsLamps[3];
OutputBindingRef autobrakeLamps[4];

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

void setLampBit(uint8_t index, bool b)
{
    if (index < 2) {
        // GPIO for the 12v lamps
        fireCautionLamps[index]->setState(b);
    } else {
        // LED output for the sixpack
        sixpackLamps[index - 2]->setState(b);
    }
}

bool getInitialState()
{
    int attempts = 5;
    bool ok;

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
            setLampBit(i, b);
        }

        if (attemptOk) { // all values in this attempt worked, we are done
            return true;
        }
    } // of attempts

    return false;
}

// map analogue gear positions to lamp discrete values
void updateGearLEDState()
{
    uint8_t offset = 0;
    for (int i=0; i<3; ++i) {
        const double p = gearPositionNorm[i];
        bool isDownAndLocked = (p >= gearDownAndLockedThreshold);
        bool gearUnsafe = (p > gearUpAndLockedThreshold) &&
                (p <= gearDownAndLockedThreshold);

        gearLamps[0 + offset]->setState(gearUnsafe);
        gearLamps[1 + offset]->setState(isDownAndLocked);
        offset += 2;
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

}


const char* WEU_OUTPUT_PREFIX = "/instrumentation/weu/outputs/";
const int WEU_OUTPUT_PREFIX_LEN = 29;
const char* WEU_LAMP_SUFFIX = "-lamp=";
const int WEU_LAMP_SUFFIX_LEN = 6;

void pollHandler(const std::string& message)
{
    bool gearUpdateRequired  = false;

    // Telnet code doesn't send index for [0]
    if (message.find("/gear/gear/position-norm=") == 0) {
        gearPositionNorm[0] = std::stod(message.substr(25));
        gearUpdateRequired = true;
    } else if (message.find("/gear/gear[0]/position-norm=") == 0) {
        gearPositionNorm[0] = std::stod(message.substr(28));
        gearUpdateRequired = true;
    } else if (message.find("/gear/gear[1]/position-norm=") == 0) {
        gearPositionNorm[1] = std::stod(message.substr(28));
        gearUpdateRequired = true;
    } else if (message.find("/gear/gear[2]/position-norm=") == 0) {
        gearPositionNorm[2] = std::stod(message.substr(28));
        gearUpdateRequired = true;
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
                const int lampIndex = std::distance(lampNames.begin(), it);
                const auto v = message.substr(lampSuffix + WEU_LAMP_SUFFIX_LEN);
                const bool b = (v == "true");
                setLampBit(lampIndex, b);
            } catch (std::exception& e) {
                std::cerr << "exception processing value data:" << message.substr(lampSuffix + WEU_LAMP_SUFFIX_LEN) << std::endl;
                std::cerr << "full message was:" << message << std::endl;
            }
        }
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

    if (gearUpdateRequired) {
        updateGearLEDState();
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

void defineGearOutputs()
{
    for (int i=0; i<6; i++) {
        gearLamps[i] = new LEDOutput(global_ledDriver, i);
    }
}

void defineSixpackOutputs(GPIOPoller& i)
{
    fireCautionLamps[0] = OutputBindingRef{new OutputBinding{1, 0}};
    fireCautionLamps[1] = OutputBindingRef{new OutputBinding{1, 1}};
    i.addOutput(fireCautionLamps[0]);
    i.addOutput(fireCautionLamps[1]);

    for (int i=0; i<6; i++) {
        sixpackLamps[i] = new LEDOutput(global_ledDriver, i + 8);
    }
}

void defineMIPOutputs(GPIOPoller& gpio)
{
    for (uint8_t i=0; i<4; i++) {
        autobrakeLamps[i] = OutputBindingRef{new OutputBinding{0, i}};
        gpio.addOutput(autobrakeLamps[i]);
    }
}

void updateTestMode()
{
#if 0
LEDOutput* gearLamps[6];
LEDOutput* sixpackLamps[6];
OutputBindingRef fireCautionLamps[2];
OutputBindingRef afdsLamps[3];
OutputBindingRef autobrakeLamps[4];

#endif
    static int ledLampIt = 5; // so we start at zero
    gearLamps[ledLampIt]->setState(false);
    sixpackLamps[ledLampIt]->setState(false);
    ledLampIt = (ledLampIt + 1) % 6;
    gearLamps[ledLampIt]->setState(true);
    sixpackLamps[ledLampIt]->setState(true);

    static bool fireCautionToggle = false;
    fireCautionToggle = !fireCautionToggle;
    fireCautionLamps[0]->setState(fireCautionToggle);
    fireCautionLamps[1]->setState(!fireCautionToggle);
}

const char* argp_program_version = "simGPIO 0.2";

static struct argp_option options[] = {
  {"host",  'h', "HOSTNAME",      0,  "Host to connect to" },
  {"test",   't', 0,      0,  "Run in test mode - don't connect to FGFS" },
  {"port",   'p', "PORT",     0,  "Use PORT as the Websocket port" },
  { nullptr }
};

static error_t parse_opt (int key, char *arg, struct argp_state *state)
{
    switch (key)
    {
    case 'h':
      fgfsHost = arg;
      break;
    case 'p':
      fgfsPort = std::stoi(arg);
      break;
    case 't':
      global_testMode = true;
      break;

    case ARGP_KEY_ARG:
      break;

    case ARGP_KEY_END:
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct argp argp = { options, parse_opt, nullptr, nullptr };

int main(int argc, char* argv[])
{
    argp_parse(&argp, argc, argv, 0, 0, nullptr);

    // install signal handlers?
    signal(SIGPIPE, SIG_IGN);

    global_fgSocket = new FGFSTelnetSocket;
    global_ledDriver = new LEDDriver();

    GPIOPoller gearSixpackInputs(0x20);
    GPIOPoller mipInputs(0x21);
    GPIOPoller mipOutputs(0x22);

    defineGearSixpackInputs(gearSixpackInputs);
    defineMIPInputs(mipInputs);
    defineMIPOutputs(mipOutputs);
    defineGearOutputs();
    defineSixpackOutputs(mipInputs);

    gearSixpackInputs.open();
    mipInputs.open();

    int reconnectBackoff = defaultReconnectBackoff;
    time_t lastReadTime = time(nullptr);
    time_t testModeLastTime;

    while (true) {
        if (!global_testMode && !global_fgSocket->isConnected()) {
        //    std::cerr << "starting connection" << std::endl;
            setSpecialLEDState(SpecialLEDState::Connecting);
            if (!global_fgSocket->connect(fgfsHost, fgfsPort)) {
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

        if (global_testMode) {
            time_t nowSeconds = time(nullptr);
            if (testModeLastTime != nowSeconds) {
                testModeLastTime = nowSeconds;
                updateTestMode();
            }
        } else {
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
        }

        gearSixpackInputs.update();
        mipInputs.update();
        // check for kill signal
    }

    return EXIT_SUCCESS;
}
