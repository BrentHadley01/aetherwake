cmake_minimum_required(VERSION 3.25)
project(Aetherwake VERSION 0.1.0 LANGUAGES CXX)

# ENet's maintained release still declares an older CMake policy baseline.
# Keep its policy compatibility local to this dependency graph on CMake 4+.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Minimum CMake policy version for legacy dependencies")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
find_package(OpenGL REQUIRED)
option(AETHERWAKE_FETCH_DEPS "Download native engine dependencies into the local build directory" ON)

if(AETHERWAKE_FETCH_DEPS)
  FetchContent_Declare(SDL3 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git GIT_TAG release-3.2.16)
  FetchContent_Declare(glm GIT_REPOSITORY https://github.com/g-truc/glm.git GIT_TAG 1.0.1)
  FetchContent_Declare(entt GIT_REPOSITORY https://github.com/skypjack/entt.git GIT_TAG v3.15.0)
  FetchContent_Declare(json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.11.3)
  FetchContent_Declare(enet GIT_REPOSITORY https://github.com/lsalzman/enet.git GIT_TAG v1.3.17)
  FetchContent_Declare(JoltPhysics GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git GIT_TAG v5.3.0)
  FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.92.2b)
  FetchContent_Declare(tinygltf GIT_REPOSITORY https://github.com/syoyo/tinygltf.git GIT_TAG v2.9.3)
  set(ENET_INSTALL OFF CACHE BOOL "" FORCE)
  set(JPH_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
  set(JPH_BUILD_HELLO_WORLD OFF CACHE BOOL "" FORCE)
  set(JPH_BUILD_VIEWER OFF CACHE BOOL "" FORCE)
  set(JPH_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(SDL3 glm entt json enet JoltPhysics)
  # Jolt keeps its top-level build entry point in Build/ rather than repository root.
  add_subdirectory(${joltphysics_SOURCE_DIR}/Build ${joltphysics_BINARY_DIR})
  FetchContent_GetProperties(imgui)
  if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
  endif()
  FetchContent_GetProperties(tinygltf)
  if(NOT tinygltf_POPULATED)
    FetchContent_Populate(tinygltf)
  endif()
  add_library(aetherwake_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp)
  target_include_directories(aetherwake_imgui PUBLIC ${imgui_SOURCE_DIR})
endif()

file(GLOB_RECURSE AETHERWAKE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_executable(Aetherwake ${AETHERWAKE_SOURCES})
target_include_directories(Aetherwake PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(AETHERWAKE_FETCH_DEPS)
  target_link_libraries(Aetherwake PRIVATE SDL3::SDL3 OpenGL::GL glm::glm EnTT::EnTT nlohmann_json::nlohmann_json enet Jolt aetherwake_imgui)
  target_include_directories(Aetherwake PRIVATE ${tinygltf_SOURCE_DIR})
endif()

add_custom_command(TARGET Aetherwake POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:Aetherwake>/assets")

# Ship the SDL runtime next to the Windows executable so it can launch outside
# the build environment.
if(AETHERWAKE_FETCH_DEPS)
  add_custom_command(TARGET Aetherwake POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:Aetherwake>)
endif()

if(MSVC)
  target_compile_options(Aetherwake PRIVATE /W4 /permissive-)
else()
  target_compile_options(Aetherwake PRIVATE -Wall -Wextra -Wpedantic)
endif()
