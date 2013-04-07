#include "timer.h"
#include "log.h"

#include <sys/time.h>
#include <time.h>
#include <strings.h>
#include <unistd.h>

static int start_leg(Run_timer* t, ulonglong ts, double d);
static int start_split(Run_timer* t, ulonglong ts, double d);
static int open_file(Run_timer* t);

int run_timer_init(Run_timer* t, const char* file_prefix)
{
  bzero(t,sizeof(*t));
  
  if (mem_pool_init(&t->mem_pool,RUN_TIMER_MEM_POOL_BLOCK))
    return 1;
  
  if (!(t->file_prefix = strdup(file_prefix)))
    return 1;
  
  return 0;
}

static int open_file(Run_timer* t)
{
  time_t t_now = t->t_start/1000LL;
  struct tm* lt;
  char fname[PATH_MAX+1];
  uint len;
  
  if (!t->file_prefix)
    return 1;
  
  len = strlen(t->file_prefix);
  
  if (!(lt = localtime(&t_now)))
    return 1;

  memcpy(fname,t->file_prefix,len);
  
  if (!strftime(fname + len, sizeof(fname) - len, "timer_data_%Y_%m_%d-%H_%M_%S.csv", lt))
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


int run_timer_pause(Run_timer* t)
{
  t->t_pause = run_timer_now();
  return 0;
}

int run_timer_reset(Run_timer* t)
{
  int res;
  const char* tmp = 0;
  
  if (t->fp)
  {
    fputc('\n', t->fp);
    fclose(t->fp);
    t->fp = 0;
  }
  mem_pool_free(&t->mem_pool);
  
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
  
  if (!cur_split)
    return 1;
  
  cur_split->t = ts;
  cur_split->d = d;
  
  if (t->fp)
  {  
    if (cur_leg->first_split)
      fputc(',',t->fp);
    
    fprintf(t->fp,"%llu,%g", ts, d);
    fflush(t->fp);
  }  
  LL_APPEND(cur_leg->first_split,cur_split);
  cur_leg->cur_split = cur_split;
  return 0;
}


static int start_leg(Run_timer* t, ulonglong ts, double d)
{
  Run_leg* cur_leg;
  
  if (!(cur_leg = (Run_leg*)mem_pool_alloc(&t->mem_pool,sizeof(Run_leg))))
    return 1;
  
  cur_leg->cur_split = cur_leg->first_split = 0;
  t->cur_leg = cur_leg;
  
  if (t->fp)
  {
    if (t->first_leg)
      fputc('\n',t->fp);
  }
  
  if (start_split(t,ts,d))
    return 1;
  
  LL_APPEND(t->first_leg,t->cur_leg); 
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
