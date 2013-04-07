#ifndef MEM_POOL_H
#define MEM_POOL_H

#include "utlist.h"

typedef struct st_mem_pool_block
{
  char* buf,*buf_end,*cur;
  struct st_mem_pool_block* next;
} Mem_pool_block;

typedef struct 
{
  Mem_pool_block first_block;
} Mem_pool;

int mem_pool_init(Mem_pool* pool, uint block_size);
char* mem_pool_alloc(Mem_pool* pool, uint size);
int mem_pool_free(Mem_pool* pool);

#endif