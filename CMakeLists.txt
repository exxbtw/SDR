cmake_minimum_required(VERSION 3.16)
project(PlutoSDR CXX)

set(CMAKE_BUILD_TYPE Debug)

find_package(SoapySDR REQUIRED)

set(MAIN_SOURCE_FILES
    src/main.cpp
)

add_executable(main ${MAIN_SOURCE_FILES})

target_link_libraries(main ${SoapySDR_LIBRARIES})
