# libusb 1.0.29 (LGPL-2.1-or-later), built from its release source and linked into SDL, Windows only.
# SDL's Switch 2 driver needs it: a Switch 2 Pro Controller sends no input until it has been sent a
# start-up sequence over its USB bulk interface (Windows gives that interface the WinUSB driver
# itself). SDL only builds the driver when it finds libusb, so this target stands in for SDL's
# FindLibUSB: LibUSB::LibUSB exists before SDL looks for it.
FetchContent_Declare(libusb
  URL https://github.com/libusb/libusb/releases/download/v1.0.29/libusb-1.0.29.tar.bz2
  URL_HASH SHA256=5977fc950f8d1395ccea9bd48c06b3f808fd3c2c961b44b0c2e6e29fc3a70a85
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(libusb)   # no CMakeLists.txt of its own: only downloaded
set(_U "${libusb_SOURCE_DIR}/libusb")
add_library(sfa3mr_libusb STATIC
  ${_U}/core.c ${_U}/descriptor.c ${_U}/hotplug.c ${_U}/io.c ${_U}/strerror.c ${_U}/sync.c
  ${_U}/os/events_windows.c ${_U}/os/threads_windows.c ${_U}/os/windows_common.c
  ${_U}/os/windows_usbdk.c ${_U}/os/windows_winusb.c)
target_include_directories(sfa3mr_libusb PUBLIC "${_U}" PRIVATE "${CMAKE_SOURCE_DIR}/cmake/libusb")
if(MSVC)
  target_compile_options(sfa3mr_libusb PRIVATE /W0)
else()
  target_compile_options(sfa3mr_libusb PRIVATE -w)
endif()
# an imported target that links ours: SDL exports its targets, and an export may only name imported
# dependencies
add_library(LibUSB::LibUSB INTERFACE IMPORTED GLOBAL)
set_target_properties(LibUSB::LibUSB PROPERTIES
  INTERFACE_LINK_LIBRARIES sfa3mr_libusb INTERFACE_INCLUDE_DIRECTORIES "${_U}")
# what SDL's FindLibUSB and its compile check would otherwise look for on disk
set(LibUSB_INCLUDE_PATH "${_U}" CACHE PATH "" FORCE)
set(LibUSB_LIBRARY sfa3mr_libusb CACHE STRING "" FORCE)
set(HAVE_LIBUSB_H 1 CACHE INTERNAL "")
set(SDL_HIDAPI_LIBUSB ON CACHE BOOL "" FORCE)
set(SDL_HIDAPI_LIBUSB_SHARED OFF CACHE BOOL "" FORCE)   # linked in: no libusb-1.0.dll
