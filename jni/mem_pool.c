#include <stdlib.h>

#include "mem_pool.h"

static Mem_pool_block* get_block(uint block_size)
{
  Mem_pool_block* b = (Mem_pool_block*)malloc(block_size + sizeof(*b));
  
  if (!b)
    return 0;
  
  b->cur = b->buf = (char*)b + sizeof(*b);
  b->buf_end = b->buf + block_size;
  b->next = 0;
  return b;
}

int mem_pool_init(Mem_pool* pool, uint min_block_size)
{
  if (!(pool->first_block = get_block(min_block_size)))
    return 1;
  
  pool->min_block_size = min_block_size;
  return 0;
}

char* mem_pool_alloc(Mem_pool* pool, uint size)
{
  Mem_pool_block* block;
  
  /* find a block with sufficient free space */
  LL_FOREACH(pool->first_block,block)
  {
    if (block->buf_end - block->cur < size)
    {
      char* res = block->cur;
      block->cur += size;
      return res;
    }
  }
  
  /* if not, allocate a new block */
  if (!(block = get_block(pool->min_block_size * (size/pool->min_block_size + 1))))
    return 0;
  
  LL_APPEND(pool->first_block,block);
  
  block->cur += size;
  return block->buf;
}

int mem_pool_free(Mem_pool* pool)
{
  Mem_pool_block* block, *tmp;
  
  LL_FOREACH_SAFE(pool->first_block,block,tmp)
  {
    free(block);
  }
  
  pool->first_block = 0;
  return 0;
}
