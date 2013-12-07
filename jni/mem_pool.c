#include <stdlib.h>

#include "mem_pool.h"
#include "log.h"

#define MEM_ALIGN(n) (((n)+(0x3))&~(0x3))

static Mem_pool_block* get_block(uint block_size)
{
  Mem_pool_block* b = (Mem_pool_block*)malloc(block_size + sizeof(*b));

  if (!b)
    return 0;

  b->cur = b->buf = (char*)b + sizeof(*b);
  b->buf_end = b->buf + block_size;
  b->next = 0;
  LOGE("Allocated block at %p b->buf=%p b->buf_end=%p", b, b->buf, b->buf_end);
  return b;
}

int mem_pool_init(Mem_pool* pool, uint min_block_size)
{
  Mem_pool_block* b;
  min_block_size = MEM_ALIGN(min_block_size);

  if (!(b = get_block(min_block_size)))
    return 1;

  pool->first_block = 0;
  LL_APPEND(pool->first_block,b);
  pool->min_block_size = min_block_size;
  return 0;
}

char* mem_pool_alloc(Mem_pool* pool, uint size)
{
  Mem_pool_block* block;
  size = MEM_ALIGN(size);

  /* find a block with sufficient free space */
  LL_FOREACH(pool->first_block,block)
  {
    if (block->buf_end - block->cur >= size)
    {
      char* res = block->cur;
      block->cur += size;
      LOGE("Allocated memory at %p size = %d new cur=%p", res, size, block->cur);
      return res;
    }
  }

  /* if not, allocate a new block */
  if (!(block = get_block(pool->min_block_size * (size/pool->min_block_size + 1))))
    return 0;

  block->cur += size;
  LL_APPEND(pool->first_block,block);
  LOGE("Allocated memory in new block at %p size = %d new cur=%p", block->buf, size, block->cur);
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

char* mem_pool_dup(Mem_pool* pool, const char* src, uint size)
{
  char* buf = mem_pool_alloc(pool,size);

  if (!buf)
    return 0;
 
  memcpy(buf,src,size);
  return buf;
}

char* mem_pool_cdup(Mem_pool* pool, const char* src, uint size)
{
  char* buf = mem_pool_alloc(pool,size+1);

  if (!buf)
    return 0;
 
  memcpy(buf,src,size);
  buf[size] = 0;
  return buf;
}
