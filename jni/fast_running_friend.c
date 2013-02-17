#include <unistd.h>
#include <jni.h>
#include <android/log.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <fcntl.h>
#include <dirent.h>
#include <asm/ioctl.h>
#include <errno.h>
#include <linux/android_alarm.h>

static FILE* gps_data_fp = 0, *gps_debug_fp = 0;
static char gps_data_dir[PATH_MAX+1];
static unsigned int gps_data_dir_len = 0;

#define FNAME_EXTRA_SPACE 32
#define LOG_TAG "FastRunningFriend"
#define  LOGE(...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__)

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

static int init_gps_buf_fields(JNIEnv* env, GPS_buf_fields* fields);
static int remove_expired_files(JNIEnv* env, jobject* this_obj, const char* dir_name);


#define COORD_ARR_SIG "[Lcom/fastrunningblog/FastRunningFriend/GPSCoord;"
#define COORD_BUF_CLASS "com/fastrunningblog/FastRunningFriend/GPSCoordBuffer"
#define CFG_CLASS "com/fastrunningblog/FastRunningFriend/ConfigState"
#define COORD_CLASS "com/fastrunningblog/FastRunningFriend/GPSCoord"

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
  
  if (init_gps_buf_fields(env,&gps_buf_fields))
    return -1;
   
  return JNI_VERSION_1_4;
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
    
    fprintf(gps_data_fp,"%f,%f,%ld,%f,%f,%f,%f\n",
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
    if (mkdir(dir_name,0755))
      goto err;
    
    dir_created = 1;
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
    (*env)->ReleaseStringUTFChars(env,dir_name_str,NULL);
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
    LOGE(msg_s);
  
  (*env)->ReleaseStringUTFChars(env,msg,msg_s);
}
