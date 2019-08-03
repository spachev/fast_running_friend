#ifndef FRB_H
#define FRB_H

#define FRB_TEMPLATE_FNAME "frb.template"
#define FRB_TEMPLATE_URL "http://www.fastrunningblog.com/frf.php"
#define FRB_OK "AUTH OK\n"
#define FRB_OK_LEN strlen(FRB_OK)
#define FRB_POST_OK "POST OK\n"
#define FRB_POST_URL FRB_TEMPLATE_URL

#include "utstring.h"
#include "timer.h"

int frb_update_template();
UT_string* frb_template_html();
int frb_post_workout(Run_timer* t);

#endif
