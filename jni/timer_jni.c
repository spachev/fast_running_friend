#include "timer.h"
#include <jni.h>
#include "timer_jni.h"
#include "log.h"

static Run_timer timer;

/*
 * Class:     com_fastrunningblog_FastRunningFriend_RunTimer
 * Method:    init
 * Signature: (Ljava/lang/String;)Z
 */
JNIEXPORT jboolean JNICALL Java_com_fastrunningblog_FastRunningFriend_RunTimer_init
  (JNIEnv *env, jclass cls, jstring file_prefix)
{
  const char* file_prefix_s = (*env)->GetStringUTFChars(env, file_prefix, NULL);
  int res;
  
  if (!file_prefix_s)
    return 0;
  
  res = run_timer_init(&timer,file_prefix_s);
  (*env)->ReleaseStringUTFChars(env,file_prefix,NULL);
  return res == 0;
}

/*
 * Class:     com_fastrunningblog_FastRunningFriend_RunTimer
 * Method:    start
 * Signature: ()Z
 */
JNIEXPORT jboolean JNICALL Java_com_fastrunningblog_FastRunningFriend_RunTimer_start
  (JNIEnv *env , jclass cls)
{
  return run_timer_start(&timer) == 0;
}

/*
 * Class:     com_fastrunningblog_FastRunningFriend_RunTimer
 * Method:    now
 * Signature: ()J
 */
JNIEXPORT jlong JNICALL Java_com_fastrunningblog_FastRunningFriend_RunTimer_now
  (JNIEnv *env, jclass cls)
{
  return (jlong)run_timer_running_time(&timer);
}
/*
 * Class:     com_fastrunningblog_FastRunningFriend_RunTimer
 * Method:    get_run_info
 * Signature: (Lcom/fastrunningblog/FastRunningFriend/RunInfo;)Z
 */
JNIEXPORT jboolean JNICALL Java_com_fastrunningblog_FastRunningFriend_RunTimer_get_1run_1info
  (JNIEnv *env, jclass cls, jobject run_info)
{
  Run_info info;
  
  if (run_timer_info(&timer,&info))
    return 0;
  
  (*env)->SetLongField(env,run_info,run_info_fields.t_total_id,info.t_total);
  (*env)->SetLongField(env,run_info,run_info_fields.t_split_id,info.t_split);
  (*env)->SetLongField(env,run_info,run_info_fields.t_leg_id,info.t_leg);
  (*env)->SetDoubleField(env,run_info,run_info_fields.d_last_split_id,info.d_last_split);
  (*env)->SetDoubleField(env,run_info,run_info_fields.d_last_leg_id,info.d_last_leg);
  return 1;
}


/*
 * Class:     com_fastrunningblog_FastRunningFriend_RunTimer
 * Method:    pause
 * Signature: ()Z
 */
JNIEXPORT jboolean JNICALL Java_com_fastrunningblog_FastRunningFriend_RunTimer_pause
  (JNIEnv *env, jclass cls,jdouble d)
{
  return run_timer_pause(&timer,d) == 0;
}

/*
 * Class:     com_fastrunningblog_FastRunningFriend_RunTimer
 * Method:    resume
 * Signature: ()Z
 */
JNIEXPORT jboolean JNICALL Java_com_fastrunningblog_FastRunningFriend_RunTimer_resume
  (JNIEnv *env, jclass cls)
{
  return run_timer_resume(&timer) == 0;
}

/*
 * Class:     com_fastrunningblog_FastRunningFriend_RunTimer
 * Method:    reset
 * Signature: ()Z
 */
JNIEXPORT jboolean JNICALL Java_com_fastrunningblog_FastRunningFriend_RunTimer_reset
  (JNIEnv *env, jclass cls)
{
  return run_timer_reset(&timer) == 0;
}

/*
 * Class:     com_fastrunningblog_FastRunningFriend_RunTimer
 * Method:    start_leg
 * Signature: (D)Z
 */
JNIEXPORT jboolean JNICALL Java_com_fastrunningblog_FastRunningFriend_RunTimer_start_1leg
  (JNIEnv *env, jclass cls, jdouble d)
{
  return run_timer_start_leg(&timer,d) == 0;
}

/*
 * Class:     com_fastrunningblog_FastRunningFriend_RunTimer
 * Method:    split
 * Signature: (D)Z
 */
JNIEXPORT jboolean JNICALL Java_com_fastrunningblog_FastRunningFriend_RunTimer_split
  (JNIEnv *env, jclass cls, jdouble d)
{
  return run_timer_split(&timer,d) == 0;
}

JNIEXPORT jstring JNICALL Java_com_fastrunningblog_FastRunningFriend_RunTimer_get_1review_1info
  (JNIEnv *env, jclass cls, jstring file_prefix, jstring workout)
{
  char* review_info;
  Run_timer* cur_timer = &timer, tmp_timer;
  jstring res;

  if (workout)
  {
    const char* workout_str;
    const char* file_prefix_str; ;
    int error = 0;

    if (!(workout_str = (*env)->GetStringUTFChars(env,workout,NULL)))
      return 0;

    if (!(file_prefix_str= (*env)->GetStringUTFChars(env,file_prefix,NULL)))
    {
      (*env)->ReleaseStringUTFChars(env,workout,NULL);
      return 0;
    }

    if (run_timer_init_from_workout(&tmp_timer, file_prefix_str, workout_str))
    {
      error = 1;
      LOGE("Error in run_timer_init_from_workout()");
      goto err;
    }
    (*env)->ReleaseStringUTFChars(env,workout,NULL);
err:
    if (error)
      return 0;

    cur_timer = &tmp_timer;
  }

  review_info = run_timer_review_info(cur_timer,REVIEW_MODE_TEXT);

  if (!review_info)
  {
    LOGE("Error in run_timer_review_info()");
    return 0;
  }
  res = (*env)->NewStringUTF(env,review_info);
  return res;
}

/*
 * Class:     com_fastrunningblog_FastRunningFriend_RunTimer
 * Method:    get_run_list
 * Signature: ()[Ljava/lang/String;
 */
JNIEXPORT jobjectArray JNICALL Java_com_fastrunningblog_FastRunningFriend_RunTimer_get_1run_1list
  (JNIEnv *env , jclass cls)
{
  Mem_pool pool;
  char** rl, **rl_p;
  jobjectArray res;
  uint num_entries;
  jclass str_cls = (*env)->FindClass(env,"java/lang/String");
  
  if (!str_cls)
    return 0;
  
  if (mem_pool_init(&pool,RUN_TIMER_MEM_POOL_BLOCK))
  {
    return 0;
  }  
  
  if (!(rl = run_timer_run_list(&timer,&pool,&num_entries)))
    return 0;
  
  if (!(res = (*env)->NewObjectArray(env,num_entries,str_cls,0)))
    goto err;

  for (rl_p = rl;*rl_p;rl_p++)
  {
    jstring js = (*env)->NewStringUTF(env,*rl_p);
    (*env)->SetObjectArrayElement(env,res,rl_p - rl,js);
  }
  
err:  
  mem_pool_free(&pool);
  return res;
}
