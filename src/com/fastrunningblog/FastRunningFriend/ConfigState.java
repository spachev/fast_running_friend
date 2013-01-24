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
