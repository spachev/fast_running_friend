#include "timer.h"
#include "log.h"

#include <sys/time.h>
#include <sys/types.h>
#include <dirent.h>
#include <time.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include "sirf_gps.h"

static int start_leg(Run_timer* t, ulonglong ts, double d);
static int start_split(Run_timer* t, ulonglong ts, double d);
static int open_file(Run_timer* t);
static uint print_segment(char* buf, uint buf_size, ulonglong t, double d);
static uint print_time(char* buf, uint buf_size, ulonglong t);

typedef struct st_run_list
{
  char* name;
  struct st_run_list* next;
} RUN_LIST;

int run_timer_init(Run_timer* t, const char* file_prefix)
{
  char* p;

  bzero(t,sizeof(*t));

  if (mem_pool_init(&t->mem_pool,RUN_TIMER_MEM_POOL_BLOCK))
    return 1;

  if (!(t->file_prefix = strdup(file_prefix)))
    return 1;

  if (!(p = strrchr(file_prefix,'/')))
    t->dir_len = 0;
  else
    t->dir_len = p - file_prefix;

  return 0;
}

static uint print_segment(char* buf, uint buf_size, ulonglong t, double d)
{
  uint bytes_printed = print_time(buf, buf_size, t);

  if (buf_size <= bytes_printed)
    return bytes_printed;

  return bytes_printed + snprintf(buf + bytes_printed, buf_size - bytes_printed, " %.3f", d);
}

void run_timer_print_time(UT_string* res, ulonglong t)
{
  char buf[64];
  uint len = print_time(buf,sizeof(buf),t);
  utstring_bincpy(res,buf,len);
}

static uint print_time(char* buf, uint buf_size, ulonglong t)
{
  uint ss_fract,ss,mm,hh;
  ulonglong t_left = t;
  ss_fract = (uint)(t_left % 1000);
  t_left -= ss_fract;
  ss_fract = (ss_fract / 100) + ((ss_fract % 100) > 50);
  if (ss_fract >= 10)
  {
    ss_fract -= 10;
    t_left += 1000;
  }
  ss = t_left / 1000;
  mm = ss / 60;
  ss = (ss % 60);
  hh = mm / 60;
  mm = (mm % 60);

  if (hh)
  {
    return snprintf(buf, buf_size, "%02d:%02d:%02d.%d", hh, mm, ss, ss_fract);
  }

  return snprintf(buf, buf_size, "%02d:%02d.%d", mm, ss, ss_fract);
}

static int open_file(Run_timer* t)
{
  time_t t_now = t->t_start/1000LL;
  struct tm* lt;
  char fname[PATH_MAX+1];
  uint len;

  if (!t->file_prefix)
    return 1;

  if ((len = strlen(t->file_prefix)) > sizeof(fname) - 3 * TIMER_DATA_PREFIX_LEN + 3)
    return 1;

  if (!(lt = localtime(&t_now)))
    return 1;

  memcpy(fname,t->file_prefix,len);

  if (!strftime(fname + len, sizeof(fname) - len, TIMER_DATA_PREFIX TIMER_DATA_FMT "." TIMER_DATA_EXT, lt))
    return 1;

  if (!(t->fp = fopen(fname,"w")))
  {
    LOGE("Could not open file %s (%d)", fname, errno);
    return 1;
  }

  return 0;
}


int run_timer_start(Run_timer* t)
{
  t->t_start = run_timer_now();
  t->t_pause = t->t_delay = 0;

  if (open_file(t))
  {
    LOGE("Will not log timer data to a file");
    // OK to continue, better give user something that nothing
  }

  return start_leg(t,0, 0.0);
}

int run_timer_resume(Run_timer* t)
{
  ulonglong t_now = run_timer_now();

  t->t_delay += t_now - t->t_pause;
  t->t_pause = 0;

  if (t->fp)
  {
    fflush(t->fp);
    fseek(t->fp,t->resume_fp_pos,SEEK_SET);
    ftruncate(fileno(t->fp),t->resume_fp_pos);
  }
  return 0;
}

int run_timer_info(Run_timer* t, Run_info* info)
{
  ulonglong t_run = run_timer_now() - t->t_delay - t->t_start;
  Run_leg* cur_leg = t->cur_leg;
  Run_split* cur_split;

  info->t_total = t_run;

  if (!cur_leg || !cur_leg->first_split)
  {
    info->t_total = info->t_split = info->t_leg = 0;
    info->d_last_split = info->d_last_leg = 0.0;
    return 0;
  }

  info->t_leg = t_run - cur_leg->first_split->t;
  info->d_last_leg = cur_leg->first_split->d;

  if (!(cur_split = cur_leg->cur_split))
  {
    info->t_split = 0;
    info->d_last_split = 0.0;
    return 1;
  }

  info->t_split = t_run - cur_split->t;
  info->d_last_split = cur_split->d;
  return 0;
}

ulonglong run_timer_running_time(Run_timer* t)
{
  if (t->t_pause)
    return t->t_pause - t->t_delay - t->t_start;

  return run_timer_now() - t->t_start - t->t_delay;
}


int run_timer_pause(Run_timer* t, double d)
{
  t->t_pause = run_timer_now();

  if (!t->fp)
    return 1;

  t->resume_fp_pos = ftell(t->fp);
  fprintf(t->fp, "\n%llu,%g\n", run_timer_running_time(t),d);
  fflush(t->fp);
  return 0;
}

void run_timer_deinit(Run_timer* t)
{
  if (t->fp)
  {
    fclose(t->fp);
    t->fp = 0;
  }

  if (t->meta_fp)
  {
    fclose(t->meta_fp);
    t->meta_fp = 0;
  }

  mem_pool_free(&t->mem_pool);

  //no need to iterate through post hash, as it is from mem_pool, so just set to to 0
  t->post_h = 0;
}

int run_timer_reset(Run_timer* t)
{
  int res;
  const char* tmp = 0;

  if (t->fp)
    fputc('\n', t->fp);

  run_timer_deinit(t);

  if (!t->file_prefix)
    return 1;

  if (!(tmp = strdup(t->file_prefix)))
    return 1;

  free((void*)t->file_prefix);
  t->file_prefix = 0;
  res = run_timer_init(t, tmp);
  free((void*)tmp);
  t->post_h = 0;
  return res;
}

static int start_split(Run_timer* t, ulonglong ts, double d)
{
  Run_leg* cur_leg = t->cur_leg;
  Run_split* cur_split = (Run_split*)mem_pool_alloc(&t->mem_pool,sizeof(Run_split));
  LOGE("start_split(%llu,%.3f)",ts,d);

  if (!cur_split)
    return 1;

  cur_split->t = ts;
  cur_split->d = d;
  cur_split->comment = 0;
  cur_split->zone = 0;
  // those are initialized and used later in processing POST
  cur_split->d_d = 0.0;
  cur_split->d_t = 0;

  if (t->fp)
  {
    if (cur_leg->first_split)
      fputc(',',t->fp);

    fprintf(t->fp,"%llu,%g", ts, d);
    fflush(t->fp);
  }
  LL_APPEND(cur_leg->first_split,cur_split);
  cur_leg->cur_split = cur_split;
  t->num_splits++;
  cur_leg->num_splits++;
  return 0;
}


static int start_leg(Run_timer* t, ulonglong ts, double d)
{
  Run_leg* cur_leg;
  LOGE("start_leg(%llu,%.3f)",ts,d);
  if (!(cur_leg = (Run_leg*)mem_pool_alloc(&t->mem_pool,sizeof(Run_leg))))
    return 1;

  cur_leg->cur_split = cur_leg->first_split = 0;
  t->cur_leg = cur_leg;
  cur_leg->num_splits = 0;
  cur_leg->comment = 0;

  if (t->fp)
  {
    if (t->first_leg)
      fputc('\n',t->fp);
  }

  if (start_split(t,ts,d))
    return 1;

  LL_APPEND(t->first_leg,t->cur_leg);
  t->num_legs++;
  return 0;
}

int run_timer_start_leg(Run_timer* t, double d)
{
  return start_leg(t,run_timer_now() - t->t_delay - t->t_start,d);
}

int run_timer_split(Run_timer* t, double d)
{
  return start_split(t,run_timer_now() - t->t_delay - t->t_start,d);
}

ulonglong run_timer_now()
{
  struct timeval t;

  if (gettimeofday(&t,0))
    return 0;

  return (ulonglong)t.tv_sec * 1000LL + (ulonglong)t.tv_usec/1000LL;
}

#define CHECK_BYTES if (bytes_left <= bytes_printed) \
  { free(buf); return 0; } else { bytes_left -= bytes_printed; p += bytes_printed; }

char* run_timer_review_info(Run_timer* t, Run_timer_review_mode mode)
{
  uint buf_size = t->num_legs * 32 + t->num_splits * 16 + 64; /* should be enough */
  char* buf;
  Run_leg* cur_leg;
  Run_split* cur_split;
  char* p;
  int bytes_left;
  ulonglong t_tmp;
  double d_tmp;
  uint bytes_printed;

  if (!(buf = (char*)malloc(buf_size)))
    return 0;

  p = buf;
  bytes_left = buf_size;

  LL_FOREACH(t->first_leg,cur_leg)
  {
    LOGE("leg split in review:(%llu,%.3f)", cur_leg->first_split->t,cur_leg->first_split->d);
    if (!cur_leg->next)
      continue;

    bytes_printed = snprintf(p,bytes_left,"L:");
    CHECK_BYTES;
    bytes_printed = print_segment(p,bytes_left,
                                  cur_leg->next->first_split->t - cur_leg->first_split->t,
                                  cur_leg->next->first_split->d - cur_leg->first_split->d);
    CHECK_BYTES;
    bytes_printed = snprintf(p,bytes_left," Sp:");
    CHECK_BYTES;

    LL_FOREACH(cur_leg->first_split,cur_split)
    {
      if (cur_split->next)
      {
        t_tmp = cur_split->next->t;
        d_tmp = cur_split->next->d;
      }
      else
      {
        t_tmp = cur_leg->next->first_split->t;
        d_tmp = cur_leg->next->first_split->d;
      }

      bytes_printed = print_segment(p,bytes_left,t_tmp - cur_split->t,d_tmp - cur_split->d);
      CHECK_BYTES;
      *p = ' ';
      bytes_printed = 1;
      CHECK_BYTES;
    }

    *p = '\n';
    bytes_printed = 1;
    CHECK_BYTES;
  }

  *p = 0;
  bytes_printed = 1;
  CHECK_BYTES;

  return buf;
}

static int rl_compare(RUN_LIST* a, RUN_LIST* b)
{
  if (!a) return -1;
  if (!b) return 1;
  //LOGE("Comparing %s and %s", a->name, b->name);
  return strcmp(a->name, b->name);
}

char** run_timer_run_list(Run_timer* t, Mem_pool* pool,uint* num_entries)
{
  char* dir_name = (char*)mem_pool_dup(pool,t->file_prefix,t->dir_len+1);
  struct dirent* d_ent;
  RUN_LIST* rl_head = 0,*rl_tmp;
  DIR* d = 0;
  char** res = 0,**res_p;

  if (!dir_name)
    return 0;

  dir_name[t->dir_len] = 0;

  if (!(d = opendir(dir_name)))
  {
    LOGE("Could not open directory %s (%d)", dir_name, errno);
    goto err;
  }

 *num_entries = 0;

  while ((d_ent = readdir(d)))
  {
    char *dot = strrchr(d_ent->d_name,'.');
    char* name;
    uint name_len;

    if (!dot || strcmp(dot + 1,TIMER_DATA_EXT))
      continue;

    name_len = dot - d_ent->d_name;

    if (name_len < TIMER_DATA_PREFIX_LEN || memcmp(d_ent->d_name,TIMER_DATA_PREFIX,TIMER_DATA_PREFIX_LEN))
      continue;

    name = d_ent->d_name + TIMER_DATA_PREFIX_LEN;
    name_len -= TIMER_DATA_PREFIX_LEN;

    if (!(rl_tmp = (RUN_LIST*)mem_pool_alloc(pool,sizeof(*rl_tmp) + name_len + 1)))
    {
      LOGE("Error allocating memory");
      goto err;
    }

    rl_tmp->name = (char*)(rl_tmp+1);
    memcpy(rl_tmp->name,name,name_len);
    rl_tmp->name[name_len] = 0;
    rl_tmp->next = 0;
    //LOGE("rl_tmp = %p name=%p name_len = %d", rl_tmp, rl_tmp->name, name_len);
    LL_APPEND(rl_head,rl_tmp);
    (*num_entries)++;
  }

  LL_SORT(rl_head, rl_compare);

  if (!(res = (char**)mem_pool_alloc(pool,sizeof(char*) * (*num_entries+1))))
  {
    LOGE("Out of memory");
    goto err;
  }

  res_p = res;
  LOGE("Copying names");

  if (rl_head)
  {
    LL_FOREACH(rl_head,rl_tmp)
    {
      LOGE("rl_tmp = %p",rl_tmp);
      *res_p++ = rl_tmp->name;
      LOGE("Copied %s ", rl_tmp->name);
    }
  }

  *res_p = 0;

err:
  if (d)
    closedir(d);

  return res;
}

static int read_uint(char** s, const char* buf_end, uint* out)
{
  char* p = *s;
  uint res = 0;

  if (p >= buf_end)
    return 1;

  for (; p < buf_end && isdigit(*p); p++)
  {
    res = 10 * res + *p - '0';
  }

  if (p == buf_end)
    return 1;

  *out = res;
  *s = p;
  return 0;
}

static int read_str(Run_timer* t, char** s, const char* buf_end, char** out)
{
  char* p = *s;
  UT_string* tmp;
  int res = 1;
  int in_esc = 0;

  if (p >= buf_end || *p++ != '"' || p == buf_end)
    return 1;

  utstring_new(tmp);

  for (;p < buf_end;p++)
  {
    switch (*p)
    {
      case '"':
        if (in_esc)
        {
          utstring_bincpy(tmp,p,1);
          in_esc = 0;
        }
        else
          goto done;

        break;

      case '\\':
        if (in_esc)
        {
          utstring_bincpy(tmp,p,1);
          in_esc = 0;
        }
        else
          in_esc = 1;

        break;

      case 'n':
        if (in_esc)
        {
          utstring_bincpy(tmp,"\n",1);
          in_esc = 0;
        }
        else
          utstring_bincpy(tmp,p,1);

        break;

      default:
        utstring_bincpy(tmp,p,1);
        break;
    }
  }

done:

  if (p == buf_end || ++p == buf_end)
    goto err;

  *s = p;

  if (!(*out = (char*)mem_pool_dup(&t->mem_pool,utstring_body(tmp),utstring_len(tmp)+1)))
  {
    LOGE("Out of memory parsing string");
    return 1;
  }

  res = 0;

err:

  utstring_free(tmp);
  return res;
}

static int init_meta_file(Run_timer* t, const char* fname)
{
  FILE* fp;
  int file_empty = 0;
  long file_size;
  char* buf, *p, *buf_end;
  Run_leg* l;
  Run_split* sp;

  LOGE("Initializing meta file %s", fname);

  if (!(fp = fopen(fname,"r+")))
  {
    if (!(fp = fopen(fname,"w+")))
    {
      LOGE("Error opening meta file %s", fname);
      return 1;
    }

    file_empty = 1;
  }

  t->meta_fp = fp;

  if (file_empty)
    return 0;

  fseek(fp,0L,SEEK_END);

  if ((file_size = ftell(fp)) <= 0)
  {
    LOGE("Nothing to read from meta file");
    return 0;
  }

  rewind(fp);

  // Read errors are not fatal, do not report error - partially read meta file is still usefull

  if (!(buf = (char*)mem_pool_alloc(&t->mem_pool,file_size)))
  {
    LOGE("Could not allocate memory buffer to read meta file but can live with it");
    return 0;
  }

  if (fread(buf,file_size,1,fp) != 1)
  {
    LOGE("Error reading from meta file, but can live with it");
    return 0;
  }

  p = buf;
  buf_end = buf + file_size;

  if (read_str(t,&p,buf_end,&t->comment))
  {
    LOGE("Parse error reading workout comment");
    return 0;
  }

  if (*p++ != '\n')
  {
    LOGE("No newline after workout comment");
    return 0;
  }

  LL_FOREACH(t->first_leg,l)
  {
    if (read_str(t,&p,buf_end,&l->comment))
    {
      LOGE("Error reading leg comment");
      return 0;
    }

    if (*p++ != '\n')
    {
      LOGE("No newline after leg comment");
      return 0;
    }

    LL_FOREACH(l->first_split,sp)
    {
      if (read_uint(&p,buf_end,&sp->zone))
      {
        LOGE("Error reading split zone");
        return 0;
      }

      if (*p++ != ',')
      {
        LOGE("Missing comma after split zone");
        return 0;
      }

      if (read_str(t,&p,buf_end,&sp->comment))
      {
        LOGE("Error parsing split comment");
        return 0;
      }

      if (*p++ != '\n')
      {
        LOGE("Missing newline after split comment");
        return 0;
      }
    }
  }
  return 0;
}

#define RESET_VARS cur_t = 0; cur_d = 0.0; cur_pow_10 = 0.1;

int run_timer_init_from_workout(Run_timer* t, const char* file_prefix, const char* workout, int init_fp)
{
  char fname[PATH_MAX+1],meta_fname[PATH_MAX+1];
  char* p;
  uint len = strlen(file_prefix);
  uint workout_len = strlen(workout);
  FILE* fp = 0, *save_fp = 0,*meta_fp = 0;
  ulonglong cur_t = 0;
  double cur_d = 0.0, cur_pow_10 = 0.1;
  int need_start_leg = 1, line_not_empty = 0;
  enum {READ_MODE_TIME, READ_MODE_DIST, READ_MODE_DIST_F} read_mode = READ_MODE_TIME;

  if (run_timer_init(t,file_prefix))
    return 1;

  if (len > sizeof(fname) - 3 * TIMER_DATA_PREFIX_LEN + 3)
    return 1;

  if (!(t->workout_ts = (char*)mem_pool_dup(&t->mem_pool,workout,workout_len+1)))
  {
    LOGE("OOM initializing workout");
    return 1;
  }

  t->workout_ts_len = workout_len;
  memcpy(fname,file_prefix,len);
  memcpy(fname + len, TIMER_DATA_PREFIX, TIMER_DATA_PREFIX_LEN);
  memcpy(fname + len + TIMER_DATA_PREFIX_LEN, workout, workout_len);
  p = fname + len + workout_len + TIMER_DATA_PREFIX_LEN;
  *p++ = '.';
  memcpy(p,TIMER_DATA_EXT, TIMER_DATA_EXT_LEN);
  p[TIMER_DATA_EXT_LEN] = 0;

  memcpy(meta_fname,file_prefix,len);
  memcpy(meta_fname + len, META_DATA_PREFIX, META_DATA_PREFIX_LEN);
  memcpy(meta_fname + len + META_DATA_PREFIX_LEN, workout, workout_len);
  p = meta_fname + len + workout_len + META_DATA_PREFIX_LEN;
  *p++ = '.';
  memcpy(p,META_DATA_EXT, META_DATA_EXT_LEN);
  p[META_DATA_EXT_LEN] = 0;

  save_fp = t->fp;
  t->fp = 0;

  if (!(fp = fopen(fname,"r+")))
  {
    LOGE("Could not open %s for reading", fname);
    return 1;
  }

  for (;;)
  {
    int c = fgetc(fp);

    if (feof(fp))
    {
      if (need_start_leg && line_not_empty)
      {
        start_leg(t,cur_t,cur_d);
        need_start_leg = 0;
      }
      else if (line_not_empty)
        start_split(t,cur_t,cur_d);

      RESET_VARS
      break;
    }

    switch (c)
    {
      case '\n':
        if (!line_not_empty)
          break;

        if (need_start_leg)
          start_leg(t,cur_t,cur_d);
        else
          start_split(t,cur_t,cur_d);

        RESET_VARS
        read_mode = READ_MODE_TIME;
        need_start_leg = 1;
        line_not_empty = 0;
        break;
      case ',':
        switch (read_mode)
        {
          case READ_MODE_TIME:
            read_mode = READ_MODE_DIST;
            break;
          case READ_MODE_DIST_F:
          case READ_MODE_DIST:

            if (need_start_leg)
            {
              start_leg(t,cur_t,cur_d);
              need_start_leg = 0;
            }
            else
              start_split(t,cur_t,cur_d);

            RESET_VARS
            read_mode = READ_MODE_TIME;
            break;
        }
        break;
      case '.':
        read_mode = READ_MODE_DIST_F;
        cur_pow_10 = 0.1;
        break;
      default:
        if (isdigit(c))
        {
          line_not_empty = 1;

          switch (read_mode)
          {
            case READ_MODE_TIME:
              cur_t = cur_t * 10 + (c - '0');
              break;
            case READ_MODE_DIST:
              cur_d = cur_d * 10.0 + (double)(c - '0');
              break;
            case READ_MODE_DIST_F:
              cur_d += (double)(c - '0') * cur_pow_10;
              cur_pow_10 /= 10.0;
              break;
          }
        }
        break;
    }
  }

  if (init_meta_file(t,meta_fname))
  {
    LOGE("Error initializing from meta file");
    return 1;
  }

  if (init_fp)
  {
    t->fp = fp;
  }
  else
  {
    if (fp)
      fclose(fp);

    t->fp = save_fp;
  }
  return 0;
}

#undef CHECK_BYTES

int run_timer_init_split_arr(Run_timer* t)
{
  Run_leg* cur_leg,**leg_p,**leg_end;

  if (!t->num_legs)
  {
    LOGE("Empty timer");
    return 1;
  }

  if (!(t->leg_arr = (Run_leg**)mem_pool_alloc(&t->mem_pool,sizeof(Run_leg*)*t->num_legs)))
  {
    LOGE("No memory for leg array");
    return 1;
  }

  bzero(t->leg_arr,sizeof(Run_leg*)*t->num_legs);
  leg_end = (leg_p = t->leg_arr) + t->num_legs;

  LL_FOREACH(t->first_leg,cur_leg)
  {
    Run_split** split_p, *cur_split, **split_end;

    if (leg_p >= leg_end)
      break;

    *leg_p++ = cur_leg;

    if (!(cur_leg->split_arr = (Run_split**)mem_pool_alloc(&t->mem_pool,cur_leg->num_splits*sizeof(Run_split*))))
    {
      LOGE("No memory for split array");
      return 1;
    }

    bzero(cur_leg->split_arr,cur_leg->num_splits*sizeof(Run_split*));
    split_end = (split_p = cur_leg->split_arr) + cur_leg->num_splits;

    LL_FOREACH(cur_leg->first_split,cur_split)
    {
      if (split_p >= split_end)
        break;

      *split_p++ = cur_split;
    }
  }

  return 0;
}

Run_leg* run_timer_get_leg(Run_timer* t, int leg_num)
{
  Run_leg* l;

  if (leg_num > t->num_legs || !(l = t->leg_arr[leg_num-1]))
    return 0;

  return l;
}


Run_split* run_timer_get_split(Run_timer* t, int leg_num, int split_num)
{
  Run_leg* l;
  Run_split* sp;

  if (leg_num > t->num_legs || !(l = t->leg_arr[leg_num-1]))
    return 0;

  if (split_num > l->num_splits || !(sp = l->split_arr[split_num-1]))
    return 0;

  return sp;
}

ulonglong run_timer_parse_time(const char* s, uint len)
{
  const char*p = s, *p_end = s + len;
  ulonglong res = 0,res_tmp=0;
  int  seen_dot = 0;
  uint ms_mul = 1000;

  for (;p < p_end;p++)
  {
    if (isdigit(*p))
    {
      res_tmp = res_tmp * 10 + (*p - '0');

      if (seen_dot && ms_mul >= 10)
        ms_mul /= 10;

      continue;
    }

    switch(*p)
    {
      case ':':
        res = res * 60 + res_tmp;
        res_tmp = 0;
        break;
      case '.':
        seen_dot = 1;
        res = (res * 60 + res_tmp)*1000;
        res_tmp = 0;
        break;
    }
  }

  if (seen_dot)
  {
    res += res_tmp * ms_mul;
  }

  return res;
}

// at some point move it into a util module
static void csv_print(FILE* fp, const char* s)
{
  fputc('"',fp);

  if (s)
    for (;*s;s++)
    {
      switch (*s)
      {
        case '"':
        case '\\':
          fputc('\\',fp);
          fputc(*s,fp);
          break;
        case '\n':
          fputs("\\n",fp);
          break;
        case '\r':
          break;
        default:
          fputc(*s,fp);
          break;
      }
    }

  fputc('"',fp);
}

int run_timer_save(Run_timer* t)
{
  Run_leg* cur_leg;
  Run_split* cur_split;
  ulonglong cur_t = 0;
  double cur_d = 0.0;
  long pos;

  if (!t->meta_fp)
  {
    LOGE("BUG: run_timer_save() called with NULL meta_fp");
    return 1;
  }

  if (!t->fp)
  {
    LOGE("BUG: run_timer_save() called with NULL fp");
    return 1;
  }

  rewind(t->fp);
  rewind(t->meta_fp);
  csv_print(t->meta_fp,t->comment);
  fputc('\n',t->meta_fp);

  LL_FOREACH(t->first_leg,cur_leg)
  {
    int line_start = 1;
    csv_print(t->meta_fp,cur_leg->comment);
    fputc('\n',t->meta_fp);

    LL_FOREACH(cur_leg->first_split,cur_split)
    {
      cur_split->t = cur_t;
      cur_split->d = cur_d;
      cur_t += cur_split->d_t;
      cur_d += cur_split->d_d;

      if (!line_start)
        fputc(',',t->fp);
      else
        line_start = 0;

      fprintf(t->fp,"%llu,%g",cur_split->t,cur_split->d);
      fprintf(t->meta_fp,"%d,",cur_split->zone);
      csv_print(t->meta_fp,cur_split->comment);
      fputc('\n',t->meta_fp);
    }

    fputc('\n',t->fp);
  }

  fflush(t->fp);

  if ((pos = ftell(t->fp)) < 0)
    return 1;

  ftruncate(fileno(t->fp),pos);
  return 0;
}

int run_timer_add_key_to_hash(Run_timer* t, const char* key, const char* data, uint size)
{
  Url_hash_entry* e = 0;
  size_t key_len = strlen(key);

  HASH_FIND_STR(t->post_h,key,e);

  // append if the entry is present
  if (e)
  {
    uint new_val_len = size + e->val_len;
    char* new_val;

    LOGE("Found entry with key '%s' for key '%s'", e->key, key);

    if (!(new_val = (char*)mem_pool_alloc(&t->mem_pool,new_val_len)))
    {
      LOGE("OOM allocating additional space for value");
      return 1;
    }

    memcpy(new_val,e->val,e->val_len);
    memcpy(new_val + e->val_len, data, size);
    new_val[new_val_len] = 0;
    e->val = new_val;
    e->val_len = new_val_len;
    return 0;
  }

  // if the entry is not present we get here
  if (!(e = (Url_hash_entry*)mem_pool_alloc(&t->mem_pool,sizeof(*e) + key_len + size + 2)))
  {
    LOGE("Out of memory in add_key_to_hash()");
    return 1;
  }

  e->key = (char*)(e + 1);
  e->val = e->key + key_len + 1;
  memcpy(e->key, key, key_len + 1);
  memcpy(e->val, data, size);
  e->val[size] = 0;
  e->key_len = key_len;
  e->val_len = size;

  HASH_ADD_KEYPTR(hh,t->post_h,e->key,key_len,e);
  return 0;
}

int run_timer_parse_keys(Run_timer* t)
{
  Url_hash_entry* he;

  for (he = t->post_h; he; he = he->hh.next)
  {
    if (run_timer_parse_key(t,he))
    {
      LOGE("Error parsing key %s but we can live with it", he->key);
    }
  }

  return 0;
}

int run_timer_parse_key(Run_timer* t, Url_hash_entry* he)
{
  char* key = he->key;
  size_t size = he->val_len;
  char* data = he->val;

  if (*key == 't' || *key == 'd' || *key == 'z'|| *key == 'c')
  {
    uint leg_num = 0,split_num = 0;
    const char*p = key + 1;
    Run_split* sp;

    if (*p++ != '_')
      goto done;

    for (;isdigit(*p);p++)
    {
      leg_num = leg_num*10 + (*p - '0');
    }

    if (*p++ != '_')
      goto done;

    for (;isdigit(*p);p++)
    {
      split_num = split_num*10 + (*p - '0');
    }

    if (split_num == 0 && *key == 'c')
    {
      if (leg_num == 0)
      {
        t->comment = (char*)mem_pool_cdup(&t->mem_pool,data,size);
        goto done;
      }
      else
      {
        Run_leg* l = run_timer_get_leg(t,leg_num);

        if (!l)
        {
          LOGE("Leg %d not found for comment field", leg_num);
          goto done;
        }

        l->comment = (char*)mem_pool_cdup(&t->mem_pool,data,size);
        goto done;
      }
    }

    if (!(sp = run_timer_get_split(t,leg_num,split_num)))
    {
      LOGE("Split %d for leg %d not found", split_num,leg_num);
      goto done;
    }

    switch (*key)
    {
      case 't':
        sp->d_t = run_timer_parse_time(data,size);
        LOGE("Parsed time %-.*s into %llu ms", size, data, sp->d_t);
        break;
      case 'd':
      {
        char buf[32];

        if (size + 1 > sizeof(buf))
          goto done;

        memcpy(buf,data,size);
        buf[size] = 0;
        sp->d_d = strtod(buf, NULL);
        break;
      }
      case 'z':
      {
        const char* p = data, *p_end = data + size;
        uint z = 0;

        for (;p < p_end;p++)
        {
          z = z * 10 + *p - '0';
        }

        sp->zone = z;
        break;
      }
      case 'c':
      {
        sp->comment = (char*)mem_pool_cdup(&t->mem_pool,data,size);
        break;
      }
      default: /* impossible */
        break;
    }
  }
done:
 return 0;
}

void run_timer_stop_sirf_gps(Run_timer* t)
{
  t->sirf.done = 1;
}

void run_timer_run_sirf_gps(Run_timer* t)
{
  if (gps_sirf_init(&t->sirf) == 0)
    gps_sirf_loop(&t->sirf);

  gps_sirf_end(&t->sirf);
}

