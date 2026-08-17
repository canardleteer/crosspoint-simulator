#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "Arduino.h"

// Host mapping of ESP-IDF heap capability malloc. Caps are accepted and
// ignored: the desktop process has one heap. Do not treat these sizes as
// ESP SRAM or PSRAM pressure.

#define MALLOC_CAP_EXEC (1u << 0)
#define MALLOC_CAP_32BIT (1u << 1)
#define MALLOC_CAP_8BIT (1u << 2)
#define MALLOC_CAP_DMA (1u << 3)
#define MALLOC_CAP_PID2 (1u << 4)
#define MALLOC_CAP_PID3 (1u << 5)
#define MALLOC_CAP_PID4 (1u << 6)
#define MALLOC_CAP_PID5 (1u << 7)
#define MALLOC_CAP_PID6 (1u << 8)
#define MALLOC_CAP_PID7 (1u << 9)
#define MALLOC_CAP_SPIRAM (1u << 10)
#define MALLOC_CAP_INTERNAL (1u << 11)
#define MALLOC_CAP_DEFAULT (1u << 12)
#define MALLOC_CAP_IRAM_8BIT (1u << 13)
#define MALLOC_CAP_RETENTION (1u << 14)
#define MALLOC_CAP_RTCRAM (1u << 15)

inline void *heap_caps_malloc(size_t size, uint32_t /*caps*/) {
  return std::malloc(size);
}

inline void *heap_caps_calloc(size_t n, size_t size, uint32_t /*caps*/) {
  return std::calloc(n, size);
}

inline void *heap_caps_realloc(void *ptr, size_t size, uint32_t /*caps*/) {
  return std::realloc(ptr, size);
}

inline void heap_caps_free(void *ptr) { std::free(ptr); }

inline void *heap_caps_aligned_alloc(size_t alignment, size_t size,
                                     uint32_t /*caps*/) {
  if (alignment < sizeof(void *))
    alignment = sizeof(void *);
#if defined(_WIN32)
  return _aligned_malloc(size, alignment);
#else
  void *ptr = nullptr;
  if (posix_memalign(&ptr, alignment, size) != 0)
    return nullptr;
  return ptr;
#endif
}

inline size_t heap_caps_get_free_size(uint32_t /*caps*/) {
  return ESP.getFreeHeap();
}

inline size_t heap_caps_get_largest_free_block(uint32_t /*caps*/) {
  return ESP.getMaxAllocHeap();
}

inline size_t heap_caps_get_minimum_free_size(uint32_t /*caps*/) {
  return ESP.getMinFreeHeap();
}

inline size_t heap_caps_get_total_size(uint32_t /*caps*/) {
  return ESP.getHeapSize();
}
