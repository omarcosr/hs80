#ifndef CUI_HS80_MEMORY_H
#define CUI_HS80_MEMORY_H
#include <stdbool.h>
#include <stddef.h>
/* One private heap for the single, on-demand CUI window. */
bool cui_hs80_memory_begin(void);
void cui_hs80_memory_end(void);
void *cui_hs80_malloc(size_t size);
void *cui_hs80_calloc(size_t count,size_t size);
void *cui_hs80_realloc(void *pointer,size_t size);
void cui_hs80_free(void *pointer);
#endif
