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
LOCAL_MODULE    := libcurl
LOCAL_SRC_FILES := libcurl/asyn-ares.c libcurl/asyn-thread.c libcurl/axtls.c libcurl/base64.c libcurl/bundles.c libcurl/conncache.c libcurl/connect.c libcurl/content_encoding.c libcurl/cookie.c libcurl/curl_addrinfo.c libcurl/curl_darwinssl.c libcurl/curl_fnmatch.c libcurl/curl_gethostname.c libcurl/curl_gssapi.c libcurl/curl_memrchr.c libcurl/curl_multibyte.c libcurl/curl_ntlm.c libcurl/curl_ntlm_core.c libcurl/curl_ntlm_msgs.c libcurl/curl_ntlm_wb.c libcurl/curl_rtmp.c libcurl/curl_sasl.c libcurl/curl_schannel.c libcurl/curl_sspi.c libcurl/curl_threads.c libcurl/cyassl.c libcurl/dict.c libcurl/dotdot.c libcurl/easy.c libcurl/escape.c libcurl/file.c libcurl/fileinfo.c libcurl/formdata.c libcurl/ftp.c libcurl/ftplistparser.c libcurl/getenv.c libcurl/getinfo.c libcurl/gopher.c libcurl/gskit.c libcurl/gtls.c libcurl/hash.c libcurl/hmac.c libcurl/hostasyn.c libcurl/hostcheck.c libcurl/hostip4.c libcurl/hostip6.c libcurl/hostip.c libcurl/hostsyn.c libcurl/http2.c libcurl/http.c libcurl/http_chunks.c libcurl/http_digest.c libcurl/http_negotiate.c libcurl/http_negotiate_sspi.c libcurl/http_proxy.c libcurl/idn_win32.c libcurl/if2ip.c libcurl/imap.c libcurl/inet_ntop.c libcurl/inet_pton.c libcurl/krb5.c libcurl/ldap.c libcurl/llist.c libcurl/md4.c libcurl/md5.c libcurl/memdebug.c libcurl/mprintf.c libcurl/multi.c libcurl/netrc.c libcurl/non-ascii.c libcurl/nonblock.c libcurl/nss.c libcurl/nwlib.c libcurl/nwos.c libcurl/openldap.c libcurl/parsedate.c libcurl/pingpong.c libcurl/pipeline.c libcurl/polarssl.c libcurl/polarssl_threadlock.c libcurl/pop3.c libcurl/progress.c libcurl/qssl.c libcurl/rawstr.c libcurl/rtsp.c libcurl/security.c libcurl/select.c libcurl/sendf.c libcurl/share.c libcurl/slist.c libcurl/smtp.c libcurl/socks.c libcurl/socks_gssapi.c libcurl/socks_sspi.c libcurl/speedcheck.c libcurl/splay.c libcurl/ssh.c libcurl/sslgen.c libcurl/ssluse.c libcurl/strdup.c libcurl/strequal.c libcurl/strerror.c libcurl/strtok.c libcurl/strtoofft.c libcurl/telnet.c libcurl/tftp.c libcurl/timeval.c libcurl/transfer.c libcurl/url.c libcurl/version.c libcurl/warnless.c libcurl/wildcard.c libcurl/x509asn1.c
  
LOCAL_C_INCLUDES := $(LOCAL_PATH)/libcurl
LOCAL_CFLAGS := -DHAVE_CONFIG_H
include $(BUILD_STATIC_LIBRARY)


include $(CLEAR_VARS)
LOCAL_MODULE    := fast_running_friend 
LOCAL_SRC_FILES := fast_running_friend.c http_daemon.c timer.c timer_jni.c mem_pool.c url.c frb.c config_vars.c \
  sirf_gps.c
LOCAL_C_INCLUDES := $(LOCAL_PATH)/libmicrohttpd $(LOCAL_PATH)/libcurl
LOCAL_STATIC_LIBRARIES := microhttpd libcurl
LOCAL_LDLIBS    := -lm -llog 
LOCAL_CFLAGS := -DHAVE_CONFIG_H 

 
include $(BUILD_SHARED_LIBRARY)


  
