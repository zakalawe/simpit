#ifndef GPIO_H
#define GPIO_H

#include <vector>
#include <functional>
#include <cstdint>
#include <memory>

using Callback = std::function<void(bool)>;
using UpdateCallback = std::function<void(void)>;

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

class OutputBinding
{
public:
    OutputBinding(uint8_t port, uint8_t bit) :
        _port(port),
        _bit (bit)
    {

    }

    void setState(bool b)
    {
        if (b == _state)
            return;
        _state = b;
        if (_changedCallback) {
            _changedCallback();
        }
    }

    uint8_t port() const
    {
        return _port;
    }

    uint8_t bit() const
    {
        return _bit;
    }

    bool state() const
    {
        return _state;
    }

    void setCallback(UpdateCallback cb)
    {
        _changedCallback = cb;
    }
private:
    bool _state = false;
    UpdateCallback _changedCallback;
    const uint8_t _port;
    const uint8_t _bit;
};

using OutputBindingRef = std::shared_ptr<OutputBinding>;
using InputBindingVec = std::vector<InputBinding>;
using OutputBindingVec = std::vector<OutputBindingRef>;

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

    void addOutput(OutputBindingRef b);

    void open();

    void update();
private:
    void updateOutputs();

    void markOutputDirty(uint8_t port)
    {
        assert(port < 2);
        _portOutputsDirty[port] = true;
    }

    const uint8_t _address;
    uint8_t _portStates[2] = {0,0};
    uint8_t _portInputMask[2] = {0,0};
    uint8_t _portOutputMask[2] = {0,0};

    bool _portOutputsDirty[2] = {false, false};
    InputBindingVec _bindings;
    OutputBindingVec _outputs;
};


#endif