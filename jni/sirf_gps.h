#ifndef SIRF_GPS_H
#define SIRF_GPS_H

#define GPS_SIRF_TTY "/dev/ttyS0"
#define GPS_SIRF_STANDBY "/dev/gps_standby"
#define GPS_SIRF_RESET "/dev/gps_reset"
#define GPS_SIRF_SLEEP_FILE "/sys/devices/platform/omap-uart.1/uart0_sleep_timeout"

#define GPS_SIRF_PIN_SLEEP 1000
#define GPS_SIRF_MSG_BUF_SIZE (128*1024)

typedef unsigned char byte;

typedef struct
 {
   int fd,reset_fd,standby_fd;
   byte out_buf[4096];
   byte* msg_buf,*msg_buf_end;
   byte* cur_msg,*cur_p;
   int read_wrap; // flag to show that the read had to wrap to the start of the buffer
   volatile int done;
 } Gps_sirf_session;

typedef struct
  {
    byte id;
    byte* data;
    uint len;
  } Gps_sirf_msg;

int gps_sirf_init(Gps_sirf_session* s);
int gps_sirf_end(Gps_sirf_session* s);
int gps_sirf_write(Gps_sirf_session* s, byte* msg, uint msg_len);
int gps_sirf_read(Gps_sirf_session* s, Gps_sirf_msg* msg);
int gps_sirf_init_pin_magic(Gps_sirf_session* s);
int gps_sirf_wiggle_reset(Gps_sirf_session* s);
int gps_sirf_wiggle_standby(Gps_sirf_session* s);
int gps_sirf_init_data_source(Gps_sirf_session* s);

/*
int gps_sirf_read_ready(Gps_sirf_session* s);
int gps_sirf_read_hw_cfg_req(Gps_sirf_session* s);
*/

int gps_sirf_send_hw_cfg_resp(Gps_sirf_session* s);
int gps_sirf_loop(Gps_sirf_session* s);


#define SIRF_OK_TO_SEND 0x12
#define SIRF_HW_CFG_REQ 0x47
#define SIRF_HW_CFG_RESP 0xd6
#define SIRF_INIT_DATA_SRC 0x80

#endif