#ifndef CONFIG_VARS_H
#define CONFIG_VARS_H

struct st_config_var;
typedef struct st_config_var Config_var;

typedef int (*Config_var_reader)(JNIEnv*,void*,Config_var*,const char*);
typedef int (*Config_var_printer)(JNIEnv*,void*,Config_var*,char*,size_t);
typedef enum {VAR_TYPE_NORMAL, VAR_TYPE_ANGLE, VAR_TYPE_PACE} Config_var_type;

struct st_config_var
{
  const char* name;
  const char* lookup_name;
  const char* java_type;
  Config_var_printer printer;
  Config_var_reader reader;

  // these fields are initialized in init_config_vars()
  jfieldID var_id;
  UT_string* post_val;
  uint name_len,lookup_name_len;
  int is_pw;
  UT_hash_handle hh;
} ;

extern Config_var config_vars[];
extern Config_var* config_h;
jboolean write_config(JNIEnv* env, jobject this_obj, const char* profile_name_s);

#endif
