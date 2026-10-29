/* SFA3MR shim: decoder log messages go to stderr (errors and warnings only). */
#pragma once
#include <cstdio>
namespace Log { enum { ME }; }
#define DEBUG_LOG(cat, ...) do { (void)(cat); } while (0)
#define INFO_LOG(cat, ...)  do { (void)(cat); } while (0)
#define WARN_LOG(cat, ...)  do { (void)(cat); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)
#define ERROR_LOG(cat, ...) do { (void)(cat); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)
