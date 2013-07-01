#ifndef MEM_POOL_H
#define MEM_POOL_H

#include <sys/types.h>
#include "utlist.h"

typedef struct st_mem_pool_block
{
  char* buf,*buf_end,*cur;
  struct st_mem_pool_block* next;
} Mem_pool_block;

typedef struct 
{
  Mem_pool_block* first_block;
  uint min_block_size;
} Mem_pool;

int mem_pool_init(Mem_pool* pool, uint min_block_size);
char* mem_pool_alloc(Mem_pool* pool, uint size);
char* mem_pool_dup(Mem_pool* pool, const char* src, uint size);
int mem_pool_free(Mem_pool* pool);

#endif