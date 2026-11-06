#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "cui/cui_hs80_memory.h"

static HANDLE window_heap;

bool cui_hs80_memory_begin(void){
 if(window_heap)return false;
 window_heap=HeapCreate(0,0,0);
 return window_heap!=NULL;
}
void cui_hs80_memory_end(void){
 HANDLE heap=window_heap;window_heap=NULL;
 if(heap)HeapDestroy(heap);
}
void *cui_hs80_malloc(size_t size){
 return window_heap?HeapAlloc(window_heap,0,size?size:1):NULL;
}
void *cui_hs80_calloc(size_t count,size_t size){
 if(!window_heap||(size&&count>SIZE_MAX/size))return NULL;
 size_t bytes=count*size;
 return HeapAlloc(window_heap,HEAP_ZERO_MEMORY,bytes?bytes:1);
}
void *cui_hs80_realloc(void *pointer,size_t size){
 if(!pointer)return cui_hs80_malloc(size);
 if(!size){cui_hs80_free(pointer);return NULL;}
 return window_heap?HeapReAlloc(window_heap,0,pointer,size):NULL;
}
void cui_hs80_free(void *pointer){
 if(pointer&&window_heap)HeapFree(window_heap,0,pointer);
}
