#include "frb.h"
#include "log.h"
#include "http_daemon.h"
#include "url.h"
#include "config_vars.h"
#include "utstring.h"
#include <stdio.h>
#include <stdarg.h>


#define MAX_TEMPLATE_BUF 4096
#define MAX_POST_RESP_BUF 1024

#define INIT_FRB_AUTH   if (get_config_var_str("frb_login", frb_login, sizeof(frb_login)) || \
get_config_var_str("frb_pw", frb_pw, sizeof(frb_pw))) \
{\
  LOGE("Error fetching FRB auth config info");\
  return 1;\
}\
if (!*frb_login) \
{\
  LOGE("FRB auth not configured, not fetching template");\
  return 1;\
}


int frb_update_template()
{
  char frb_login[128],frb_pw[128];
  UT_string* url = 0;
  char* template_buf;
  int res = 1;
  FILE* fp;
  size_t data_size;

  INIT_FRB_AUTH

  if (!(template_buf = (char*)malloc(MAX_TEMPLATE_BUF)))
  {
    LOGE("OOM allocating template buffer");
    return 1;
  }

  if (!(data_size=url_fetch(FRB_TEMPLATE_URL,template_buf,MAX_TEMPLATE_BUF,
       "submit=Login&action=fetch_fields&frf_mode=1&username=?&pass=?",
       frb_login, frb_pw)))
  {
    LOGE("Error fetching template from FRB server");
    goto err;
  }

  if (data_size < FRB_OK_LEN)
  {
    LOGE("FRB response too short");
    goto err;
  }

  if (memcmp(template_buf,FRB_OK,FRB_OK_LEN))
  {
    LOGE("FRB did not authenticate successfully");
    goto err;
  }

  if (!(fp = fopen(DATA_DIR FRB_TEMPLATE_FNAME,"w")))
  {
    LOGE("Error writing to template file");
    goto err;
  }

  if (fwrite(template_buf + FRB_OK_LEN,data_size - FRB_OK_LEN,1,fp) != 1)
  {
    fclose(fp);
    LOGE("Error writing to template file");
    goto err;
  }

  if (fclose(fp))
  {
    LOGE("Error closing template file");
    goto err;
  }

  res = 0;

err:

  free(template_buf);
  return res;
}

typedef enum  {FMT_JSON, FMT_HTML} Fmt_type;

static UT_string* frb_template(Fmt_type fmt)
{
  FILE* fp;
  char buf[128];
  UT_string* res = 0;
  int tried_fetch = 0;

retry:

  if (!(fp = fopen(DATA_DIR FRB_TEMPLATE_FNAME,"r")))
  {
    if (!tried_fetch)
    {
      if (frb_update_template() == 0)
      {
        tried_fetch = 1;
        goto retry;
      }
    }
    LOGE("Could not open template file for reading");
    return 0;
  }

  utstring_new(res);

  int is_first = 1;

  if (fmt == FMT_JSON)
    utstring_printf(res, "{");

  while (fgets(buf,sizeof(buf),fp))
  {
    char* p = strchr(buf,',');
    const char* selected = "";
    const char* maybe_comma = (is_first) ? "" : ",";

    if (!p)
      continue;

    char* end = p + strlen(p) - 1;
    *end = 0;

    switch (fmt)
    {
      case FMT_HTML:
        if (strstr(p+1,"Easy"))
          selected = " selected";

        utstring_printf(res, "<option value=\"%-.*s\"%s>%s</option>\n",p - buf, buf, selected, p+1);
        break;

      case FMT_JSON:
        utstring_printf(res, "%s\"%-.*s\":\"", maybe_comma, p - buf, buf);
        print_js_escaped(res, p+1);
        utstring_printf(res, "\"");
        break;
    }

    is_first = 0;
  }

  if (fmt == FMT_JSON)
    utstring_printf(res, "}");

  fclose(fp);
  return res;
}

UT_string* frb_template_html()
{
  return frb_template(FMT_HTML);
}

UT_string* frb_template_json()
{
  return frb_template(FMT_JSON);
}


int frb_post_workout(Run_timer* t)
{
  char frb_login[128],frb_pw[128];
  char* resp_buf = 0;
  size_t resp_size;
  int res = 1;

  INIT_FRB_AUTH

  if (run_timer_add_key_to_hash(t, "username", frb_login, strlen(frb_login)) ||
      run_timer_add_key_to_hash(t, "pass", frb_pw, strlen(frb_pw)) ||
      run_timer_add_key_to_hash(t, "frf_mode", "1", 1) ||
      run_timer_add_key_to_hash(t, "action", "post_workout", 12) ||
      run_timer_add_key_to_hash(t, "workout_ts", t->workout_ts, t->workout_ts_len))
  {
    LOGE("Error adding Fast Running Blog login credentials to post hash");
    return 1;
  }

  if (!(resp_buf = (char*)malloc(MAX_POST_RESP_BUF)))
  {
    LOGE("OOM allocating post response buffer");
    return 1;
  }

  if (!(resp_size = url_fetch_with_hash(FRB_POST_URL,resp_buf,MAX_POST_RESP_BUF,t->post_h)))
  {
    LOGE("Error posting workout to Fast Running Blog");
    goto err;
  }

  if (!strstr(resp_buf,FRB_OK) || !strstr(resp_buf,FRB_POST_OK))
  {
    LOGE("Error in response to post_workout on Fast Running Blog. Server response: %.*s", resp_size, resp_buf);
    goto err;
  }

  res = 0;

err:
  if (resp_buf)
    free(resp_buf);

  return res;
}
