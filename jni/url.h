#ifndef URL_H
#define URL_H

#include <stdlib.h>
#include "uthash.h"

typedef struct
{
  char* key;
  char* val;
  size_t key_len,val_len;
  UT_hash_handle hh;
} Url_hash_entry;

typedef Url_hash_entry Url_hash;

size_t url_fetch(const char* url, char* buf, size_t buf_size, const char* post_fields,...);
size_t url_fetch_with_hash(const char* url, char* buf, size_t buf_size, Url_hash* h);
#endif