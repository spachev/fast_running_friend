#ifndef TIMER_H
#define TIMER_H

#include "utlist.h"
#include "mem_pool.h"
#include <stdio.h>
#include <sys/types.h>

typedef unsigned long long ulonglong;

typedef struct st_run_split
{
  ulonglong t;
  double d;
  struct st_run_split *next;
} Run_split;

typedef struct st_run_leg
{
  Run_split* first_split;
  Run_split* cur_split;
  struct st_run_leg *next;
} Run_leg;

#define RUN_TIMER_MEM_POOL_BLOCK 8192
#define TIMER_DATA_PREFIX "timer_data_"
#define TIMER_DATA_PREFIX_LEN (strlen(TIMER_DATA_PREFIX))
#define TIMER_DATA_EXT "csv"
#define TIMER_DATA_EXT_LEN (strlen(TIMER_DATA_EXT))
#define TIMER_DATA_FMT "%Y_%m_%d-%H_%M_%S"


typedef struct st_run_timer
{
  ulonglong t_start,t_pause,t_delay;
  Run_leg* first_leg;
  Run_leg* cur_leg;
  Mem_pool mem_pool;
  const char* file_prefix;
  uint dir_len;
  FILE* fp;
  uint num_splits;
  uint num_legs;
} Run_timer;

extern Run_timer run_timer;

typedef enum {REVIEW_MODE_HTML,REVIEW_MODE_TEXT} Run_timer_review_mode;

int run_timer_init(Run_timer* t, const char* file_prefix);
int run_timer_start(Run_timer* t);
int run_timer_pause(Run_timer* t);
int run_timer_resume(Run_timer* t);
int run_timer_reset(Run_timer* t);
int run_timer_start_leg(Run_timer* t, double d);
int run_timer_split(Run_timer* t, double d);
int run_timer_init_from_workout(Run_timer* t, const char* file_prefix, const char* workout);
char* run_timer_review_info(Run_timer* t, Run_timer_review_mode mode);
char** run_timer_run_list(Run_timer* t, Mem_pool* pool,uint* num_entries);

typedef struct
{
  ulonglong t_total,t_leg,t_split;
  double d_last_leg,d_last_split;
} Run_info;

int run_timer_info(Run_timer* t, Run_info* info);

ulonglong run_timer_now(); 
ulonglong run_timer_running_time(Run_timer* t);
#endif