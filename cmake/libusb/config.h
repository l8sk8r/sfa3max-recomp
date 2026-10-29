/* libusb's config.h for SFA3MR's own build of libusb (Windows, Visual Studio or LLVM-MinGW).
 * Only SDL's Switch 2 driver uses libusb: those pads start sending input only after a start-up
 * sequence over a USB bulk interface, which Windows drives with WinUSB. */
#define PLATFORM_WINDOWS 1
#define ENABLE_LOGGING 1
#define DEFAULT_VISIBILITY
#ifdef _MSC_VER
#define _TIMESPEC_DEFINED 1
#pragma warning(disable:4127 4200 4201 4324 4996)
#define PRINTF_FORMAT(a, b)
#else
#define PRINTF_FORMAT(a, b) __attribute__((__format__(__printf__, a, b)))
#define HAVE_SYS_TIME_H 1
#endif
