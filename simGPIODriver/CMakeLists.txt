cmake_minimum_required(VERSION 3.0)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

include(GNUInstallDirs)

project(simGPIODriver)


set(SOURCES
  simGPIODriver.cpp
  FGFSTelnetSocket.cpp
  FGFSTelnetSocket.h
)

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    SET(ABE_sources
        ABE_IoPi.c
        ABE_IoPi.h)

    SET(driver_sources
        Adafruit_PWMServoDriver.cpp
        Adafruit_PWMServoDriver.h
        LEDDriver.cpp
        LEDDriver.h)
endif()

add_executable(simGPIODriver ${SOURCES} ${ABE_sources} ${driver_sources})


if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    set_target_properties (simGPIODriver PROPERTIES COMPILE_DEFINITIONS "LINUX_BUILD")
endif()

install(TARGETS simGPIODriver RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    add_executable(servoTest servoTest.cpp ${driver_sources})
    add_executable(ledTest ledTest.cpp ${driver_sources})

endif()
