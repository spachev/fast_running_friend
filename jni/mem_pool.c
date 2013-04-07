#include <stdlib.h>

#include "mem_pool.h"


int mem_pool_init(Mem_pool* pool, uint block_size);
{
  if (!(pool->buf = (char*)malloc(block_size)))
    return 1;
  
  pool->buf_end = pool->buf + block_size;
  pool->cur = pool->buf;
  pool->next = 0;
  return 0;
}

char* mem_pool_alloc(Mem_pool* pool, uint size)
{
}

int mem_pool_free(Mem_pool* pool)
{
}
