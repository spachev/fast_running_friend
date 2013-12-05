#ifndef FRB_H
#define FBR_H

#define FRB_TEMPLATE_FNAME "frb.template"
#define FRB_TEMPLATE_URL "http://www.fastrunningblog.com/frf.php"
#define FRB_OK "AUTH OK\n"
#define FRB_OK_LEN strlen(FRB_OK)

#include "utstring.h"

int frb_update_template();
UT_string* frb_template_html();

#endif