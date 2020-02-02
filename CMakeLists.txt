cmake_minimum_required(VERSION 3.13)
project(FileRecv)

#set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}  -std=c++17")

add_executable(FileRecv main.cpp src/Server.cpp inc/Server.hpp src/Client.cpp inc/Client.hpp inc/ftpDataUnit.hpp)

target_link_libraries(FileRecv -levent)