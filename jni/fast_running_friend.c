#include <unistd.h>
#include <jni.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <fcntl.h>
#include <dirent.h>
#include <asm/ioctl.h>
#include <errno.h>
#include <stdarg.h>
#include <math.h>
#include <ctype.h>
#include <linux/android_alarm.h>
#include "uthash.h"
#include "utstring.h"
#include "config_vars.h"
#include "http_daemon.h"
#include "log.h"
#include "timer_jni.h"
#include "sirf_gps.h"

static FILE* gps_data_fp = 0, *gps_debug_fp = 0;
static char gps_data_dir[PATH_MAX+1];
static unsigned int gps_data_dir_len = 0;

#define FNAME_EXTRA_SPACE 32

typedef struct 
{
  jclass the_class,coord_class,cfg_class; 
  jfieldID buf_id,cfg_id;
  jfieldID lon_id,lat_id,dist_to_prev_id,speed_id,
    bearing_id,accuracy_id,ts_id;
  jfieldID buf_end_id,flush_ind_id;  
  jfieldID expire_files_days_id;
} GPS_buf_fields;

static GPS_buf_fields gps_buf_fields;
static jclass cfg_class;
static jfieldID data_dir_id;


// needs to be extern

Run_info_fields run_info_fields;

static int init_gps_buf_fields(JNIEnv* env, GPS_buf_fields* fields);
static int remove_expired_files(JNIEnv* env, jobject* this_obj, const char* dir_name);
static int has_ext(const char* fname, const char* ext);
static int get_config_path(JNIEnv* env, jobject* cfg_obj, char* path, int max_path, char* fmt, ...);


#define COORD_ARR_SIG "[Lcom/fastrunningblog/FastRunningFriend/GPSCoord;"
#define COORD_BUF_CLASS "com/fastrunningblog/FastRunningFriend/GPSCoordBuffer"
#define CFG_CLASS "com/fastrunningblog/FastRunningFriend/ConfigState"
#define COORD_CLASS "com/fastrunningblog/FastRunningFriend/GPSCoord"
#define RUN_INFO_CLASS "com/fastrunningblog/FastRunningFriend/RunInfo"



static int read_config_int(JNIEnv* env,void* config_obj,Config_var* var, const char* val);
static int read_config_double(JNIEnv* env,void* config_obj,Config_var* var, const char* val);
static int read_config_angle(JNIEnv* env,void* config_obj,Config_var* var, const char* val);
static int read_config_pace(JNIEnv* env,void* config_obj,Config_var* var, const char* val);
static int read_config_long(JNIEnv* env,void* config_obj,Config_var* var, const char* val);
static int read_config_str(JNIEnv* env,void* config_obj,Config_var* var, const char* val);

static int print_config_int(JNIEnv* env,void* config_obj,Config_var* var, char* buf, size_t buf_size);
static int print_config_double(JNIEnv* env,void* config_obj,Config_var* var, char* buf, 
                               size_t buf_size);
static int print_config_angle(JNIEnv* env,void* config_obj,Config_var* var, char* buf, size_t buf_size);
static int print_config_pace(JNIEnv* env,void* config_obj,Config_var* var, char* buf, size_t buf_size);
static int print_config_long(JNIEnv* env,void* config_obj,Config_var* var, char* buf, size_t buf_size);
static int print_config_str(JNIEnv* env,void* config_obj,Config_var* var, char* buf, size_t buf_size);


static int read_config_double_low(JNIEnv* env, void* config_obj, Config_var* var, 
                                   const char* val_str, Config_var_type type);
static int print_config_double_low(JNIEnv* env,void* config_obj,Config_var* var, char* buf, 
                               size_t buf_size, Config_var_type type);

static int init_config_vars(JNIEnv *env);
static int init_run_info_fields(JNIEnv* env, Run_info_fields* ri_fields);
static int start_config_daemon();



Config_var config_vars[] =
{
  {"min_d_last_trusted", 0, "D", print_config_double, read_config_double},
  {"max_d_last_trusted", 0, "D", print_config_double, read_config_double},
  {"max_pace_diff", 0, "D", print_config_double, read_config_double},
  {"top_pace_t", "top_pace", "D", print_config_pace, read_config_pace},
  {"start_pace_t", "start_pace", "D", print_config_pace, read_config_pace},
  {"expire_files_days", 0, "I",  print_config_int, read_config_int},
  {"max_t_no_signal", 0, "J", print_config_long, read_config_long},
  {"dist_update_interval", 0, "J", print_config_long, read_config_long},
  {"split_display_pause", 0, "J", print_config_long, read_config_long},
  {"gps_update_interval", 0, "J", print_config_long, read_config_long},
  {"min_cos", "max_angle", "D", print_config_angle, read_config_angle},
  {"min_neighbor_cos", "max_neighbor_angle", "D", print_config_angle, read_config_angle},
  {"wifi_ssid", 0, "Ljava/lang/String;", print_config_str, read_config_str},
  {"wifi_key", 0, "Ljava/lang/String;", print_config_str, read_config_str},
  {"frb_login", 0, "Ljava/lang/String;", print_config_str, read_config_str},
  {"frb_pw", 0, "Ljava/lang/String;", print_config_str, read_config_str},
  {"gps_disconnect_interval", 0, "J", print_config_long, read_config_long},
  {"kill_bad_guys", 0, "Ljava/lang/String;", print_config_str, read_config_str},
  {0,0,0}
};   

Config_var* config_h = 0;

jint JNI_OnLoad(JavaVM* vm, void* reserved)
{
  JNIEnv *env;
  
  if ((*vm)->GetEnv(vm, (void**) &env, JNI_VERSION_1_4) != JNI_OK)
  {
     LOGE("JNI Check failure");
     return -1;
  }
  
  if (!env)
  {
    LOGE("JNI env is NULL");
    return -1;
  }
  
  if (init_gps_buf_fields(env,&gps_buf_fields) || init_run_info_fields(env,&run_info_fields))
    return -1;
  
  cfg_class = gps_buf_fields.cfg_class;
  
  if (!(data_dir_id = (*env)->GetFieldID(env,cfg_class,"data_dir","Ljava/lang/String;")))
  {  
    LOGE("Could not find ConfigState.data_dir member");
    return -1;
  }
  
  if (init_config_vars(env))
    return -1;

  return JNI_VERSION_1_4;
}

#define GET_RUN_INFO_FIELD(name,type) if (!(fields->name## _id = (*env)->GetFieldID(env,\
   fields->info_class,#name,type))) \
   {\
     LOGE("Did not find RunInfo.%s member",#name);\
     return -1;\
   }\


static int init_run_info_fields(JNIEnv* env, Run_info_fields* fields)
{
  if (!(fields->info_class = (*env)->FindClass(env,RUN_INFO_CLASS)))
  {  
    LOGE("Did not find RunInfo class");
    return 1;
  }
  
  GET_RUN_INFO_FIELD(t_total,"J");
  GET_RUN_INFO_FIELD(t_leg,"J");
  GET_RUN_INFO_FIELD(t_split,"J");
  GET_RUN_INFO_FIELD(d_last_leg,"D");
  GET_RUN_INFO_FIELD(d_last_split,"D");
  return 0;
}


#define GET_COORD_FIELD(name,type) if (!(fields->name## _id = (*env)->GetFieldID(env,\
   fields->coord_class,#name,type))) \
   {\
     LOGE("Did not find GPSCoord.%s member",#name);\
     return -1;\
   }\

#define GET_CFG_FIELD(name,type) if (!(fields->name## _id = (*env)->GetFieldID(env,\
   fields->cfg_class,#name,type))) \
   {\
     LOGE("Did not find ConfigState.%s member",#name);\
     return -1;\
   }\


#define GET_BUF_FIELD(name,type) if (!(fields->name## _id = (*env)->GetFieldID(env,\
   fields->the_class,#name,type))) \
   {\
     LOGE("Did not find GPSCoordBuffer.%s member",#name);\
     return -1;\
   }\

static int init_config_vars(JNIEnv *env)
{
  Config_var *v;
  
  for (v = config_vars; v->name; v++)
  {
    if (!v->lookup_name)
      v->lookup_name = v->name;

    v->name_len = strlen(v->name);
    v->lookup_name_len = strlen(v->lookup_name);
    v->is_pw = 0;

    if (v->lookup_name_len > 3 && !memcmp(v->lookup_name + v->lookup_name_len - 3,"_pw",3))
    {
      v->is_pw = 1;
    }

    if (!(v->var_id = (*env)->GetFieldID(env,cfg_class,v->name,v->java_type)))
    {
      LOGE("Cannot find ConfigState.%s member", v->name);
      return -1;
    }
    
    HASH_ADD_KEYPTR(hh,config_h,v->lookup_name,strlen(v->lookup_name),v);
  }
  
  return 0;
}

static int init_gps_buf_fields(JNIEnv* env, GPS_buf_fields* fields )
{
  if (!(fields->the_class = (*env)->FindClass(env,COORD_BUF_CLASS)))
  {  
    LOGE("Did not find GPSCoordBuffer class");
    return 1;
  }
  
  GET_BUF_FIELD(buf,COORD_ARR_SIG);
  GET_BUF_FIELD(buf_end,"I");
  GET_BUF_FIELD(flush_ind,"I");
  GET_BUF_FIELD(cfg,"L"CFG_CLASS";");
  
  
  if (!(fields->coord_class = (*env)->FindClass(env,COORD_CLASS)))
  {
    LOGE("Could not find GPSCoord class");
    return 1;
  }
  
  GET_COORD_FIELD(lat,"D");
  GET_COORD_FIELD(lon,"D");
  GET_COORD_FIELD(dist_to_prev,"D");
  
  GET_COORD_FIELD(speed,"F");
  GET_COORD_FIELD(bearing,"F");
  GET_COORD_FIELD(accuracy,"F");
  GET_COORD_FIELD(bearing,"F");
  GET_COORD_FIELD(ts,"J");
  
  if (!(fields->cfg_class = (*env)->FindClass(env,CFG_CLASS)))
  {
    LOGE("Could not find StateConfig class");
    return 1;
  }
  
  GET_CFG_FIELD(expire_files_days,"I");
  return 0;
}

#undef GET_COORD_FIELD
#undef GET_BUF_FIELD
#undef GET_CFG_FIELD

#define GET_BUF_MEMBER(name,type) if (!(name = (*env)->Get##type##Field(env,this_obj, \
  gps_buf_fields.name ## _id))) \
  {\
    LOGE("Error fetching GPSCoordBuffer.%s member", #name);\
    return 0;\
  }\

#define GET_BUF_MEMBER_NO_CHECK(name,type) name = (*env)->Get##type##Field(env,this_obj, \
  gps_buf_fields.name ## _id); 

#define GET_CFG_MEMBER_NO_CHECK(name,type) name = (*env)->Get##type##Field(env,cfg, \
  gps_buf_fields.name ## _id); 
  
  
#define GET_COORD_MEMBER(name,type)  (*env)->Get##type##Field(env,coord, \
  gps_buf_fields.name ## _id)

#define SET_BUF_MEMBER(name,type)  (*env)->Set##type##Field(env,this_obj, \
  gps_buf_fields.name ## _id,name)

static int print_config_double(JNIEnv* env,void* config_obj,Config_var* var,char* buf,size_t buf_size )
{
  return print_config_double_low(env,config_obj,var,buf,buf_size,VAR_TYPE_NORMAL);
}

static int print_config_angle(JNIEnv* env,void* config_obj,Config_var* var, char* buf, size_t buf_size)
{
  return print_config_double_low(env,config_obj,var,buf,buf_size,VAR_TYPE_ANGLE);
}

static int print_config_pace(JNIEnv* env,void* config_obj,Config_var* var, char* buf, size_t buf_size)
{
  return print_config_double_low(env,config_obj,var,buf,buf_size,VAR_TYPE_PACE);
}

static int print_config_double_low(JNIEnv* env, void* config_obj, Config_var* var, char* buf,
                                   size_t buf_size, Config_var_type type)
{
  jdouble val;
  const char* var_name = var->name;
  
  val = (*env)->GetDoubleField(env,(jobject)config_obj,var->var_id);
  
  switch (type)
  {
    case VAR_TYPE_ANGLE:
    {
      var_name = var->lookup_name;
      val = acos(val) * 180.0/M_PI;
      break;
    }  
    case VAR_TYPE_PACE:
    {
      int mm,ss,len,pace_s;
      pace_s = (int)val/1000;
      mm = pace_s / 60;
      ss = pace_s % 60;
      snprintf(buf,buf_size,"%02d:%02d",mm,ss);
      return 0;
    }
    default:
      break;
  }
  
  snprintf(buf,buf_size,"%g",val);
  return 0;
}

static int print_config_str(JNIEnv* env,void* config_obj,Config_var* var, 
                            char* buf, size_t buf_size)
{
  jstring str_obj;
  const char* s;

  if (!(str_obj = (jstring)(*env)->GetObjectField(env,config_obj,var->var_id)))
  {
    LOGE("Error reading string object for ConfigState.%s", var->name);
    return -1;
  }
  
  if (!(s = (*env)->GetStringUTFChars(env, str_obj, NULL)))
  {
    LOGE("Could not get Java string");
    return -1;
  }
  
  strncpy(buf,s,buf_size);
  (*env)->ReleaseStringUTFChars(env,str_obj,s);
  (*env)->DeleteLocalRef(env,str_obj);
  return 0;
}

static int print_config_int(JNIEnv* env, void* config_obj, Config_var* var, char* buf, 
                            size_t buf_size )
{
  jint val;
  
  val = (*env)->GetIntField(env,(jobject)config_obj,var->var_id);
  snprintf(buf,buf_size, "%d",val);
  return 0;
}

static int print_config_long(JNIEnv* env, void* config_obj, Config_var* var, char* buf, 
                             size_t buf_size)
{
  jlong val;
  
  val = (*env)->GetLongField(env,(jobject)config_obj,var->var_id);
  snprintf(buf,buf_size,"%lld",val);
  return 0;
}

static int read_config_str(JNIEnv* env,void* config_obj,Config_var* var, const char* val)
{
  jstring str_obj = (*env)->NewStringUTF(env,val);
  
  if (!str_obj)
  {
    LOGE("Error creating string for %s", var->name);
    return -1;
  }
  
  
  (*env)->SetObjectField(env,(jobject)config_obj,var->var_id,str_obj);
  
  if ((*env)->ExceptionOccurred(env))
  {
    LOGE("Error setting ConfigState.%s", var->name);
    return -1;
  }
  
  return 0;
}

static int read_config_int(JNIEnv* env,void* config_obj,Config_var* var, const char* val)
{
  (*env)->SetIntField(env,(jobject)config_obj,var->var_id,atoi(val));
  return 0;
}

static int read_config_long(JNIEnv* env,void* config_obj,Config_var* var, const char* val)
{
  (*env)->SetLongField(env,(jobject)config_obj,var->var_id,atoll(val));
  return 0;
}

static int read_config_double(JNIEnv* env,void* config_obj,Config_var* var, const char* val)
{
  return read_config_double_low(env,config_obj,var,val,VAR_TYPE_NORMAL);
}

static int read_config_angle(JNIEnv* env,void* config_obj,Config_var* var, const char* val)
{
  return read_config_double_low(env,config_obj,var,val,VAR_TYPE_ANGLE);
}

static int read_config_pace(JNIEnv* env,void* config_obj,Config_var* var, const char* val)
{
  return read_config_double_low(env,config_obj,var,val,VAR_TYPE_PACE);
}

static int read_config_double_low(JNIEnv* env, void* config_obj, Config_var* var, 
                                   const char* val_str, Config_var_type type)
{
  jdouble val;
  
  switch (type)
  {
    case VAR_TYPE_ANGLE:
      val = cos(atof(val_str) * M_PI/180.0);
      break;
    case VAR_TYPE_PACE:
    {
      const char* p = strchr(val_str,':');
      int mm,ss;
      
      if (!p)
      {
        LOGE("Error parsing pace for %s", var->lookup_name);
        return -1;
      }
      
      mm = atoi(val_str);
      ss = atoi(p+1);
      val = (double)(mm * 60 + ss) * 1000.0;
      break;
    }
    default:
      val = atof(val_str);
      break;
  }
  
  (*env)->SetDoubleField(env,(jobject)config_obj,var->var_id,val);
  return 0;
}


static jboolean flush_gps_buffer(JNIEnv* env, jobject this_obj)
{
  jobjectArray buf;
  jint buf_end,flush_ind;
  jobject cur_coord;
  jsize buf_len;
  
  if (!gps_data_fp)
  {
    LOGE("Attempt to flush the GPS data buffer with a closed file descriptor");
    return 0;
  }
  
  GET_BUF_MEMBER(buf,Object);
  GET_BUF_MEMBER_NO_CHECK(buf_end,Int);
  GET_BUF_MEMBER_NO_CHECK(flush_ind,Int);
  
  if (buf_end == flush_ind)
    return 0;
  
  buf_len = (*env)->GetArrayLength(env,buf);
  
  for (; flush_ind != buf_end;)
  {
    jobject coord = (*env)->GetObjectArrayElement(env,buf,flush_ind);
    
    if (!coord)
    {
       LOGE("Coordinate object at index %d is NULL, wonder how that happened", flush_ind);
       continue;
    }
    
    fprintf(gps_data_fp,"%f,%f,%lld,%f,%f,%f,%f\n",
            GET_COORD_MEMBER(lat,Double),
            GET_COORD_MEMBER(lon,Double),
            GET_COORD_MEMBER(ts,Long),
            GET_COORD_MEMBER(dist_to_prev,Double),
            GET_COORD_MEMBER(bearing,Float),
            GET_COORD_MEMBER(accuracy,Float),
            GET_COORD_MEMBER(speed,Float)
            );
   fflush(gps_data_fp);
   
   if (++flush_ind == buf_len)
    flush_ind = 0;
   
   (*env)->DeleteLocalRef(env,coord);
  }
  
  SET_BUF_MEMBER(flush_ind,Int);
  (*env)->DeleteLocalRef(env,buf);
  return 1;
}

static int has_ext(const char* fname, const char* ext)
{
  const char*p = strrchr(fname,'.');
  
  if (!p || strcmp(p + 1,ext))
    return 0;
  
  return 1;
}

static int get_config_path(JNIEnv* env, jobject* cfg_obj, char* path, int max_path, char* fmt, ...)
{
  va_list ap;
  jstring data_dir = 0;
  const char* data_dir_s = 0;
  char* p;
  int data_dir_len = 0;
  va_start(ap,fmt);
  
  if (!(data_dir = (jstring)(*env)->GetObjectField(env,cfg_obj,data_dir_id)))
  {
    LOGE("NULL value for data_dir string object");
    return 1;
  }
  
  if (!(data_dir_s = (*env)->GetStringUTFChars(env,data_dir,0)))
  {
    LOGE("data_dir_s == NULL");
    goto err;
  }
  
  if ((data_dir_len = strlen(data_dir_s)) >= max_path-1)
  {
    LOGE("data_dir too long");
    goto err;
  }
  
  memcpy(path,data_dir_s,data_dir_len);
  p = path + data_dir_len;
  
  if (p[-1] != '/')
    *p++ = '/';
  
  vsnprintf(p, max_path - data_dir_len - 1, fmt,ap);
  
err:
  if (data_dir_s)
    (*env)->ReleaseStringUTFChars(env,data_dir,data_dir_s);
  
  if (data_dir)
    (*env)->DeleteLocalRef(env,data_dir);
  
  return 0;
}


static int remove_expired_files(JNIEnv* env, jobject* this_obj, const char* dir_name)
{
  jobject cfg;
  jint expire_files_days;
  DIR* dir_p;
  struct dirent* dir_e;
  time_t now;
  
  GET_BUF_MEMBER(cfg,Object);
  GET_CFG_MEMBER_NO_CHECK(expire_files_days,Int);

  time(&now);
  
  if (!(dir_p = opendir(dir_name)))
  {
    LOGE("opendir() failed while removing expired files");
    return 1;
  }
  
  while ((dir_e = readdir(dir_p)))
  {
    struct stat s;
    char fname[PATH_MAX+1];
    
    if (has_ext(dir_e->d_name,"cnf"))
      continue;
    
    snprintf(fname,sizeof(fname),"%s/%s",dir_name,dir_e->d_name);
    
    if (stat(fname,&s))
    {
      LOGE("Error reading %s entry during expired file purge", fname);
      continue;
    }
    
    if ((s.st_mode & S_IFMT) == S_IFREG && now - s.st_mtime > expire_files_days * 86400)
    {
      if (unlink(fname))
        LOGE("Could not remove %s", fname);
    }
  }
  
  closedir(dir_p);
  return 0;
}

JNIEXPORT void JNICALL Java_com_fastrunningblog_FastRunningFriend_FastRunningFriend_set_1system_1time
  (JNIEnv *env, jobject this_obj, jlong t_ms)
{
  int fd;
  struct timeval tv;
  
  if ((fd = open("/dev/alarm",O_RDWR)) < 0)
  {
    LOGE("Error opening /dev/alarm while trying to set time: errno=%d", errno);
    return;
  }
  
  tv.tv_sec = t_ms/1000;
  tv.tv_usec = (t_ms % 1000) * 1000;
  
  if (ioctl(fd,ANDROID_ALARM_SET_RTC,&tv) < 0)
  {
    LOGE("ioctl() failed trying to set system time: errno=%d", errno);
  }
  
  close(fd);
}

JNIEXPORT jboolean
Java_com_fastrunningblog_FastRunningFriend_GPSCoordBuffer_flush(JNIEnv* env,
                    jobject this_obj)
{
  return flush_gps_buffer(env,this_obj);
}

JNIEXPORT jboolean
Java_com_fastrunningblog_FastRunningFriend_GPSCoordBuffer_close_1data_1file(JNIEnv* env,
                    jobject this_obj)
{
  jboolean res = 0;
  
  if (gps_data_fp)
  {
    res = (fclose(gps_data_fp) == 0);
    gps_data_fp = 0;
  }
  
  if (gps_debug_fp)
  {
    res |= (fclose(gps_debug_fp) == 0);
    gps_debug_fp = 0;
  }
  
  return res;
}

JNIEXPORT jboolean 
Java_com_fastrunningblog_FastRunningFriend_GPSCoordBuffer_open_1data_1file(JNIEnv* env,
                    jobject this_obj)
{
  char fname[PATH_MAX+1];
  char* p, *p_end = fname + sizeof(fname);
  time_t t;
  struct tm* lt;
  
  if (gps_data_dir_len > sizeof(fname) - 1)
    return 0;
  
  memcpy(fname,gps_data_dir,gps_data_dir_len);
  p = fname + gps_data_dir_len;
  
  if (p + FNAME_EXTRA_SPACE > p_end)
    return 0;
  
  if (p[-1] != '/')
  {
    *p++ = '/';
  }
  
  time(&t);
  
  if (!(lt = localtime(&t)))
    return 0;
  
  if (!strftime(p, p_end - p, "gps_data_%Y_%m_%d-%H_%M_%S.csv", lt))
    return 0;
  
  if (!(gps_data_fp = fopen(fname,"w")))
    return 0;
  
  // reuse fname
  if (strftime(p, p_end - p, "gps_debug_%Y_%m_%d-%H_%M_%S.log", lt))
  {  
    if (!(gps_debug_fp = fopen(fname,"w")))
    {
      LOGE("Could not open debug log %s", fname);
    }
  }
  
  return 1;
}

JNIEXPORT jboolean
Java_com_fastrunningblog_FastRunningFriend_ConfigState_run_1daemon(JNIEnv* env,
                                                  jobject this_obj)
{
  LOGE("Starting config daemon");
  
  if (http_run_daemon(env,this_obj))
  {
    LOGE("Error starting config daemon");
    return 0;
  }
  return 1;
}

JNIEXPORT jboolean
Java_com_fastrunningblog_FastRunningFriend_ConfigState_stop_1daemon(JNIEnv* env,
                                                  jobject this_obj)
{
  LOGE("stopping HTTPD daemon");
  http_stop_daemon();
  return 1;
}

JNIEXPORT jboolean
Java_com_fastrunningblog_FastRunningFriend_ConfigState_daemon_1running(JNIEnv* env,
                                                  jobject this_obj)
{
  return http_daemon_running();
}


JNIEXPORT jboolean
Java_com_fastrunningblog_FastRunningFriend_GPSCoordBuffer_init_1data_1dir( JNIEnv* env,
                                                  jobject this_obj, jstring dir_name_str )
{
  struct stat s;
  jboolean status = 0;
  const char *dir_name = (*env)->GetStringUTFChars(env, dir_name_str, NULL);
  int dir_created = 0;
  
  gps_data_dir[0] = 0;
  
  if (!dir_name)
    goto err;
  
  if (stat(dir_name,&s))
  {
    jobject cfg_obj;
    
    if (mkdir(dir_name,0755))
      goto err;
    
    dir_created = 1;
    
    if (!(cfg_obj = (*env)->GetObjectField(env,this_obj,gps_buf_fields.cfg_id)))
    {
      LOGE("Could not find config object while writing default config file");
      goto done;
    }
    
    if (!write_config(env,cfg_obj,"default"))
      LOGE("Error writing default config file");
    
    goto done;
  }  
  
  if ((s.st_mode & S_IFMT) != S_IFDIR)
    goto err;

done:  
  gps_data_dir_len = strlen(dir_name);
  
  if (gps_data_dir_len > sizeof(gps_data_dir) - 1)
    goto err;
  
  memcpy(gps_data_dir,dir_name,gps_data_dir_len+1);
  status = 1;  
  
  if (!dir_created)
    remove_expired_files(env,this_obj,gps_data_dir);
  
err:  
  if (dir_name)
    (*env)->ReleaseStringUTFChars(env,dir_name_str,dir_name);
  return status;
}

JNIEXPORT void JNICALL Java_com_fastrunningblog_FastRunningFriend_GPSCoordBuffer_debug_1log
  (JNIEnv *env, jobject this_obj, jstring msg)
{
  const char* msg_s = (*env)->GetStringUTFChars(env,msg,0);
  
  if (!msg_s)
    return;
  
  if (gps_debug_fp)
  {
    time_t now;
    char time_buf[64];
    struct tm *now_tm;
    time(&now);
    
    if (!(now_tm = localtime(&now)) || !strftime(time_buf,sizeof(time_buf),"%x %X",now_tm))
     strncpy(time_buf, "Unknown time", sizeof(time_buf));
    
    fprintf(gps_debug_fp,"[%s] %s\n", time_buf,msg_s);
    fflush(gps_debug_fp);
  }
  else
    LOGE("GPS debug message: %s", msg_s);
  
  (*env)->ReleaseStringUTFChars(env,msg,msg_s);
}

JNIEXPORT jboolean JNICALL Java_com_fastrunningblog_FastRunningFriend_ConfigState_read_1config
  (JNIEnv *env, jobject this_obj, jstring profile_name)
{
  FILE* fp = 0;
  const char* profile_name_s = (*env)->GetStringUTFChars(env,profile_name,0);
  char path[PATH_MAX+1];
  jboolean res = 0;
  Config_var* cfg_var_p;
  
  if (!profile_name_s)
    return 0;
  
  if (get_config_path(env,this_obj,path,sizeof(path),"%s.cnf",profile_name_s))
    goto err;
  
  if (!(fp = fopen(path,"r")))
  {
    LOGE("Could not open config file %s for writing", path);
    goto err;
  }

  LOGE("Reading config file");
  
  while (!feof(fp))
  {
    char buf[512];
    char* p,*p_end;
    Config_var* cfg_v;
    
    if (!fgets(buf,sizeof(buf),fp))
      break;
    
    if (*buf == '#')
      continue;
    
    if (!(p = strchr(buf,'=')))
    {
      LOGE("Error in config file on line '%s'", buf);
      goto err;
    }
    p_end = buf + strlen(buf) - 1;
    *p = 0;
    
    HASH_FIND_STR(config_h, buf, cfg_v);
    
    if (!cfg_v)
    {
      LOGE("Unknown variable '%s'", buf);
      continue;
    }
    
    
    for ( ; p_end > buf && isspace(*p_end); p_end--); 
    
    p_end[1] = 0;
    (*cfg_v->reader)(env, this_obj, cfg_v, p + 1);
  }
  
  res = 1;
err:  
  (*env)->ReleaseStringUTFChars(env,profile_name,profile_name_s);
  
  if (fp)
    fclose(fp);
  
  return res;
}
  

JNIEXPORT jboolean JNICALL Java_com_fastrunningblog_FastRunningFriend_ConfigState_write_1config
  (JNIEnv *env, jobject this_obj, jstring profile_name)
{
  jboolean res;
  const char* profile_name_s = (*env)->GetStringUTFChars(env,profile_name,0);
  res = write_config(env,this_obj,profile_name_s);
  (*env)->ReleaseStringUTFChars(env,profile_name,profile_name_s);
  return res;
}

jboolean write_config(JNIEnv* env, jobject this_obj, const char* profile_name_s)
{
  FILE* fp = 0;
  char path[PATH_MAX+1];
  jboolean res = 0;
  Config_var* cfg_var_p;
  
  if (!profile_name_s)
    return 0;
  
  if (get_config_path(env,this_obj,path,sizeof(path),"%s.cnf",profile_name_s))
    goto err;
  
  if (!(fp = fopen(path,"w")))
  {
    LOGE("Could not open config file %s for writing", path);
    goto err;
  }
  
  for (cfg_var_p = config_vars; cfg_var_p->name; cfg_var_p++)
  {
    char buf[512];
    if ((*cfg_var_p->printer)(env,this_obj,cfg_var_p,buf,sizeof(buf)))
      goto err;
    fprintf(fp,"%s=%s\n",cfg_var_p->lookup_name,buf);
  }
  
  res = 1;
err:  
  
  if (fp)
    fclose(fp);
  
  return res;
}


