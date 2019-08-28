#include "GPIO.h"


#if defined(LINUX_BUILD)
extern "C" {
  #include "ABE_IoPi.h"
}
#else
#include "dummy_IoPi.h"
#endif

void GPIOPoller::open()
{
    IOPi_init(_address); // initialise one of the io pi buses on i2c address 0x20
    set_port_direction(_address, 0, _portInputMask[0]);
    set_port_direction(_address, 1, _portInputMask[1]);
    set_port_pullups(_address, 0, _portInputMask[0]);
    set_port_pullups(_address, 1, _portInputMask[1]);
    updateOutputs();
}

void GPIOPoller::addOutput(OutputBindingRef b)
{
    assert(b);
    _outputs.push_back(b);
    UpdateCallback cb = std::bind(std::mem_fn(&GPIOPoller::markOutputDirty), this, b->port());
    b->setCallback(cb);
    _portOutputMask[b->port()] |= 1 << b->bit();
    _portOutputsDirty[b->port()] = true;
}

void GPIOPoller::update()
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

    updateOutputs();
}

void GPIOPoller::updateOutputs()
{
    for (int port=0; port < 2; ++port) {
        if (!_portOutputsDirty[port]) {
            continue;
        }

        _portOutputsDirty[port] = false;
        uint8_t value = 0;

        for (auto& b : _outputs) {
            if ((b->port() == port) && b->state()) {
                value |= 1 << b->bit();
            }
        }

        write_port(_address, port, value);
    } // of port iteration
}
