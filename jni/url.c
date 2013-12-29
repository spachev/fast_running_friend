#include "url.h"
#include "curl_config.h"
#include "curl/curl.h"
#include "log.h"
#include "utstring.h"

#define PRE_ALLOC_SIZE 16384*8

struct Curl_data
{
  char* buf;
  size_t buf_size,data_size;
};

static size_t process_data(void *contents, size_t size, size_t nmemb, void *userp)
{
  struct Curl_data* data = (struct Curl_data*)userp;
  size_t cp_size = size * nmemb;
  size_t real_size = cp_size + data->data_size;

  //LOGE("in process_data(): size=%d nmemb=%d data=%p cp_size=%d real_size=%d", size, nmemb, data->data,
  //      cp_size,real_size);

  // the buffer is full so 
  // we pretend we have copied the data, but we really just throw it away because we have no room
  if (data->data_size >= data->buf_size)
    return cp_size;

  if (real_size > data->buf_size)
  {
    cp_size = data->buf_size - data->data_size;
  }

  memcpy(data->buf + data->data_size, contents, cp_size);
  data->data_size = real_size;
  return cp_size;
}

static UT_string* escape_fields(CURL* ch, const char* fields, va_list ap)
{
  UT_string* res = 0;
  utstring_new(res);

  for (;*fields;*fields++)
  {
    if (*fields == '?')
    {
      char* f,*arg;
      arg = va_arg(ap,char*);
      f = curl_easy_escape(ch,arg,0);
      utstring_bincpy(res,f,strlen(f));
      curl_free(f);
    }
    else
      utstring_bincpy(res,fields,1);
  }

  return res;
}

size_t url_fetch(const char* url, char* buf, size_t buf_size, const char* post_fields,...)
{
  CURL *ch;
  CURLcode res;
  struct Curl_data data;
  char* content = 0;
  UT_string* esc_post_fields = 0;

  data.buf = buf;
  data.buf_size = buf_size;
  data.data_size = 0;

  if (curl_global_init(CURL_GLOBAL_ALL))
    return 0;

  if (!(ch = curl_easy_init()))
  {
    curl_global_cleanup();
    return 0;
  }

  if (curl_easy_setopt(ch,CURLOPT_URL,url))
    goto err;

  if (post_fields)
  {
    va_list ap;
    va_start(ap,post_fields);
    esc_post_fields = escape_fields(ch,post_fields,ap);
    va_end(ap);

    if (!esc_post_fields)
    {
      goto err;
    }

    if (curl_easy_setopt(ch,CURLOPT_POSTFIELDS,utstring_body(esc_post_fields)))
    {
      goto err;
    }
    LOGE("Sending post_fields %s", utstring_body(esc_post_fields));
  }

  if (curl_easy_setopt(ch,CURLOPT_WRITEFUNCTION,process_data) || 
      curl_easy_setopt(ch,CURLOPT_WRITEDATA,&data))
    return 0;

  curl_easy_setopt(ch, CURLOPT_USERAGENT, "Fast Running Friend/1.0");

  if (curl_easy_perform(ch))
    goto err;

  //LOGE("curl_easy_perform(), got %d bytes ", data.data_size);

err:

    curl_easy_cleanup(ch);
    curl_global_cleanup();

    if (esc_post_fields)
      utstring_free(esc_post_fields);

    return data.data_size;
}

size_t url_fetch_with_hash(const char* url, char* buf, size_t buf_size, Url_hash* h)
{
  CURL *ch;
  CURLcode res;
  struct Curl_data data;
  char* content = 0;
  UT_string* esc_post_fields = 0;
  Url_hash_entry* cur_he;
  
  data.buf = buf;
  data.buf_size = buf_size;
  data.data_size = 0;
  
  if (curl_global_init(CURL_GLOBAL_ALL))
    return 0;
  
  if (!(ch = curl_easy_init()))
  {
    curl_global_cleanup();
    return 0;
  }
  
  if (curl_easy_setopt(ch,CURLOPT_URL,url))
    goto err;

  utstring_new(esc_post_fields);

  for(cur_he = h; cur_he; cur_he = cur_he->hh.next)
  {
    char* f;
    utstring_bincpy(esc_post_fields,cur_he->key,cur_he->key_len);
    utstring_bincpy(esc_post_fields,"=",1);

    if (!(f = curl_easy_escape(ch,cur_he->val,cur_he->val_len)))
    {
      LOGE("Error in curl_easy_escape()");
      goto err;
    }

    utstring_bincpy(esc_post_fields,f,strlen(f));

    if (cur_he->hh.next)
      utstring_bincpy(esc_post_fields,"&",1);

    curl_free(f);
  }

  if (curl_easy_setopt(ch,CURLOPT_POSTFIELDS,utstring_body(esc_post_fields)))
  {
    goto err;
  }

  LOGE("Sending post_fields %s", utstring_body(esc_post_fields));

  if (curl_easy_setopt(ch,CURLOPT_WRITEFUNCTION,process_data) ||
    curl_easy_setopt(ch,CURLOPT_WRITEDATA,&data))
    return 0;
  
  curl_easy_setopt(ch, CURLOPT_USERAGENT, "Fast Running Friend/1.0");
  
  if (curl_easy_perform(ch))
    goto err;
  
  //LOGE("curl_easy_perform(), got %d bytes ", data.data_size);
  
  err:
  
  curl_easy_cleanup(ch);
  curl_global_cleanup();
  
  if (esc_post_fields)
    utstring_free(esc_post_fields);
  
  return data.data_size;
}