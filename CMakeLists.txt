cmake_minimum_required(VERSION 3.25)
project(Aetherwake VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB_RECURSE AETHERWAKE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_executable(Aetherwake ${AETHERWAKE_SOURCES})
target_include_directories(Aetherwake PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(MSVC)
  target_compile_options(Aetherwake PRIVATE /W4 /permissive-)
else()
  target_compile_options(Aetherwake PRIVATE -Wall -Wextra -Wpedantic)
endif()
