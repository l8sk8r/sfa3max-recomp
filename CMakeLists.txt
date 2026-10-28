# SFA3R: build the native game from the translated code.
#   python tools/prepare.py            (writes work/ and build/generated/)
#   cmake -B out                       (build.ps1: Ninja + LLVM-MinGW it downloads; or Visual Studio 2026:
#                                       cmake -B out -G "Visual Studio 18 2026" -A x64)
#   cmake --build out --config Release
#   out/Release/sfa3r.exe --work work
cmake_minimum_required(VERSION 3.20)
project(sfa3r C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(SFA3R_GENERATED_DIR "${CMAKE_SOURCE_DIR}/build/generated" CACHE PATH "Translated code written by tools/prepare.py")
file(GLOB GEN_SRC "${SFA3R_GENERATED_DIR}/*.c")
if(NOT GEN_SRC)
  message(FATAL_ERROR "No translated code in ${SFA3R_GENERATED_DIR}. Run: python tools/prepare.py")
endif()
file(GLOB RT_SRC "${CMAKE_SOURCE_DIR}/src/runtime/*.c" "${CMAKE_SOURCE_DIR}/src/runtime/*.cpp")

# ATRAC3/ATRAC3+ decoder (FFmpeg-derived, LGPL 2.1+), vendored from PPSSPP
file(GLOB AT3_SRC "${CMAKE_SOURCE_DIR}/third_party/at3_standalone/*.cpp")
add_library(at3_standalone STATIC ${AT3_SRC})
target_include_directories(at3_standalone PUBLIC "${CMAKE_SOURCE_DIR}/third_party/at3_standalone"
                                                 "${CMAKE_SOURCE_DIR}/third_party/at3_standalone/shim")
if(MSVC)
  target_compile_options(at3_standalone PRIVATE /W0)
  target_compile_definitions(at3_standalone PRIVATE _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(at3_standalone PRIVATE -w)
endif()

# KIRK (PSP crypto engine, GPL 3.0+), vendored from PPSSPP: PSP save data IMPORT / EXPORT
file(GLOB KIRK_SRC "${CMAKE_SOURCE_DIR}/third_party/libkirk/*.c")
add_library(libkirk STATIC ${KIRK_SRC})
target_include_directories(libkirk PUBLIC "${CMAKE_SOURCE_DIR}/third_party/libkirk")
if(MSVC)
  target_compile_options(libkirk PRIVATE /W0)
  target_compile_definitions(libkirk PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(libkirk PRIVATE -w)
endif()

option(SFA3R_SDL "Build the window, input and sound layer (SDL3, downloaded automatically)" ON)
option(SFA3R_WATCH "Debug build: memory watchpoints (--watch); slower" OFF)
option(SFA3R_EGL "Headless OpenGL through EGL for tests (Linux)" OFF)

add_executable(sfa3r ${RT_SRC} ${GEN_SRC})
target_include_directories(sfa3r PRIVATE "${CMAKE_SOURCE_DIR}/src/runtime" "${SFA3R_GENERATED_DIR}")
target_link_libraries(sfa3r PRIVATE at3_standalone libkirk)
if(SFA3R_EGL)
  target_compile_definitions(sfa3r PRIVATE SFA3R_EGL)
  target_link_libraries(sfa3r PRIVATE EGL)
endif()
if(SFA3R_WATCH)
  target_compile_definitions(sfa3r PRIVATE SFA3R_WATCH)
endif()

if(SFA3R_SDL)
  include(FetchContent)
  FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz
    URL_HASH SHA256=7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(SDL3)
  target_link_libraries(sfa3r PRIVATE SDL3::SDL3-static)
  target_compile_definitions(sfa3r PRIVATE SFA3R_SDL)
endif()

if(WIN32)
  target_link_libraries(sfa3r PRIVATE ws2_32)
  if(NOT CMAKE_CONFIGURATION_TYPES)   # Ninja (build.ps1's own toolchain): out/Release/sfa3r.exe, as Visual Studio puts it
    if(NOT CMAKE_BUILD_TYPE)
      set(CMAKE_BUILD_TYPE Release)
    endif()
    set_target_properties(sfa3r PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}")
  endif()
  if(MINGW)
    target_link_options(sfa3r PRIVATE -static)   # no MinGW runtime DLLs next to the exe
  endif()
  # Exe icon (res/sfa3r.ico); SDL also picks it up for the window and taskbar
  target_sources(sfa3r PRIVATE "${CMAKE_SOURCE_DIR}/res/sfa3r.rc")
endif()

# The version (VERSION, shown on EX. SETTING); changing the file re-runs CMake
file(STRINGS "${CMAKE_SOURCE_DIR}/VERSION" SFA3R_VER LIMIT_COUNT 1)
string(STRIP "${SFA3R_VER}" SFA3R_VER)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/VERSION")
target_compile_definitions(sfa3r PRIVATE "SFA3R_VERSION=\"V${SFA3R_VER}\"")

if(MSVC)
  target_compile_options(sfa3r PRIVATE /W3 /wd4102 /wd4100 /wd4996 /bigobj /utf-8 /we4013)   # C4013: a function used before it is declared is an error, as with gcc below
  target_compile_definitions(sfa3r PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(sfa3r PRIVATE -Wall -Wno-unused-label -Wno-unused-parameter -Wno-unused-variable $<$<COMPILE_LANGUAGE:C>:-Werror=implicit-function-declaration>)
  target_link_libraries(sfa3r PRIVATE m)
endif()
