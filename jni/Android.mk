# Copyright (C) 2009 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE    := microhttpd
LOCAL_SRC_FILES := libmicrohttpd/base64.c libmicrohttpd/basicauth.c libmicrohttpd/connection.c \
  libmicrohttpd/daemon.c libmicrohttpd/digestauth.c \
  libmicrohttpd/internal.c libmicrohttpd/md5.c libmicrohttpd/memorypool.c \
  libmicrohttpd/postprocessor.c libmicrohttpd/reason_phrase.c libmicrohttpd/response.c \
  libmicrohttpd/tsearch.c
  
LOCAL_C_INCLUDES := $(LOCAL_PATH)/libmicrohttpd
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE    := fast_running_friend 
LOCAL_SRC_FILES := fast_running_friend.c http_daemon.c timer.c timer_jni.c mem_pool.c 
LOCAL_C_INCLUDES := $(LOCAL_PATH)/libmicrohttpd 
LOCAL_STATIC_LIBRARIES := microhttpd
LOCAL_LDLIBS    := -lm -llog 

 
include $(BUILD_SHARED_LIBRARY)


  
