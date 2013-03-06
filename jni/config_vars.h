struct st_config_var;
typedef struct st_config_var Config_var;

typedef int (*Config_var_writer)(JNIEnv*,void*,Config_var*,FILE*);
typedef int (*Config_var_reader)(JNIEnv*,void*,Config_var*,const char*);
typedef enum {VAR_TYPE_NORMAL, VAR_TYPE_ANGLE, VAR_TYPE_PACE} Config_var_type;
struct st_config_var
{
  const char* name;
  const char* lookup_name;
  const char* java_type;
  Config_var_writer writer;  
  Config_var_reader reader; 
  jfieldID var_id;
  UT_hash_handle hh;
} ;

extern Config_var config_vars[];
