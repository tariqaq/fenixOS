// Simulator stub: plain malloc, and a fixed "free" figure (the board's usual value) so the
// About screen renders the same every run.
#pragma once
#include <stddef.h>
#include <stdlib.h>
#define MALLOC_CAP_DMA (1 << 3)
#define MALLOC_CAP_8BIT (1 << 2)
static inline void* heap_caps_malloc(size_t n, unsigned) { return malloc(n); }
static inline size_t heap_caps_get_free_size(unsigned) { return 106 * 1024; }
