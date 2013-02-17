package com.fastrunningblog.FastRunningFriend;

import android.content.SharedPreferences;
import android.app.Activity;
import android.os.Bundle;


class ConfigState
{
    long start_time = 0, pause_time = 0, resume_time = 0;
    long max_dist_delay = 3000; 
    boolean gps_guess_mode = true;
    int min_running_pace = 16*60;
    public double min_cos = Math.cos(10.0 * Math.PI/180.0);
    public double min_neighbor_cos = Math.cos(5.0 * Math.PI/180.0);
    public double min_d_last_trusted = 0.08, max_d_last_trusted = 0.15,
      max_pace_diff = 0.07, top_pace_t = 300000.0 /* 5:00 */, start_pace_t = 480000.0 /* 8:00 */;
    public long max_t_no_signal = 120000;  
    public long dist_update_interval = 500;
    public int expire_files_days = 7;
    
    String data_dir = "/mnt/sdcard/FastRunningFriend";
    
    public void saveToBundle(Bundle b)
    {
      b.putLong("start_time", start_time);
      b.putLong("pause_time", pause_time);
      b.putLong("resume_time", resume_time);
    }
    
    public void saveToPrefs(Activity a)
    {
      SharedPreferences.Editor e = 
        a.getSharedPreferences(FastRunningFriend.PREFS_NAME, 0).edit();
      e.putLong("start_time", start_time);
      e.putLong("pause_time", pause_time);
      e.putLong("resume_time", resume_time);
      e.commit();
    }
    
    public void initFromBundle(Bundle b)
    {
      if (b != null)
      {
        start_time = b.getLong("start_time");
        pause_time = b.getLong("pause_time");
        resume_time = b.getLong("resume_time");
      }  
    }
    
    public void initFromPrefs(Activity a)
    {
      try
      {
       SharedPreferences p = 
         a.getSharedPreferences(FastRunningFriend.PREFS_NAME, 0);
        start_time = p.getLong("start_time", 0);
        pause_time = p.getLong("pause_time", 0);
        resume_time = p.getLong("resume_time",0);
      }
      catch (Exception e)
      {
      }  
    }
    
    public void save_time()
    {
    }
    
};
