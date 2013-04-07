#ifndef TIMER_JNI_H
#define TIMER_JNI_H

typedef struct 
{
  jfieldID t_total_id,t_leg_id,t_split_id,d_last_split_id,d_last_leg_id;
  jclass info_class;
} Run_info_fields;

extern Run_info_fields run_info_fields;

#endif