

#include <string>
#include <iostream>
#include <cstdlib>
#include <vector>
#include <cassert>
#include <algorithm>
#include <exception>
#include <locale>
#include <codecvt>

#include <unistd.h>
#include <ctime>
#include <signal.h>

#include "FGFSTelnetSocket.h"
#include "CDUKeys.h"

#include <hidapi/hidapi.h>

using namespace std;

const std::string fgfsHost = "simpc.local";
const int fgfsPort = 5501;
const int cduIndex = 0; // captain's CDU, F/O is likely CDU=1, and the center/spare one is CDU=2

const int defaultReconnectBackoff = 4;
const int keepAliveInterval = 10;

hid_device* hidComplexDevice = nullptr;
hid_device* hidPlainDevice = nullptr;
bool keepRunning = true;

std::vector<bool> keyState;

enum class Lamp
{
    Call = 1,
    Message = 2 ,
    Exec,
    Fail = 3,
    Offset = 4,
    
};

bool writeBytes(hid_device* dev, const std::vector<uint8_t>& bytes);
void readCDU();
void exitCleanup();

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

void setBacklight(uint8_t brightness)
{
    writeBytes(hidComplexDevice, {0x2, 0xC3, 0xC0, 0x02, brightness, 0, 0, 0});
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
    // Telnet code doesn't send index for [0]
    if (message.find("/gear/gear/position-norm=") == 0) {
       // gearPositionNorm[0] = std::stod(message.substr(25));
       // LEDUpdateRequired = true;
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

void setupSubscriptions(FGFSTelnetSocket& socket)
{
    socket.subscribe("/gear/gear[0]/position-norm");
    socket.subscribe("/gear/gear[1]/position-norm");
    socket.subscribe("/gear/gear[2]/position-norm");
}

bool getInitialState(FGFSTelnetSocket& socket)
{

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
    setLamp(Lamp::Call, true);
    setLamp(Lamp::Offset, true);

    // turn on the screen
    std::vector<uint8_t> enableLCD = {0xd8, 0xff, 0, 0};
    writeBytes(hidPlainDevice, enableLCD);

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

void processInputReport(uint8_t reportId, uint8_t* data, int len)
{
    if ((reportId == 0x19) || (reportId == 0x1A) || (reportId == 0x1B)) {
        // compute active keys
        size_t offset = (reportId - 0x19) * 32;
        for (size_t b = 0; b < (4 * 8); ++b) {
            const bool on = testBit(data, b);
            if (on != keyState[offset + b]) {
                keyState[offset + b] = on;
                const Key k = static_cast<Key>(offset + b);
                cout << "toggled:" << offset + b << " which is " << codeForKey(k) << endl;
            }
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

    // install signal handlers
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, interruptHandler);

    FGFSTelnetSocket socket;
    keyState.resize(static_cast<int>(Key::NumKeys));

    initCDU();

    int reconnectBackoff = defaultReconnectBackoff;
    time_t lastReadTime = time(nullptr);

    while (keepRunning) {
        if (!socket.isConnected()) {
            if (!socket.connect(host, port)) {
                idleForTime(reconnectBackoff);
                reconnectBackoff = std::min(reconnectBackoff * 2, 30);
                continue;
            }

            // reset back-off after succesful connect
            reconnectBackoff = defaultReconnectBackoff;
           // setSpecialLEDState(SpecialLEDState::DidConnect);

            if (!getInitialState(socket)) {
                std::cerr << "failed to get initial state, will re-try" << std::endl;
                socket.close();
                continue;
            }

            setupSubscriptions(socket);
         //   setSpecialLEDState(SpecialLEDState::DidConnect);
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