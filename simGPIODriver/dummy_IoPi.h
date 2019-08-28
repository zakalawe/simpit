#ifndef DUMMY_IOPI_H
#define DUMMY_IOPI_H

#include <cstdint>

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

void set_port_direction(char address, char port, char direction)
{

}

void set_port_pullups(char address, char port, char value)
{

}

#endif