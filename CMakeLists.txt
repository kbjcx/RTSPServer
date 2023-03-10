CMAKE_MINIMUM_REQUIRED(VERSION 3.16)

PROJECT(RTSPServer)

set(CMAKE_EXPORT_COMPILE_COMMANDS on)
set(server main.cpp rtp.cpp)

add_executable(server ${server})
