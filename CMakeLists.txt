cmake_minimum_required(VERSION 3.21)

project(DockerRoboshopServer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.7 REQUIRED COMPONENTS Core Network)

add_executable(DockerRoboshopServer
    src/main.cpp
    src/HttpsServer.cpp
    src/HttpsServer.h
)

target_link_libraries(DockerRoboshopServer
    PRIVATE
        Qt6::Core
        Qt6::Network
)

if(MSVC)
    target_compile_options(DockerRoboshopServer PRIVATE /W4 /permissive-)
else()
    target_compile_options(DockerRoboshopServer PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS DockerRoboshopServer RUNTIME DESTINATION bin)
