/* SFA3MR shim: aligned allocation for the decoder. */
#pragma once
#include <cstdlib>
#include <cstddef>
#ifdef _WIN32
#include <malloc.h>
inline void *AllocateAlignedMemory(size_t size, size_t align) { return _aligned_malloc(size ? size : 1, align); }
inline void FreeAlignedMemory(void *p) { _aligned_free(p); }
#else
inline void *AllocateAlignedMemory(size_t size, size_t align) { void *p = nullptr; return posix_memalign(&p, align, size ? size : 1) == 0 ? p : nullptr; }
inline void FreeAlignedMemory(void *p) { free(p); }
#endif
