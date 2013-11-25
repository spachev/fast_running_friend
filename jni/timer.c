#include "timer.h"
#include "log.h"

#include <sys/time.h>
#include <sys/types.h>
#include <dirent.h>
#include <time.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

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

  mem_pool_free(&t->mem_pool);
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
    LL_APPEND(rl_head,rl_tmp);
    (*num_entries)++;
  }
  
  if (!(res = (char**)mem_pool_alloc(pool,sizeof(char*) * (*num_entries+1))))
  {
    LOGE("Out of memory");
    goto err;
  }
  
  res_p = res;
  
  LOGE("Copying names");
  LL_FOREACH(rl_head,rl_tmp)
  {
    *res_p++ = rl_tmp->name;
    LOGE("Copied %s ", rl_tmp->name);
  }
  
  *res_p = 0;
  
err:
  if (d)
    closedir(d);
  
  return res;
}

#define RESET_VARS cur_t = 0; cur_d = 0.0; cur_pow_10 = 0.1;

int run_timer_init_from_workout(Run_timer* t, const char* file_prefix, const char* workout, int init_fp)
{
  char fname[PATH_MAX+1];
  char* p;
  uint len = strlen(file_prefix);
  uint workout_len = strlen(workout);
  FILE* fp = 0, *save_fp = 0;
  ulonglong cur_t = 0;
  double cur_d = 0.0, cur_pow_10 = 0.1;
  int need_start_leg = 1, line_not_empty = 0;
  enum {READ_MODE_TIME, READ_MODE_DIST, READ_MODE_DIST_F} read_mode = READ_MODE_TIME;
  
  if (run_timer_init(t,file_prefix))
    return 1;
  
  if (len > sizeof(fname) - 3 * TIMER_DATA_PREFIX_LEN + 3)
    return 1;
  
  memcpy(fname,file_prefix,len);
  memcpy(fname + len, TIMER_DATA_PREFIX, TIMER_DATA_PREFIX_LEN);
  memcpy(fname + len + TIMER_DATA_PREFIX_LEN, workout, workout_len);
  p = fname + len + workout_len + TIMER_DATA_PREFIX_LEN;
  *p++ = '.';
  memcpy(p,TIMER_DATA_EXT, TIMER_DATA_EXT_LEN);
  p[TIMER_DATA_EXT_LEN] = 0;
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
