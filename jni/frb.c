#include "frb.h"
#include "log.h"
#include "http_daemon.h"
#include "url.h"
#include "config_vars.h"
#include "utstring.h"
#include <stdio.h>
#include <stdarg.h>


#define MAX_TEMPLATE_BUF 4096

int frb_update_template()
{
  char frb_login[128],frb_pw[128];
  UT_string* url = 0;
  char* template_buf;
  int res = 1;
  FILE* fp;
  size_t data_size;

  if (get_config_var_str("frb_login", frb_login, sizeof(frb_login)) ||
      get_config_var_str("frb_pw", frb_pw, sizeof(frb_pw)))
  {
    LOGE("Error fetching FRB auth config info");
    return 1;
  }

  if (!*frb_login)
  {
    LOGE("FRB auth not configured, not fetching template");
    return 1;
  }

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

UT_string* frb_template_html()
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

  while (fgets(buf,sizeof(buf),fp))
  {
    char* p = strchr(buf,',');

    if (!p)
      continue;

    utstring_printf(res, "<option value=\"%-.*s\">%s</option>\n",p - buf, buf, p+1);
  }

  fclose(fp);
  return res;
}
