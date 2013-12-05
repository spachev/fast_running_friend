#ifndef URL_H
#define URL_H

#include <stdlib.h>

size_t url_fetch(const char* url, char* buf, size_t buf_size, const char* post_fields,...);

#endif