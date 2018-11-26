cmake_minimum_required(VERSION 3.0)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

include(GNUInstallDirs)

project(simCDUDriver)

set(SOURCES
  simCDUDriver.cpp
  ../simGPIODriver/FGFSTelnetSocket.cpp
  ../simGPIODriver/FGFSTelnetSocket.h
)

add_executable(simCDUDriver ${SOURCES})

# for FGFSTelnetSocket.h
target_include_directories(simCDUDriver PRIVATE ${CMAKE_SOURCE_DIR}/../simGPIODriver)

###########################################################
# hidAPI build borrowed and hacked up from FlightGear ####

set(HIDAPI_ROOT ${CMAKE_SOURCE_DIR}/../hidapi)
set(HIDAPI_SOURCES ${HIDAPI_ROOT}/hidapi/hidapi.h)

if (WIN32)
    list(APPEND HIDAPI_SOURCES ${HIDAPI_ROOT}/windows/hid.c)
elseif(APPLE)
    find_library(IOKIT_FRAMEWORK IOKit)
    find_library(CF_LIBRARY CoreFoundation)

    list(APPEND HIDAPI_SOURCES ${HIDAPI_ROOT}/mac/hid.c)
else()
    list(APPEND HIDAPI_SOURCES ${HIDAPI_ROOT}/linux/hid.c)
endif(WIN32)


add_library(hidapi STATIC  ${HIDAPI_SOURCES} )

target_link_libraries(hidapi ${UDEV_LIBRARIES} ${IOKIT_FRAMEWORK} ${CF_LIBRARY} )

target_include_directories(hidapi PRIVATE ${HIDAPI_ROOT}/hidapi)
target_include_directories(hidapi PUBLIC ${HIDAPI_ROOT})
###########################################################

target_link_libraries(simCDUDriver PUBLIC hidapi)

install(TARGETS simCDUDriver RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
