

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

#include <hidapi/hidapi.h>

using namespace std;

const std::string fgfsHost = "simpc.local";
const int fgfsPort = 5501;
const int cduIndex = 0; // captain's CDU, F/O is likely CDU=1, and the center/spare one is CDU=2

const int defaultReconnectBackoff = 4;
const int keepAliveInterval = 10;

hid_device* hidComplexDevice = nullptr;
hid_device* hidPlainDevice = nullptr;

void idleForTime(int timeSec)
{
    time_t endTime = time(nullptr) + timeSec;
   // setSpecialLEDState(SpecialLEDState::ConnectBackoff);
    while (time(nullptr) < endTime) {
        ::usleep(500 * 1000);
      //  setSpecialLEDState(SpecialLEDState::ConnectBackoff2);
        ::usleep(500 * 1000);
       // setSpecialLEDState(SpecialLEDState::ConnectBackoff);
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
        cerr << "failed to write command" << endl;
        auto w = hid_error(dev);
        if (w) {
            cerr << "\tHID msg:" << ws2s(wstring(w)) << endl;
        }
        return false;
    }

    uint8_t buf[8];
    int rd = hid_read_timeout(dev, buf, 8, 0);
    if (rd > 0) {
        cout << "read bytes" << rd << endl;
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
            // this is the reported name on Macl;
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

    cout << "starting CDU up" << endl;

    // init CDU
    writeBytes(hidComplexDevice, {0x1, 0x1, 0x1, 0x10, 0x48, 0x97, 0xA9, 0x0});

    cout << "did init" << endl;

    // request switch (keypress) callbacks
  //  writeBytes(hidComplexDevice, {0x18, 0x02, 0x0});

    cout << "enabled key callbacks" << endl;


    // enable LEDs
    writeBytes(hidComplexDevice, {0x14, 0x01});
    writeBytes(hidComplexDevice, {0x15, 0x01, 0xff});
    writeBytes(hidComplexDevice, {0x15, 0x02, 0xff});

    cout << "set LEDs" << endl;


    // turn on the screen
    std::vector<uint8_t> enableLCD = {0xd8};
    writeBytes(hidPlainDevice, enableLCD);
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
    FGFSTelnetSocket socket;

    initCDU();

    int reconnectBackoff = defaultReconnectBackoff;
    time_t lastReadTime = time(nullptr);

    while (true) {
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

    hid_close(hidComplexDevice);
    hid_close(hidPlainDevice);

    hid_exit();
    return EXIT_SUCCESS;
}