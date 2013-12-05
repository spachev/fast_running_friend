#include "config_vars.h"
#include "utstring.h"
#include "log.h"
#include <jni.h>

JNIEnv* jni_env = 0;
jobject* jni_cfg = 0;

void cfg_jni_init(JNIEnv* env, jobject* obj)
{
  jni_env = env;
  jni_cfg = obj;
}

void cfg_jni_reset()
{
  jni_env = 0;
  jni_cfg = 0;
}

int get_config_var_str(const char* var_name, char* buf, size_t buf_size)
{
  UT_string* res = 0;
  Config_var* cfg_v, *frb_pw_v;
  HASH_FIND_STR(config_h,var_name,cfg_v);

  if (!cfg_v)
  {
    LOGE("Did not find '%s' variable object",var_name);
    return 1;
  }

  if (!jni_env || !jni_cfg)
  {
    LOGE("JNI environment not intialized while fetching '%s'", var_name);
    return 1;
  }

  if ((*cfg_v->printer)(jni_env,jni_cfg,cfg_v,buf,buf_size))
  {
    LOGE("Error fetching value of variable '%s'", var_name);
    return 1;
  }

  return 0;
}