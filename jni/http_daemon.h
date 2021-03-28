#ifndef HTTP_DAEMON_H
#define HTTP_DAEMON_H

#include <jni.h>
#include "utstring.h"
#include "uthash.h"

#define DATA_DIR "/mnt/sdcard/FastRunningFriend/"

int http_run_daemon(JNIEnv* env,jobject* cfg_obj);
void http_stop_daemon();
int http_daemon_running();
void print_js_escaped(UT_string* res, const char* s);

#endif
