#include <cui/cui_hs80_memory.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void){
 for(int cycle=0;cycle<4;cycle++){
  CHECK(cui_hs80_memory_begin());CHECK(!cui_hs80_memory_begin());
  unsigned char *zero=cui_hs80_calloc(32,4);CHECK(zero);
  for(int i=0;i<128;i++)CHECK(zero[i]==0);
  memset(zero,0x5a,128);
  unsigned char *grown=cui_hs80_realloc(zero,65536);CHECK(grown);
  for(int i=0;i<128;i++)CHECK(grown[i]==0x5a);
  CHECK(cui_hs80_realloc(grown,0)==NULL);
  CHECK(cui_hs80_calloc(SIZE_MAX,2)==NULL);
  void *from_null=cui_hs80_realloc(NULL,64);CHECK(from_null);cui_hs80_free(from_null);cui_hs80_free(NULL);
  /* Closing the window also releases allocations its owner did not free. */
  CHECK(cui_hs80_malloc(2*1024*1024));cui_hs80_memory_end();cui_hs80_memory_end();
 }
 puts("PASS: GUI heap lifetime, zeroing, realloc preservation, overflow, repeated open/close");return 0;
}
