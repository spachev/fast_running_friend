#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <microhttpd.h>
#include <internal.h>
#include <ctype.h>

#include <jni.h>
#include "uthash.h"
#include "utstring.h"
#include "config_vars.h"
#include "log.h"
#include "http_daemon.h"
#include "timer.h"
#include "c_html.h"
#include "frb.h"

#define METHOD_ERROR "<html><head></head><body>Unsupported method</body>"

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

typedef enum {POST_UNDEF,POST_CONFIG,POST_WORKOUT} Post_type;

struct Request
{

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
  Post_type post_type;
  Run_timer post_timer;
};


static int volatile exit_requested = 0;
static int volatile httpd_running = 0;
static Run_timer review_timer;
static int review_timer_inited = 0;
// TODO: fix thread safety
static struct MHD_Daemon *httpd = 0;

static UT_string* get_config_form(const char* msg, const char* url);
static UT_string* get_review_list(const char* msg, const char* url);
static UT_string* get_workout_review(const char* msg, const char* url);

static void print_html_escaped(UT_string* res, const char* s);

static int
post_iterator_config(void *cls,
         enum MHD_ValueKind kind,
         const char *key,
         const char *filename,
         const char *content_type,
         const char *transfer_encoding,
         const char *data, uint64_t off, size_t size);

static int
post_iterator_workout(void *cls,
         enum MHD_ValueKind kind,
         const char *key,
         const char *filename,
         const char *content_type,
         const char *transfer_encoding,
         const char *data, uint64_t off, size_t size);

static int
post_iterator(void *cls,
         enum MHD_ValueKind kind,
         const char *key,
         const char *filename,
         const char *content_type,
         const char *transfer_encoding,
         const char *data, uint64_t off, size_t size);


static int init_post_config();
static int finalize_post_config();
static int finalize_post_workout(struct Request* r);
static int ensure_review_timer_inited();

#define WORKOUT_URL "/workout"
#define WORKOUT_URL_LEN strlen(WORKOUT_URL)


/**
 * Name of our cookie.
 */
#define COOKIE_NAME "session"

static struct Session *sessions;

#define CONFIG_PAGE_TITLE "FastRunningFriend Configuration"
#define REVIEW_PAGE_TITLE "Workout Review"
#define WORKOUT_PAGE_TITLE "Workout Details"

#define CONFIG_URL "/config"
#define CONFIG_URL_LEN strlen(CONFIG_URL)

typedef enum {NAV_UNDEF,NAV_CONFIG,NAV_REVIEW} Nav_ident;

typedef struct 
{
  const char* title;
  const char* url;
  Nav_ident type;
} Nav_item;

Nav_item nav_arr[] = {
  {"Configuration","config",NAV_CONFIG},
  {"Workout Review","review",NAV_REVIEW},
  {0,0,NAV_UNDEF}
};

static void add_nav_menu(UT_string* res, Nav_ident type);
static void print_workout_form(UT_string* res, Run_timer* t);
static void get_prev_and_next(const char* workout, const char** prev, const char** next);


static void add_nav_menu(UT_string* res, Nav_ident type)
{
  Nav_item *n = nav_arr;
  utstring_printf(res,"<table><tr>");
  
  for (; n->title; n++)
  {
    if (n->type != type)
      utstring_printf(res,"<td><a href=\"/%s\">%s</a></td>", n->url, n->title);
    else
      utstring_printf(res,"<td>%s</td>", n->title);
  }

  utstring_printf(res,"</tr></table>");
}

static void print_workout_date(UT_string* res, const char* t)
{
  utstring_printf(res,"%.4s-%.2s-%.2s %.2s:%.2s:%.2s", t,t+5,t+8,t+11,t+14,t+17);
}

static void print_html_run_segment(UT_string* res, uint leg_num, uint split_num, ulonglong t, double d,
                                   const char* template_html, const char* comment)
{
  int have_comment = (comment && *comment);
  char* comment_prompt = have_comment ? "Edit Comment" : "Add Comment";

  if (split_num)
  {
    utstring_printf(res,"<tr><td>Split %d</td><td>Distance:</td><td><input name='d_%d_%d' type='text' size=6"
    " value='%.3f' onChange=\"update_leg(%d)\">"
    "</td><td>Time:</td><td><input name='t_%d_%d' size=9 onChange=\"update_leg(%d)\" value='",
                    split_num,leg_num,split_num,d,leg_num,leg_num,split_num,leg_num);

    run_timer_print_time(res,t);
    utstring_printf(res,"'>");

    if (template_html)
    {
      utstring_printf(res,"</td><td>Zone</td><td><select name='z_%d_%d'>%s</select></td>"
                          "<td><span id='sc_%d_%d' class='comment' onclick='open_comment(%d,%d)'>"
                          "%s</span><div style='display:none' id='c_%d_%d'>"
                          "<textarea name='c_%d_%d' rows='5' cols='30'>",
                      leg_num,split_num,template_html,
                      leg_num, split_num, leg_num, split_num, comment_prompt, 
                      leg_num, split_num, leg_num, split_num);

    }
  }
  else
  {
    utstring_printf(res,"<tr><td>Leg %d</td><td>Distance:</td><td><span id='d_%d'> "
    "%.3f</span></td><td>Time:</td><td><span id='t_%d'>", leg_num,leg_num,d,leg_num);
    run_timer_print_time(res,t);
    utstring_printf(res,"</span></td><td colspan=3>"
                        "<span id='sc_%d_%d' class='comment' onclick='open_comment(%d,%d)'>"
                        "%s</span><div style='display:none' id='c_%d_%d'>"
                        "<textarea name='c_%d_%d' rows='5' cols='30'>", leg_num, split_num,
                        leg_num, split_num, comment_prompt, leg_num, split_num, leg_num, split_num);
  }

  if (have_comment)
    print_html_escaped(res,comment);

  utstring_printf(res,"</textarea><span class='comment' onclick='close_comment(%d,%d)'>"
                      "Close</span></div>",
                  leg_num, split_num);

  utstring_printf(res,"</td></tr>");
}

static int init_post_config()
{
   Config_var *v;

   for (v = config_vars; v->name; v++)
     utstring_new(v->post_val);

   return 0;
}


static void print_form_js(UT_string* res)
{
  utstring_printf(res,FORM_JS_Q_F);
}

static void print_zone_js(UT_string* res, uint zone, uint leg_num, uint split_num)
{
  utstring_printf(res,"set_zone(%d,%d,%d);\n",leg_num,split_num,zone);
}

static void print_workout_form(UT_string* res, Run_timer* t)
{
  Run_leg* cur_leg;
  Run_split* cur_split;
  uint leg_num = 1, split_num = 1;
  double d_tmp;
  ulonglong t_tmp;
  UT_string *ut_template_html = 0,*zone_js;
  const char* template_html = 0;
  const char* comment = t->comment;
  const char* comment_prompt;
  int comment_present;

  utstring_new(zone_js);

  if ((ut_template_html = frb_template_html()))
  {
    template_html = utstring_body(ut_template_html);
  }

  comment_present = (comment && *comment);
  comment_prompt =  comment_present ? "Edit Workout Comment" : "Add Workout Comment";
  print_form_js(res);
  utstring_printf(res,"<form method='POST' id='theform'>"
   "<span id='sc_0_0' class='comment' onclick='open_comment(0,0)'>%s</span><div id='c_0_0' style='display:none'>"
   "<textarea name='c_0_0' rows='5' cols='40'>", comment_prompt
  );

  if (comment_present)
    print_html_escaped(res,comment);

  utstring_printf(res,"</textarea><span class='comment' onclick='close_comment(0,0)'>"
  "Close</span></div></br>\n<table>\n");

  LL_FOREACH(t->first_leg,cur_leg)
  {
    if (!cur_leg->next)
      continue;

    print_html_run_segment(res,leg_num,0,
                                  cur_leg->next->first_split->t - cur_leg->first_split->t,
                                  cur_leg->next->first_split->d - cur_leg->first_split->d,
                                  template_html,cur_leg->comment);

    split_num = 1;

    LL_FOREACH(cur_leg->first_split,cur_split)
    {
      print_zone_js(zone_js,cur_split->zone,leg_num,split_num);

      if (cur_split->next)
      {
        t_tmp = cur_split->next->t;
        d_tmp = cur_split->next->d;
      }
      else
      {
        t_tmp = cur_leg->next->first_split->t;
        d_tmp = cur_leg->next->first_split->d;
      }

      print_html_run_segment(res,leg_num,split_num,t_tmp - cur_split->t,d_tmp - cur_split->d,template_html,
                             cur_split->comment);
      split_num++;
    }

    leg_num++;
  }

  utstring_printf(res,"<tr><td colspan='100%%'><input type='submit' value='Update'></td></tr></table></form>"
  "\n<script>");
  utstring_concat(res,zone_js);
  utstring_printf(res,"</script>");
  utstring_free(zone_js);

  if (ut_template_html)
    utstring_free(ut_template_html);
}

static void print_workout_nav(UT_string* res, const char* workout)
{
  const char* prev, *next;

  get_prev_and_next(workout, &prev, &next);

  utstring_printf(res, "<table><tr>");

  if (prev)
    utstring_printf(res, "<td><a href=\"%s\">Prev</a></td>", prev);

  if (next)
    utstring_printf(res, "<td><a href=\"%s\">Next</a></td>", next);

  utstring_printf(res, "</div>");
}

static UT_string* get_workout_review(const char* msg, const char* url)
{
  UT_string* res = 0;
  Run_timer w_timer;
  const char* t;

  utstring_new(res);
  utstring_printf(res, "<html><head><title>" WORKOUT_PAGE_TITLE 
    "</title></head><body><h1>" WORKOUT_PAGE_TITLE "</h1>");
  add_nav_menu(res, NAV_UNDEF);

  if (!(t = strchr(url+1,'/')))
  {
    utstring_printf(res,"Missing workout date<br>");
    goto err;
  }
  t++;
  utstring_printf(res,"<h2>Workout details for ");
  print_workout_date(res,t);
  utstring_printf(res,"</h2>\n");
  
  if (msg && *msg)
  {
    utstring_printf(res,"%s<br>", msg);
  }

  if (run_timer_init_from_workout(&w_timer,DATA_DIR,t,0))
  {
    utstring_printf(res,"Error fetching workout details for %s", t);
    goto err;
  }

  print_workout_nav(res, t);
  print_workout_form(res,&w_timer);
  utstring_printf(res,"</body></html>");
  run_timer_deinit(&w_timer);
err:
  return res;
}

static void get_prev_and_next(const char* workout, const char** prev, const char** next)
{
  char** rl;
  char** p, **p_end;
  uint num_entries;

  *prev = *next = 0;

  if (ensure_review_timer_inited())
  {
    LOGE("Error initializing review timer object");
    return;
  }

  // TODO: do not rely on the file system sort, make sure the list is sorted
  if (!(rl = run_timer_run_list(&review_timer,&review_timer.mem_pool,&num_entries)))
    return;

  p_end = rl + num_entries;

  //LOGE("Found %d entries", num_entries);

  for (p = rl; p < p_end ; p++)
  {
    //LOGE("Comparing %s against %s", *p, workout);
    if (strcmp(*p, workout) == 0)
    {
      if (p > rl)
        *prev = p[-1];

      if (p + 1 < p_end)
        *next = p[1];

      break;
    }
  }
}

static UT_string* get_review_list(const char* msg, const char* url)
{
  UT_string* res = 0;
  Mem_pool pool;
  char** rl, **rl_p;
  uint num_entries;

  int mem_pool_inited = 0;
  // aborts on malloc() failure, which is OK - if things are so bad that malloc fails it is
  // OK to exit for now. TODO: fix so there is clean exit on malloc() failure
  utstring_new(res);
  utstring_printf(res, "<html><head><title>" REVIEW_PAGE_TITLE 
    "</title></head><body><h1>" REVIEW_PAGE_TITLE "</h1>");
  add_nav_menu(res, NAV_REVIEW);

  if (ensure_review_timer_inited())
  {
    utstring_printf(res,"Error initializing review timer object");
    goto err;
  }

  if (mem_pool_init(&pool,RUN_TIMER_MEM_POOL_BLOCK))
  {
    utstring_printf(res,"Error initializing review timer memory pool");
    goto err;
  }

  mem_pool_inited = 1;

  if (!(rl = run_timer_run_list(&review_timer,&pool,&num_entries)))
    goto err;

  utstring_printf(res,"<table>");

  for (rl_p = rl;*rl_p;rl_p++)
  {
    utstring_printf(res,"<tr><td><a href='/workout/%s'>%s</a></td></tr>", *rl_p, *rl_p);
  }

  utstring_printf(res,"</table>");

err:
  utstring_printf(res,"</body></html>");

  if (mem_pool_inited)
  {
    mem_pool_free(&pool);
    mem_pool_inited = 0;
  }

  return res;
}

static void print_html_escaped(UT_string* res, const char* s)
{
  for (;*s;s++)
  {
    switch (*s)
    {
      case '<':
        utstring_bincpy(res,"&lt;",4);
        break;
      case '>':
        utstring_bincpy(res,"&gt;",4);
        break;
      case '"':
        utstring_bincpy(res,"&quot;",6);
        break;
      case '\'':
        utstring_bincpy(res,"&apos;",6);
        break;
      case '&':
        utstring_bincpy(res,"&amp;",5);
        break;
      default:
        utstring_bincpy(res,s,1);
        break;
    }
  }
}

static UT_string* get_config_form(const char* msg, const char* url)
{
  UT_string* res;
  Config_var* cfg_var_p;
  
  // aborts on malloc() failure, which is OK - if things are so bad that malloc fails it is
  // OK to exit for now. TODO: fix so there is clean exit on malloc() failure
  utstring_new(res);
  utstring_printf(res, "<html><head><title>" CONFIG_PAGE_TITLE 
    "</title></head><body><h1>" CONFIG_PAGE_TITLE "</h1>");

  add_nav_menu(res, NAV_CONFIG);

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
    const char* input_type = cfg_var_p->is_pw ? "password":"text";

    if ((*cfg_var_p->printer)(jni_env,jni_cfg,cfg_var_p,buf,sizeof(buf)))
      goto err;

    utstring_printf(res,"<tr><td>%s</td><td>"
       "<input name=\"%s\"type='%s' size=40 value=\"",
       cfg_var_p->lookup_name,cfg_var_p->lookup_name,input_type);
   print_html_escaped(res,buf);
   utstring_printf(res,"\"></td></tr>\n");
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

static int ensure_review_timer_inited()
{
  if (review_timer_inited)
    return 0;

  if (run_timer_init(&review_timer,DATA_DIR))
    return 1;

  review_timer_inited = 1;
  return 0;
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
  const char* url = (const char*)cls;
  UT_string* (*cb)(const char*,const char*);
  cb = get_config_form;

  if (strcmp(url,"/review") == 0)
  {
    cb = get_review_list;
  }
  else if(strncmp(url,WORKOUT_URL,WORKOUT_URL_LEN) == 0)
  {
    cb = get_workout_review;
  }
  
  if (!(reply_s = (*cb)(session->msg,url)))
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
post_iterator_config (void *cls,
         enum MHD_ValueKind kind,
         const char *key,
         const char *filename,
         const char *content_type,
         const char *transfer_encoding,
         const char *data, uint64_t off, size_t size)
{
  Config_var* cfg_v;

  //LOGE("post_interator_config: key='%s' value='%-.*s'", key, size, data);
  HASH_FIND_STR(config_h,key,cfg_v);

  if (cfg_v && utstring_len(cfg_v->post_val) + size < MAX_POST_VAR_SIZE)
  {
    utstring_bincpy(cfg_v->post_val,data,size);
    //LOGE("current value for '%s' is '%s'", cfg_v->lookup_name, utstring_body(cfg_v->post_val));
  }

  return MHD_YES;
}

static int
post_iterator_workout(void *cls,
         enum MHD_ValueKind kind,
         const char *key,
         const char *filename,
         const char *content_type,
         const char *transfer_encoding,
         const char *data, uint64_t off, size_t size)
{
  struct Request* r = (struct Request*)cls;

  if (!size)
    return MHD_YES;

  LOGE("post_interator_workout: key='%s' value='%-.*s'", key, size, data);

  if (run_timer_add_key_to_hash(&r->post_timer,key,data,size))
  {
    LOGE("Error adding key to hash");
    return MHD_NO;
  }

  return MHD_YES;
}

static int
post_iterator(void *cls,
         enum MHD_ValueKind kind,
         const char *key,
         const char *filename,
         const char *content_type,
         const char *transfer_encoding,
         const char *data, uint64_t off, size_t size)
{
  struct Request* request = (struct Request*)cls;

  switch (request->post_type)
  {
    case POST_WORKOUT:
      //LOGE("post_workout");
      return post_iterator_workout(cls,kind,key,filename,content_type,transfer_encoding,data,off,size);
    case POST_CONFIG:
      //LOGE("post_config");
      return post_iterator_config(cls,kind,key,filename,content_type,transfer_encoding,data,off,size);
    default:
      LOGE("post_impossible");
      break;
  }

  return MHD_NO;
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
  int timer_inited = 0;
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
      //LOGE("url=%s",url);

      if (memcmp(url,CONFIG_URL,CONFIG_URL_LEN) == 0 || (*url == '/' && !url[1]))
        request->post_type = POST_CONFIG;
      else if (memcmp(url,WORKOUT_URL,WORKOUT_URL_LEN) == 0)
      {
        const char* t = strchr(url+1,'/');

        if (!t)
        {
          LOGE("Invalid workout url %s", url);
          return MHD_NO;
        }
        if (run_timer_init_from_workout(&request->post_timer,DATA_DIR,t+1,1))
        {
          LOGE("Error initializing timer from workout");
          return MHD_NO;
        }
        if (run_timer_init_split_arr(&request->post_timer))
        {
          LOGE("Error initializing timer split lookup array");
          run_timer_deinit(&request->post_timer);
          return MHD_NO;
        }

        timer_inited = 1;
        request->post_type = POST_WORKOUT;
      }
      else
        request->post_type = POST_UNDEF;

      if (!(request->pp = MHD_create_post_processor (connection, 1024,
                 &post_iterator, request)))
      {
        LOGE("Failed to setup post processor for `%s'\n",
           url);

        if (timer_inited)
          run_timer_deinit(&request->post_timer);

        return MHD_NO; /* internal error */
      }
    }

    if (request->post_type == POST_CONFIG)
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

    switch (request->post_type)
    {
      case POST_CONFIG:
        finalize_post_config(request);
        session->msg = "Configuration data updated";
        break;
      case POST_WORKOUT:
        finalize_post_workout(request);
        session->msg = "Workout updated";
        run_timer_deinit(&request->post_timer);
        break;
      default:
        break;
    }
    request->pp = NULL;
    method = MHD_HTTP_METHOD_GET; /* fake 'GET' */
    if (NULL != request->post_url)
      url = request->post_url;
  }

  if ( (0 == strcmp (method, MHD_HTTP_METHOD_GET)) ||
       (0 == strcmp (method, MHD_HTTP_METHOD_HEAD)) )
  {
    LOGE("Processing URL %s", url);
    ret = (*page.handler)(url,
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

  if (httpd)
  {
    httpd->shutdown = MHD_YES;
    LOGE("requesting shutdown");
  }
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

  MHD_UNSIGNED_LONG_LONG mhd_timeout;
  
  if (httpd_running)
  {
    LOGE("Will not start daemon in http_run_daemon(), httpd_running = %d", httpd_running);
    return 1;
  }

  exit_requested = 0;
  
  srandom ((unsigned int) time (NULL));
  if (!(httpd = MHD_start_daemon (MHD_USE_DEBUG,
                        8000,
                        NULL, NULL, 
      &create_response, NULL, 
      MHD_OPTION_CONNECTION_TIMEOUT, (unsigned int) 3,
      MHD_OPTION_NOTIFY_COMPLETED, &request_completed_callback, NULL,
      MHD_OPTION_END)))
  {
    LOGE("Error starting config daemon");
    res = 1;
    goto err;
  }

  LOGE("Running config daemon");
  cfg_jni_init(env,cfg_obj);
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
    {
      /* do not go into infinite wait */
      tv.tv_sec = 1;
      tv.tv_usec = 0;
      tvp = &tv;
    }

    if (select (max + 1, &rs, &ws, &es, tvp) == 0)
    {
      if (!exit_requested)
        continue;
    }

    if (exit_requested)
    {
      exit_requested = 0;
      break;
    }

    if (!httpd)
    {
      LOGE("BUG: httpd==0");
      res = 1;
      goto err;
    }

    MHD_run(httpd);
    if (exit_requested)
    {
      exit_requested = 0;
      break;
    }
  }
  MHD_stop_daemon(httpd);
err:
  cfg_jni_reset();
  LOGE("Exiting config daemon");
  httpd_running = 0;
  httpd = 0;
  return res;
}

int http_daemon_running()
{
  return httpd_running;
}

static int finalize_post_workout(struct Request* r)
{
  run_timer_parse_keys(&r->post_timer);

  if (run_timer_save(&r->post_timer))
  {
    r->session->msg = "Error saving workout";
    return 1;
  }

  if (frb_post_workout(&r->post_timer))
  {
    r->session->msg = "Error posting workout to Fast Running Blog";
    return 1;
  }

  return 0;
}

static int finalize_post_config()
{
  Config_var *v;
  int res = 0;

  for (v = config_vars; v->name; v++)
  {
     const char* val = utstring_body(v->post_val);
     //LOGE("Setting '%s' to '%s'", v->lookup_name, val);

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

  frb_update_template();
  return res;
}

