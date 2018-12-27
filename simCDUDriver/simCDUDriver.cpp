

#include <string>
#include <iostream>
#include <cstdlib>
#include <vector>
#include <cassert>
#include <algorithm>
#include <exception>
#include <locale>
#include <codecvt>
#include <sstream>

#include <unistd.h>
#include <ctime>
#include <signal.h>

#include "FGFSTelnetSocket.h"
#include "CDUKeys.h"

#include <hidapi/hidapi.h>

using namespace std;

const std::string fgfsHost = "simpc.local";
const int fgfsPort = 5501;
int cduIndex = 0; // captain's CDU, F/O is likely CDU=1, and the center/spare one is CDU=2
string cduPropertyPrefix = "/instrumentation/cdu/";

const int defaultReconnectBackoff = 4;
const int keepAliveInterval = 10;

hid_device* hidComplexDevice = nullptr;
hid_device* hidPlainDevice = nullptr;
bool keepRunning = true;

FGFSTelnetSocket telnetSocket;
std::vector<bool> keyState;

enum class Lamp
{
    Exec = 0,
    Call = 1,
    Message = 2,
    Fail = 3,
    Offset = 4,
    Count
};

std::vector<std::string> lampNames = {
    "exec", "call", "message", "fail", "offset"
};
    
bool writeBytes(hid_device* dev, const std::vector<uint8_t>& bytes);
void readCDU();
void exitCleanup();

bool stringAsBool(const std::string& s)
{
    if ((s == "1") || (s == "true"))
        return true;

    return false;
}

void interruptHandler(int)
{
    keepRunning = false;
}

void setLamp(Lamp l, bool on)
{
    writeBytes(hidComplexDevice, 
        {0x15, static_cast<uint8_t>(l), static_cast<uint8_t>(on ? 0xff : 0), 
        0, 0, 0, 0, 0});
}

void enableBacklight()
{
    // not sure which of these is critical
    writeBytes(hidComplexDevice, {0x2, 0xC3, 0x06, 0xA5, 0x5A, 0, 0, 0});
    writeBytes(hidComplexDevice, {0x2, 0xC3, 0xC0, 0x0C, 2, 0, 0, 0});
    writeBytes(hidComplexDevice, {0x2, 0xC3, 0xC0, 0x0D, 0, 0, 0, 0});
    
    // shutdown command seems to be?
//    writeBytes(hidComplexDevice, {0x2, 0xC3, 0x06, 0xA5, 0x5A, 0, 0, 0});
}

void setBacklight(uint8_t brightness)
{
    writeBytes(hidComplexDevice, {0x2, 0xC3, 0xC0, 0x02, brightness, 0, 0, 0});
}

void queryProperty(uint8_t propId, uint8_t bytes)
{
    writeBytes(hidComplexDevice, {0x2, propId, 0xA9, 0, 0, 0, 0, 0});
    writeBytes(hidComplexDevice, {0x3, bytes, 0xA9, 0, 0, 0, 0, 0});
}

void idleForTime(int timeSec)
{
    time_t endTime = time(nullptr) + timeSec;
    setLamp(Lamp::Fail, true);
    while (time(nullptr) < endTime) {
        readCDU();
        ::usleep(400 * 1000);
        setLamp(Lamp::Message, true);
        setBacklight(2);
        readCDU();
        ::usleep(400 * 1000);
        setLamp(Lamp::Message, false);
        setBacklight(8);
    }
}

void pollHandler(const std::string& message)
{
    if (message.find(cduPropertyPrefix) == 0) {
        string relPath = message.substr(cduPropertyPrefix.size());

        if (relPath.find("outputs/") == 0) {
            const auto eqPos = relPath.find("=");
            string outputName = relPath.substr(8, eqPos - 8);
            auto it = std::find(lampNames.begin(), lampNames.end(), outputName);
            if (it != lampNames.end()) {
                const bool b = stringAsBool(relPath.substr(eqPos + 1));
                const Lamp l = static_cast<Lamp>(std::distance(lampNames.begin(), it));
                setLamp(l, b);
            } else {
                cerr << "CDU: unknown output:" << outputName << endl;
            }
        } else {
            cout << "got telnet message:" << message << endl;
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
}

void setupSubscriptions()
{   
    for (int l = 0; l < static_cast<int>(Lamp::Count); ++l) {
        telnetSocket.subscribe(cduPropertyPrefix + "outputs/" + lampNames.at(l));
    }
}

bool getInitialState()
{
    bool ok;
    for (int l = 0; l < static_cast<int>(Lamp::Count); ++l) {
        bool b;
        ok = telnetSocket.syncGetBool(cduPropertyPrefix + "outputs/" + lampNames.at(l), b);
        setLamp(static_cast<Lamp>(l), b);
    }

    return true;
}

string ws2s(const std::wstring& wstr)
{
    using convert_typeX = std::codecvt_utf8<wchar_t>;
    std::wstring_convert<convert_typeX, wchar_t> converterX;

    return converterX.to_bytes(wstr);
}

bool writeBytes(hid_device* dev, const std::vector<uint8_t>& bytes)
{
    int len = hid_write(dev, bytes.data(), bytes.size());
    if (len != bytes.size()) {
        cerr << "failed to write command:" << len << "/" << bytes.size() << endl;
        auto w = hid_error(dev);
        if (w) {
            cerr << "\tHID msg:" << ws2s(wstring(w)) << endl;
        }
        return false;
    }

    return true;
}

void initCDU()
{
    hid_init();

    struct hid_device_info* dev = hid_enumerate(0x1FD1, 0x03EA);
    for (; dev != nullptr; dev = dev->next) {
        wstring wName(dev->product_string);
        if (wName == L"Complex Interfaces") {
            hidComplexDevice = hid_open_path(dev->path);
        } else if (wName == L"Plain I/O") {
            hidPlainDevice = hid_open_path(dev->path);
        } else if (wName == L"interfaceIT Controller v2") {
            // this is the reported name on Mac;
            if (dev->interface_number == 0) {
                hidPlainDevice = hid_open_path(dev->path);
            } else if (dev->interface_number == 1) {
                hidComplexDevice = hid_open_path(dev->path);
            }
        } else {
            cerr << "Unknown TEKWork device:" << ws2s(wName) << endl;
        }
    }

    hid_free_enumeration(dev);

    if (!hidComplexDevice || !hidPlainDevice) {
        cerr << "failed to find HID devices for the CDU" << endl;
        exit(EXIT_FAILURE);
    }

    // init CDU
    writeBytes(hidComplexDevice, {0x1, 0x1, 0x1, 0x10, 0x48, 0x97, 0xA9, 0xff});

    // request switch (keypress) callbacks
    writeBytes(hidComplexDevice, {0x18, 0x02, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0});

    // enable LEDs
    writeBytes(hidComplexDevice, {0x14, 0x01, 0, 0, 0, 0, 0, 0});

    // turn on the screen
    std::vector<uint8_t> enableLCD = {0xd8, 0xff, 0, 0};
    writeBytes(hidPlainDevice, enableLCD);

    enableBacklight();
    setBacklight(0x7f);
    
    atexit(exitCleanup);
}

void shutdownCDU()
{
    if (hidComplexDevice) {
        writeBytes(hidComplexDevice, {0x1, 0, 0, 0, 0x48, 0x97, 0xA9, 0xff});
        hid_close(hidComplexDevice);
        hidComplexDevice = nullptr;
    }

    if (hidPlainDevice) {
        // turn off the screen
        std::vector<uint8_t> disableLCD = {0xff, 0, 0, 0};
        writeBytes(hidPlainDevice, disableLCD);

        hid_close(hidPlainDevice);
        hidPlainDevice = nullptr;
    }
    
    hid_exit();
}

bool testBit(uint8_t* data, size_t index)
{
    const size_t byteIndex = index >> 3;
    const uint8_t b = data[byteIndex];
    return (b >> (index & 0x7)) & 0x1;
}

void sendCommandForKey(Key k)
{
    char c = charForKey(k);
    ostringstream os;
    if (c > 0) {
        os << "run cdu-key cdu=" << cduIndex << " key=" << static_cast<int>(c);
    } else {
        string code = codeForKey(k);
        if (code.find("lsk-") == 0) {
            const string lskName = code.substr(4, 1);
            // fix base-0 vs base-1 difference :(
            int lskIndex = std::stoi(code.substr(5));
            os << "run cdu-lsk cdu=" << cduIndex << " lsk=" << lskName << (lskIndex + 1);
        } else { 
            os << "run cdu-button-" << code << " cdu=" << cduIndex; 
        }
    }

    if (telnetSocket.isConnected()) {
        telnetSocket.write(os.str());
    }
}

void sendUpCommandForKey(Key k)
{
    string code = codeForKey(k);
    ostringstream os;
    os << "run cdu-button-" << code << "-up cdu=" << cduIndex; 

    if (telnetSocket.isConnected()) {
        telnetSocket.write(os.str());
    }
}

void processInputReport(uint8_t reportId, uint8_t* data, int len)
{
    if ((reportId == 0x19) || (reportId == 0x1A) || (reportId == 0x1B)) {
        // compute active keys
        size_t offset = (reportId - 0x19) * 32;
        for (size_t b = 0; b < (4 * 8); ++b) {
            const bool on = testBit(data, b);
            const Key k = static_cast<Key>(offset + b);
            const int kIndex = static_cast<int>(k);
            if (on == keyState.at(kIndex)) {
                continue; // no actual change
            }

            if (on) {
                sendCommandForKey(k); // press
            } else {
                // release, only need this fora few keys
                if (k == Key::Clear) {
                    sendUpCommandForKey(k); // press
                }
            }

            keyState[kIndex] = on;
        }
    } else if ((reportId == 0x2) || (reportId == 0x1C)) {
        // command acknowledgement report
    } else {
        std::cerr << "unhandled input report ID:" << static_cast<int>(reportId) << endl;
    }
}

void readCDU()
{
    // in practice, this means look for key-presses
    while (true) {
        uint8_t report[9];
        int len = hid_read_timeout(hidComplexDevice, report, sizeof(report), 0);
        if (len == 0)
            return; // no report to read

        if (len < 0) {
            // error
            std::cerr << "error reading from CDU device" << endl;
            return;
        }

        processInputReport(report[0], report + 1, len - 1);
    }
}

void exitCleanup()
{
    cout << "Shutting down CDU HID..." << endl;
    shutdownCDU();
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

    // argument to set CDU index

    // telnet code omits index for [0] case, so we need
    // to adjust this path when cduIndex is > 0
    ostringstream os;
    if (cduIndex > 0) {
        os << "/instrumentation/cdu[" << cduIndex << "]/";
        cduPropertyPrefix = os.str();
    }

    // install signal handlers
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, interruptHandler);

    keyState.resize(128);

    initCDU();

    int reconnectBackoff = defaultReconnectBackoff;
    time_t lastReadTime = time(nullptr);

    while (keepRunning) {
        if (!telnetSocket.isConnected()) {
            setLamp(Lamp::Fail, true);
            if (!telnetSocket.connect(host, port)) {
                idleForTime(reconnectBackoff);
                reconnectBackoff = std::min(reconnectBackoff * 2, 30);
                continue;
            }

            reconnectBackoff = defaultReconnectBackoff;

            if (!getInitialState()) {
                std::cerr << "failed to get initial state, will re-try" << std::endl;
                telnetSocket.close();
                continue;
            }

            setupSubscriptions();
            cout << "CDU connected to FlightGear" << endl;
            setLamp(Lamp::Fail, false);
        }

        time_t nowSeconds = time(nullptr);
        if ((nowSeconds - lastReadTime) > keepAliveInterval) {
            // force a write to check for dead socket
            lastReadTime = nowSeconds;
            telnetSocket.write("pwd");
        }

        readCDU();

        bool ok = telnetSocket.poll(pollHandler, 50);
        if (!ok) {
            // poll failed, close out
            telnetSocket.close();
            continue;
        }

        // pollGearLeverState(socket);
        // pollWEUButtons(socket);

        // if (LEDUpdateRequired) {
        //     LEDUpdateRequired = false;
        //     updateGearLEDState();
        // }

        // if (SixpackLEDUpdateRequired) {
        //     SixpackLEDUpdateRequired = false;
        //     updateLampLEDState();
        // }
        // check for kill signal
    }

    // atexit handler will run to shutdown HID
    return EXIT_SUCCESS;
}
