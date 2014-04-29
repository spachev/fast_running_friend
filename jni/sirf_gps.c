#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include "sirf_gps.h"

#include "log.h"

#define PACKET_MSG "SiRF GPS packet dump: "
#define PACKET_MSG_LEN strlen(PACKET_MSG)

static char hexdigit(uint c)
{
  if (c < 10)
    return '0' + c;

  return 'A' + (c - 10);
}

static int write_to_file(char* fname, char* str, uint len)
{
  int fd = open(fname,O_WRONLY|O_CREAT|O_TRUNC);

  if (fd < 0)
  {
    LOGE("Error opening file %s for writing:(%d)", fname, errno);
    return 1;
  }

  if (write(fd, str, len) != len)
  {
    LOGE("Write of %u bytes to %s failed (%d):", len, fname, errno);
    return 0;
  }

  close(fd);
}

static void dump_packet(byte* packet, uint len)
{
  char* buf = (char*)malloc(PACKET_MSG_LEN + len * 3 + 1);
  char* p, *p_end, *bp;

  if (!buf)
  {
    LOGE("OOM trying to dump SiRF GPS packet");
    return;
  }

  p_end = packet + len;
  memcpy(buf,PACKET_MSG,PACKET_MSG_LEN);
  bp = buf + PACKET_MSG_LEN;

  for (p = packet; p < p_end; p++)
  {
    uint c = *p;
    *bp++ = hexdigit((c & 0xf0) >> 4);
    *bp++ = hexdigit(c & 0x0f);
    *bp++ = ' ';
  }

  *bp = 0;
  LOGE(buf);
  free(buf);
}

static void mem_end(Gps_sirf_session* s)
{
  if (s->msg_buf)
  {
    free(s->msg_buf);
    s->msg_buf = 0;
  }
}

int gps_sirf_init(Gps_sirf_session* s)
{
  uint resp_len;
  struct termios tty_opts;

  if (!(s->msg_buf = (byte*)malloc(GPS_SIRF_MSG_BUF_SIZE)))
  {
    LOGE("OOM allocating GPS SiRF message buffer");
    return 1;
  }

  s->msg_buf_end = s->msg_buf + GPS_SIRF_MSG_BUF_SIZE;
  s->cur_p = s->cur_msg = s->msg_buf;

  write_to_file(GPS_SIRF_SLEEP_FILE,"20",2);
  s->fd = s->standby_fd = s->reset_fd = -1;

  if ((s->fd = open(GPS_SIRF_TTY,O_RDWR|O_NOCTTY)) < 0)
  {
    LOGE("Error opening GPS SiRF device %s (%d)", GPS_SIRF_TTY, errno);
    mem_end(s);
    return 1;
  }

  // attempt exclusive access
  (void)ioctl(s->fd, (unsigned long)TIOCEXCL);

  if (tcgetattr(s->fd, &tty_opts) == 0)
  {
    (void)cfsetispeed(&tty_opts, B1500000);
    (void)cfsetospeed(&tty_opts, B1500000);
    (void)tcsetattr(s->fd, TCSANOW, &tty_opts);
  }

  if (gps_sirf_init_pin_magic(s))
  {
    LOGE("Error doing the initial pin magic on GPS SiRF device");
    mem_end(s);
    return 1;
  }

  return 0;
}

int gps_sirf_loop(Gps_sirf_session* s)
{
  Gps_sirf_msg msg;

  if (gps_sirf_init_data_source(s))
  {
    LOGE("SiRF GPS: Error initalizing data source");
    return 1;
  }

  s->done = 0;

  for (; !s->done ;)
  {
    if (gps_sirf_read(s,&msg))
    {
      sleep(1);
      continue;
    }

    switch (msg.id)
    {
      case SIRF_OK_TO_SEND:
        LOGE("GPS SiRF got OK_TO_SEND");

        if (gps_sirf_write(s,"\x84\x00",2))
        {
          LOGE("Error requesting GPS SiRF software version");
        }
        break;
      default:
        LOGE("Got GPS SiRF message ID %u of length %u", (uint)msg.id, msg.len);
        dump_packet(msg.data, msg.len);
        break;
    }
  }

  return 0;
}


int gps_sirf_send_hw_cfg_resp(Gps_sirf_session* s)
{
  // magic string taken from SiRFDrv, TODO: figure out what it actually means
  byte msg[] = {SIRF_HW_CFG_RESP,0x78,0x00,0x05,0xbc,0xa8,0x90,0x00};

  if (gps_sirf_write(s,msg,sizeof(msg)))
  {
    LOGE("Error responding to GPS SiRF HW config request");
    return 1;
  }

  return 0;
}


int gps_sirf_wiggle_reset(Gps_sirf_session* s)
{
  if (write(s->reset_fd,"\x00",1) != 1)
  {
    LOGE("Error writing 0 to GPS SiRF reset pin");
    return 1;
  }

  usleep(GPS_SIRF_PIN_SLEEP/2);

  if (write(s->reset_fd,"\x01",1) != 1)
  {
    LOGE("Error writing 1 to GPS SiRF reset pin");
    return 1;
  }

  usleep(GPS_SIRF_PIN_SLEEP*10);
  return 0;
}

int gps_sirf_wiggle_standby(Gps_sirf_session* s)
{
  if (write(s->standby_fd,"\x01",1) != 1)
  {
    LOGE("Error writing 1 to GPS SiRF standby pin");
    return 1;
  }

  usleep(GPS_SIRF_PIN_SLEEP/2);

  if (write(s->standby_fd,"\x00",1) != 1)
  {
    LOGE("Error writing 0 to GPS SiRF reset pin");
    return 1;
  }

  usleep(GPS_SIRF_PIN_SLEEP/2);
  return 0;
}

/*
int gps_sirf_read_hw_cfg_req(Gps_sirf_session* s)
{
  byte msg_code;

  if (gps_sirf_read(s) == 0)
  {
    LOGE("Error reading GPS SiRF ready packet");
    return 1;
  }

  if ((msg_code=gps_sirf_msg_code(s)) != SIRF_HW_CFG_REQ)
  {
    LOGE("GPS SiRF sent wrong message, expected %u, got %u", SIRF_HW_CFG_REQ , msg_code);
    return 1;
  }

  return 0;
}


int gps_sirf_read_ready(Gps_sirf_session* s)
{
  byte msg_code;

  if (gps_sirf_read(s) == 0)
  {
    LOGE("Error reading GPS SiRF ready packet");
    return 1;
  }

  if ((msg_code=gps_sirf_msg_code(s)) != SIRF_OK_TO_SEND)
  {
    LOGE("GPS SiRF sent wrong message, expected %u, got %u", SIRF_OK_TO_SEND, msg_code);
    return 1;
  }

  return 0;
}
*/

int gps_sirf_init_pin_magic(Gps_sirf_session* s)
{
  int i;

  if ((s->standby_fd = open(GPS_SIRF_STANDBY,O_RDWR)) < 0)
  {
    LOGE("Error opening GPS SiRF device %s (%d)", GPS_SIRF_STANDBY, errno);
    return 1;
  }

  if ((s->reset_fd = open(GPS_SIRF_RESET,O_RDWR)) < 0)
  {
    LOGE("Error opening GPS SiRF device %s (%d)", GPS_SIRF_RESET, errno);
    return 1;
  }

  for (i = 0; i < 2; i++)
  {
    if (gps_sirf_wiggle_reset(s))
    {
      LOGE("Error wiggling GPS SiRF reset on try %d", i + 1);
      return 1;
    }
  }

  if (gps_sirf_wiggle_standby(s))
  {
    LOGE("Error wiggling GPS SiRF standby");
    return 1;
  }

  return 0;
}


int gps_sirf_write(Gps_sirf_session* s, byte* msg, uint msg_len)
{
  uint crc = 0;
  byte* p,*src_p,*src_p_end = msg + msg_len;
  uint write_len = msg_len + 8;
  
  if (msg_len + 8 > sizeof(s->out_buf))
  {
    LOGE("GPS SIRF message of %u bytes is too big", msg_len);
    return 1;
  }

  p = s->out_buf;
  *p++ = (byte)0xa0;
  *p++ = (byte)0xa2;
  *p++ = (byte)(msg_len >> 8);
  *p++ = (byte)(msg_len & 0xff);

  // copy and compute checksum
  for (src_p = msg; src_p < src_p_end; src_p++,p++)
  {
    crc += (int)(*p = *src_p);
  }

  // store checksum
  *p++ = (byte)((crc & 0xff00) >> 8);
  *p++ = (byte)(crc & 0xff);
  *p++ = (byte)0xb0;
  *p++ = (byte)0xb3;

  if (write_len != write(s->fd, s->out_buf, write_len))
  {
    LOGE("Error writing SiRF GPS packet, tried to write %u bytes");
    return 1;
  }

  //(void)tcdrain(s->fd);
  return 0;
}

#define GPS_SIRF_MIN_RESP 4
#define GPS_SIRF_READ_TIMEOUT 1

uint read_data(Gps_sirf_session* s)
{
  byte* p;
  uint len,read_len,bytes_to_read;
  struct timeval timeout;
  fd_set rfds;
  int res;
  
  timeout.tv_sec = 1;
  timeout.tv_usec = 0;
  FD_ZERO(&rfds);
  FD_SET(s->fd, &rfds);
  s->read_wrap = 0;
  
  if ((res = select(s->fd + 1, &rfds, 0, 0, &timeout)) < 0)
  {
    LOGE("Error %d in select() reading from SiRF GPS", errno);
    return 0;
  }
  else if (res == 0)
  {
    LOGE("Timeout reading from SiRF GPS");
    return 0;
  }
  
  if (ioctl(s->fd,FIONREAD,&bytes_to_read))
  {
    LOGE("Error fetching number of bytes to read from SiRF GPS");
  }
  
  LOGE("SiRF GPS: %u bytes to read", bytes_to_read);
  

  // full buffer
  if (s->cur_p + bytes_to_read > s->msg_buf_end)
  {
    uint read_msg_len = s->cur_p - s->cur_msg;
    LOGE("Message buffer is full, recyling the beginning");

    if (s->msg_buf + read_msg_len + bytes_to_read > s->msg_buf_end)
    {
      LOGE("Cannot read message of size %u - not enough room in the buffer", bytes_to_read + read_msg_len);
      return 0;
    }

    if (read_msg_len)
    {
      memcpy(s->msg_buf, s->cur_msg, read_msg_len);
    }

    s->cur_msg = s->msg_buf;
    s->cur_p = s->msg_buf;
    s->read_wrap = 1;
  }

  if (read(s->fd,s->cur_p,bytes_to_read) != bytes_to_read)
  {
    LOGE("I thought SiRF GPS had %u bytes to read! (%d)", bytes_to_read, errno);
    return 0;
  }

  dump_packet(s->cur_p, bytes_to_read);
  s->cur_p += bytes_to_read;
  return bytes_to_read;
}

int gps_sirf_read(Gps_sirf_session* s, Gps_sirf_msg* msg)
{
  byte* p;
  uint len = 0;

  int parsed_header = 0;
  int should_read_fd = (s->cur_p == s->cur_msg);

  for (;;)
  {
    uint read_len,buf_len;

    if (should_read_fd && (read_len = read_data(s)) == 0)
      return 1;

    buf_len = s->cur_p - s->cur_msg;

    if (!parsed_header && buf_len >= GPS_SIRF_MIN_RESP)
    {
      p = s->cur_msg;

      for (; p < s->cur_p - 2 && (*p != 0xa0 || p[1] != 0xa2); p++) /*empty */;

      if (*p != 0xa0 || p[1] != 0xa2)
      {
        s->cur_msg = s->cur_p;
        should_read_fd = 1;
        continue;
      }

      s->cur_msg = p;

      len = ((uint)(p[2]) << 8) + (uint)p[3];
      LOGE("SiRF GPS: len=%u", len);
      parsed_header = 1;
    }

    if (parsed_header && len + 4 <= s->cur_p - s->cur_msg)
    {
      p += 4 + len + 2; // skip the header, body, and checksum

      if (*p != 0xb0 || p[1] != 0xb3)
      {
        LOGE("Bad trailing magic in SiRF GPS response");
        dump_packet(s->cur_msg,buf_len);
        s->cur_msg = p + 2;
        should_read_fd = 1;
        parsed_header = 0;
        continue;
      }

      break;
    }

    // if we had enough in the buffer to parse out the message we would not have gotten here
    should_read_fd = 1;
  }

  msg->id = s->cur_msg[4];
  msg->data = s->cur_msg + 5;
  msg->len = len;
  s->cur_msg += len + 8;
  return 0;
}

int gps_sirf_init_data_source(Gps_sirf_session* s)
{
  char buf[25];
  memset(buf,0,sizeof(buf));
  buf[0] = SIRF_INIT_DATA_SRC;
  buf[24] = 4; // reset all history
  return gps_sirf_write(s,buf,sizeof(buf));
}

int gps_sirf_end(Gps_sirf_session* s)
{
  if (s->fd >= 0)
    close(s->fd);

  if (s->standby_fd >= 0)
  {
    (void)write(s->standby_fd,"\0",1);
    close(s->standby_fd);
  }

  if (s->reset_fd > 0)
  {
    gps_sirf_wiggle_reset(s);
    close(s->reset_fd);
  }

  s->standby_fd = s->reset_fd = s->fd = -1;
  mem_end(s);
  return 0;
}
