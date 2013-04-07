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


typedef struct st_run_timer
{
  ulonglong t_start,t_pause,t_delay;
  Run_leg* first_leg;
  Run_leg* cur_leg;
  Mem_pool mem_pool;
  const char* file_prefix;
  FILE* fp;
} Run_timer;

extern Run_timer run_timer;

int run_timer_init(Run_timer* t, const char* file_prefix);
int run_timer_start(Run_timer* t);
int run_timer_pause(Run_timer* t);
int run_timer_resume(Run_timer* t);
int run_timer_reset(Run_timer* t);
int run_timer_start_leg(Run_timer* t, double d);
int run_timer_split(Run_timer* t, double d);

typedef struct
{
  ulonglong t_total,t_leg,t_split;
  double d_last_leg,d_last_split;
} Run_info;

int run_timer_info(Run_timer* t, Run_info* info);

ulonglong run_timer_now(); 
ulonglong run_timer_running_time(Run_timer* t);
#endif