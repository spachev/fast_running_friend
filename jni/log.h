#ifndef LOG_H
#define LOG_H
#include <android/log.h>
#include <errno.h>

#define  LOGE(...)  __android_log_print(ANDROID_LOG_ERROR,"Fast Running Friend",__VA_ARGS__)


#endif