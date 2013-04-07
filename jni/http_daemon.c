#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <microhttpd.h>

#include <jni.h>
#include "uthash.h"
#include "utstring.h"
#include "config_vars.h"
#include "log.h"


static int exit_requested = 0;
static JNIEnv* jni_env = 0;
static jobject* jni_cfg = 0;
static int httpd_running = 0;

static UT_string* get_config_form(const char* msg);

static int init_post_config();
static int end_post_config();


/**
 * Invalid method page.
 */
#define METHOD_ERROR "<html><head><title>Illegal request</title></head><body>Error in request.</body></html>"

/**
 * Invalid URL page.
 */
#define NOT_FOUND_ERROR "<html><head><title>Not found</title></head><body>Page not found.</body></html>"

/**
 * Front page. (/)
 */
#define MAIN_PAGE "<html><head><title>Welcome</title></head><body><form action=\"/2\" method=\"post\">What is your name? <input type=\"text\" name=\"v1\" value=\"%s\" /><input type=\"submit\" value=\"Next\" /></body></html>"

/**
 * Second page. (/2)
 */
#define SECOND_PAGE "<html><head><title>Tell me more</title></head><body><a href=\"/\">previous</a> <form action=\"/S\" method=\"post\">%s, what is your job? <input type=\"text\" name=\"v2\" value=\"%s\" /><input type=\"submit\" value=\"Next\" /></body></html>"

/**
 * Second page (/S)
 */
#define SUBMIT_PAGE "<html><head><title>Ready to submit?</title></head><body><form action=\"/F\" method=\"post\"><a href=\"/2\">previous </a> <input type=\"hidden\" name=\"DONE\" value=\"yes\" /><input type=\"submit\" value=\"Submit\" /></body></html>"

/**
 * Last page.
 */
#define LAST_PAGE "<html><head><title>Thank you</title></head><body>Thank you.</body></html>"

/**
 * Name of our cookie.
 */
#define COOKIE_NAME "session"


/**
 * State we keep for each user/session/browser.
 */
struct Session
{
  /**
   * We keep all sessions in a linked list.
   */
  struct Session *next;

  /**
   * Unique ID for this session. 
   */
  char sid[33];

  /**
   * Reference counter giving the number of connections
   * currently using this session.
   */
  unsigned int rc;

  /**
   * Time when this session was last active.
   */
  time_t start;
  const char* msg;
};


/**
 * Data kept per request.
 */
struct Request
{

  /**
   * Associated session.
   */
  struct Session *session;

  /**
   * Post processor handling form data (IF this is
   * a POST request).
   */
  struct MHD_PostProcessor *pp;

  /**
   * URL to serve in response to this POST (if this request 
   * was a 'POST')
   */
  const char *post_url;

};


/**
 * Linked list of all active sessions.  Yes, O(n) but a
 * hash table would be overkill for a simple example...
 */
static struct Session *sessions;

#define CONFIG_PAGE_TITLE "FastRunningFriend Configuration"

static UT_string* get_config_form(const char* msg)
{
  UT_string* res;
  Config_var* cfg_var_p;
  
  // aborts on malloc() failure, which is OK - if things are so bad that malloc fails it is
  // OK to exit for now. TODO: fix so there is clean exit on malloc() failure
  utstring_new(res);
  utstring_printf(res, "<html><head><title>" CONFIG_PAGE_TITLE 
    "</title></head><body><h1>" CONFIG_PAGE_TITLE "</h1>");
 
  if (!jni_env || !jni_cfg)
  {
    utstring_printf(res, "Internal error</body></html>");
    return res;
  }
    
  if (msg && *msg)
  {
    utstring_printf(res, "<p>%s</p>", msg);
  }
  
  utstring_printf(res,"<form method=post><table border=1>");
  
  for (cfg_var_p = config_vars; cfg_var_p->name; cfg_var_p++)
  {
    char buf[512];
    if ((*cfg_var_p->printer)(jni_env,jni_cfg,cfg_var_p,buf,sizeof(buf)))
      goto err;
    //TODO: html escape of buf
    utstring_printf(res,"<tr><td>%s</td><td>"
       "<input name=\"%s\"type=text size=40 value=\"%s\">\n",
       cfg_var_p->lookup_name,cfg_var_p->lookup_name,buf);
  }
  
  utstring_printf(res,"<tr><td colspan=2 align=center>"
    "<input type=submit name=submit value='Update Configuration'> </td></tr></table>\n" 
    "</form>\n</body></html>\n");
  return res;
err:
  utstring_free(res);
  return 0;
}



/**
 * Return the session handle for this connection, or 
 * create one if this is a new user.
 */
static struct Session *
get_session (struct MHD_Connection *connection)
{
  struct Session *ret;
  const char *cookie;

  cookie ;
  if ((cookie = MHD_lookup_connection_value (connection,
          MHD_COOKIE_KIND,
          COOKIE_NAME)))
  {
      /* find existing session */
      ret = sessions;
      
      while (ret)
      {
        if (0 == strcmp (cookie, ret->sid))
          break;
        ret = ret->next;
      }
      if (NULL != ret)
      {
        ret->rc++;
        return ret;
      }
  }
    
 /* create fresh session */
  if (!(ret = calloc (1, sizeof (struct Session))))
  {           
    fprintf (stderr, "calloc error: %s\n", strerror (errno));
    return NULL; 
  }
  /* not a super-secure way to generate a random session ID,
     but should do for a simple example... */
  snprintf (ret->sid,
      sizeof (ret->sid),
      "%X%X%X%X",
      (unsigned int) random (),
      (unsigned int) random (),
      (unsigned int) random (),
      (unsigned int) random ());
  ret->rc++;  
  ret->start = time (NULL);
  ret->next = sessions;
  sessions = ret;
  return ret;
}


/**
 * Type of handler that generates a reply.
 *
 * @param cls content for the page (handler-specific)
 * @param mime mime type to use
 * @param session session information
 * @param connection connection to process
 * @param MHD_YES on success, MHD_NO on failure
 */
typedef int (*PageHandler)(const void *cls,
         const char *mime,
         struct Session *session,
         struct MHD_Connection *connection);


/**
 * Entry we generate for each page served.
 */ 
struct Page
{
  /**
   * Acceptable URL for this page.
   */
  const char *url;

  /**
   * Mime type to set for the page.
   */
  const char *mime;

  /**
   * Handler to call to generate response.
   */
  PageHandler handler;

  /**
   * Extra argument to handler.
   */ 
  const void *handler_cls;
};


/**
 * Add header to response to set a session cookie.
 *
 * @param session session to use
 * @param response response to modify
 */ 
static void
add_session_cookie (struct Session *session,
        struct MHD_Response *response)
{
  char cstr[256];
  snprintf (cstr,
      sizeof (cstr),
      "%s=%s",
      COOKIE_NAME,
      session->sid);
  if (MHD_NO == 
      MHD_add_response_header (response,
             MHD_HTTP_HEADER_SET_COOKIE,
             cstr))
    {
      fprintf (stderr, 
         "Failed to set session cookie header!\n");
    }
}


static int
handle_page (const void *cls,
        const char *mime,
        struct Session *session,
        struct MHD_Connection *connection)
{
  int ret;
  char *reply;
  UT_string* reply_s;
  struct MHD_Response *response;
  
  if (!(reply_s = get_config_form(session->msg)))
    return MHD_NO;
  
  reply = utstring_body(reply_s);
  /* return static form */
  response = MHD_create_response_from_buffer (strlen (reply),
                (void *) reply,
                MHD_RESPMEM_MUST_COPY);
  add_session_cookie (session, response);
  MHD_add_response_header (response,
         MHD_HTTP_HEADER_CONTENT_ENCODING,
         mime);
  ret = MHD_queue_response (connection, 
          MHD_HTTP_OK, 
          response);
  MHD_destroy_response (response);
  utstring_free(reply_s);
  return ret;
}


/**
 * Handler used to generate a 404 reply.
 *
 * @param cls a 'const char *' with the HTML webpage to return
 * @param mime mime type to use
 * @param session session handle 
 * @param connection connection to use
 */
static int
not_found_page (const void *cls,
    const char *mime,
    struct Session *session,
    struct MHD_Connection *connection)
{
  int ret;
  struct MHD_Response *response;

  /* unsupported HTTP method */
  response = MHD_create_response_from_buffer (strlen (NOT_FOUND_ERROR),
                (void *) NOT_FOUND_ERROR,
                MHD_RESPMEM_PERSISTENT);
  ret = MHD_queue_response (connection, 
          MHD_HTTP_NOT_FOUND, 
          response);
  MHD_add_response_header (response,
         MHD_HTTP_HEADER_CONTENT_ENCODING,
         mime);
  MHD_destroy_response (response);
  return ret;
}



#define MAX_POST_VAR_SIZE 512  

/**
 * Iterator over key-value pairs where the value
 * maybe made available in increments and/or may
 * not be zero-terminated.  Used for processing
 * POST data.
 *
 * @param cls user-specified closure
 * @param kind type of the value
 * @param key 0-terminated key for the value
 * @param filename name of the uploaded file, NULL if not known
 * @param content_type mime-type of the data, NULL if not known
 * @param transfer_encoding encoding of the data, NULL if not known
 * @param data pointer to size bytes of data at the
 *              specified offset
 * @param off offset of data in the overall value
 * @param size number of bytes in data available
 * @return MHD_YES to continue iterating,
 *         MHD_NO to abort the iteration
 */
static int
post_iterator (void *cls,
         enum MHD_ValueKind kind,
         const char *key,
         const char *filename,
         const char *content_type,
         const char *transfer_encoding,
         const char *data, uint64_t off, size_t size)
{
  struct Request *request = cls;
  struct Session *session = request->session;
  Config_var* cfg_v;
  
  //LOGE("post_interator: key='%s' value='%-.*s'", key, size, data);
  HASH_FIND_STR(config_h,key,cfg_v);  
  
  if (cfg_v)
  {
    if (utstring_len(cfg_v->post_val) + size < MAX_POST_VAR_SIZE)
    {  
      utstring_bincpy(cfg_v->post_val,data,size);
      LOGE("current value for '%s' is '%s'", cfg_v->lookup_name, utstring_body(cfg_v->post_val));
    }  
  }
  
  return MHD_YES;
}


/**
 * Main MHD callback for handling requests.
 *
 *
 * @param cls argument given together with the function
 *        pointer when the handler was registered with MHD
 * @param url the requested url
 * @param method the HTTP method used ("GET", "PUT", etc.)
 * @param version the HTTP version string (i.e. "HTTP/1.1")
 * @param upload_data the data being uploaded (excluding HEADERS,
 *        for a POST that fits into memory and that is encoded
 *        with a supported encoding, the POST data will NOT be
 *        given in upload_data and is instead available as
 *        part of MHD_get_connection_values; very large POST
 *        data *will* be made available incrementally in
 *        upload_data)
 * @param upload_data_size set initially to the size of the
 *        upload_data provided; the method must update this
 *        value to the number of bytes NOT processed;
 * @param con_cls pointer that the callback can set to some
 *        address and that will be preserved by MHD for future
 *        calls for this request; since the access handler may
 *        be called many times (i.e., for a PUT/POST operation
 *        with plenty of upload data) this allows the application
 *        to easily associate some request-specific state.
 *        If necessary, this state can be cleaned up in the
 *        global "MHD_RequestCompleted" callback (which
 *        can be set with the MHD_OPTION_NOTIFY_COMPLETED).
 *        Initially, <tt>*con_cls</tt> will be NULL.
 * @return MHS_YES if the connection was handled successfully,
 *         MHS_NO if the socket must be closed due to a serios
 *         error while handling the request
 */
static int
create_response (void *cls,
     struct MHD_Connection *connection,
     const char *url,
     const char *method,
     const char *version,
     const char *upload_data, 
     size_t *upload_data_size,
     void **ptr)
{
  struct MHD_Response *response;
  struct Request *request;
  struct Session *session;
  int ret;
  unsigned int i;
  struct Page page = {"/","text/html", &handle_page, 0};
  
  request = *ptr;
  LOGE("In create_response, request=%p", *ptr);
  
  if (!request)
  {
    request = calloc (1, sizeof (struct Request));
    if (!request)
    {
      LOGE("calloc error: %s\n", strerror (errno));
      return MHD_NO;
    }
    *ptr = request;
    if (0 == strcmp (method, MHD_HTTP_METHOD_POST))
    {
      request->pp = MHD_create_post_processor (connection, 1024,
                 &post_iterator, request);
      if (NULL == request->pp)
      {
        LOGE("Failed to setup post processor for `%s'\n",
           url);
        return MHD_NO; /* internal error */
      }
    }
    init_post_config();
    
    if (request->session)
      request->session->msg = 0;
    
    return MHD_YES;
  }
  
  if (!request->session)
  {
    if (!(request->session = get_session (connection)))
    {
      LOGE("Failed to setup session for `%s'\n",
         url);
      return MHD_NO; /* internal error */
    }
  }
  
  session = request->session;
  session->start = time (NULL);
  session->msg = 0;
  
  if (0 == strcmp (method, MHD_HTTP_METHOD_POST))
    {      
      /* evaluate POST data */
      MHD_post_process (request->pp, upload_data,*upload_data_size);
      
      if (*upload_data_size)
      {
        *upload_data_size = 0;
        return MHD_YES;
      }
      
      /* done with POST data, serve response */
      MHD_destroy_post_processor (request->pp);
      session->msg = "Configuration data updated";
      end_post_config();
      request->pp = NULL;
      method = MHD_HTTP_METHOD_GET; /* fake 'GET' */
      if (NULL != request->post_url)
        url = request->post_url;
    }

  if ( (0 == strcmp (method, MHD_HTTP_METHOD_GET)) ||
       (0 == strcmp (method, MHD_HTTP_METHOD_HEAD)) )
    {
      LOGE("Processing URL %s", url);
      ret = (*page.handler)(page.handler_cls, 
            page.mime,
            session, connection);
      if (ret != MHD_YES)
       LOGE("Failed to create page for `%s'\n", url);
      return ret;
    }
  /* unsupported HTTP method */
  response = MHD_create_response_from_buffer (strlen (METHOD_ERROR),
                (void *) METHOD_ERROR,
                MHD_RESPMEM_PERSISTENT);
  ret = MHD_queue_response (connection, 
          MHD_HTTP_METHOD_NOT_ACCEPTABLE, 
          response);
  MHD_destroy_response (response);
  return ret;
}


/**
 * Callback called upon completion of a request.
 * Decrements session reference counter.
 *
 * @param cls not used
 * @param connection connection that completed
 * @param con_cls session handle
 * @param toe status code
 */
static void
request_completed_callback (void *cls,
          struct MHD_Connection *connection,
          void **con_cls,
          enum MHD_RequestTerminationCode toe)
{
  struct Request *request;
  
  if (!con_cls)
    return;
  
  request = (struct Request*)*con_cls;

  if (NULL == request)
    return;
  if (NULL != request->session)
    request->session->rc--;
  if (NULL != request->pp)
    MHD_destroy_post_processor (request->pp);
  free (request);
}

static void expire_sessions ()
{
  struct Session *pos;
  struct Session *prev;
  struct Session *next;
  time_t now;

  now = time (NULL);
  prev = NULL;
  pos = sessions;
  while (NULL != pos)
    {
      next = pos->next;
      if (now - pos->start > 60 * 60)
      {
        /* expire sessions after 1h */
        if (NULL == prev)
          sessions = pos->next;
        else
          prev->next = next;
        free (pos);
      }
      else
        prev = pos;
      pos = next;
    }      
}

void http_stop_daemon()
{
  exit_requested = 1;
}

int http_run_daemon(JNIEnv* env,jobject* cfg_obj)
{
  struct timeval tv;
  struct timeval *tvp;
  fd_set rs;
  fd_set ws;
  fd_set es;
  int max;
  int res = 0;
  struct MHD_Daemon *httpd = 0;

  MHD_UNSIGNED_LONG_LONG mhd_timeout;
  
  if (httpd_running)
    return 1;
  
  exit_requested = 0;
  
  srandom ((unsigned int) time (NULL));
  if (!(httpd = MHD_start_daemon (MHD_USE_DEBUG,
                        8000,
                        NULL, NULL, 
      &create_response, NULL, 
      MHD_OPTION_CONNECTION_TIMEOUT, (unsigned int) 15,
      MHD_OPTION_NOTIFY_COMPLETED, &request_completed_callback, NULL,
      MHD_OPTION_END)))
  {  
    res = 1;
    goto err;
  }
  
  LOGE("Running config daemon");
  jni_env = env;
  jni_cfg = cfg_obj;
  httpd_running = 1;
  
  while (1)
  {
    expire_sessions();
    max = 0;
    FD_ZERO (&rs);
    FD_ZERO (&ws);
    FD_ZERO (&es);
    if (MHD_YES != MHD_get_fdset (httpd, &rs, &ws, &es, &max))
      break; /* fatal internal error */
    if (MHD_get_timeout (httpd, &mhd_timeout) == MHD_YES) 
    {
      tv.tv_sec = mhd_timeout / 1000;
      tv.tv_usec = (mhd_timeout - (tv.tv_sec * 1000)) * 1000;
      tvp = &tv;    
    }
    else
      tvp = NULL;
    select (max + 1, &rs, &ws, &es, tvp);
    if (exit_requested)
      break;
    
    if (!httpd)
    {
      LOGE("BUG: httpd==0");
      res = 1;
      goto err;
    }
    
    MHD_run (httpd);
  }
  MHD_stop_daemon (httpd);
err:  
  jni_env = 0;
  jni_cfg = 0;
  httpd_running = 0;
  return res;
}

int http_daemon_running()
{
  return httpd_running;
}

static int init_post_config()
{
  Config_var *v;
  
  for (v = config_vars; v->name; v++)
  {
     utstring_new(v->post_val);    
  }
  
  return 0;
  
}

static int end_post_config()
{
  Config_var *v;
  int res = 0;
  
  for (v = config_vars; v->name; v++)
  {
     const char* val = utstring_body(v->post_val);    
     LOGE("Setting '%s' to '%s'", v->lookup_name, val);
     
     if ((*v->reader)(jni_env,jni_cfg,v,val))
       res = 1;
     
     utstring_free(v->post_val);
     v->post_val = 0;
  }
  
  if (!res)
  {  
    if (!write_config(jni_env,jni_cfg,"default"))
      res = 1;
  }  
  
  return res;
}
