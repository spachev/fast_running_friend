package com.fastrunningblog.FastRunningFriend;

import android.app.Activity;
import android.os.Bundle;

import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.BroadcastReceiver;

import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.widget.TextView;
import android.view.KeyEvent;
import android.view.WindowManager;
import android.os.Handler;
import android.os.SystemClock;
import java.util.Calendar;


class GPSCoord
{
  public double lon,lat;
  public long ts;
  static float[] results = new float[1];
  
  public GPSCoord()
  {
    reset();
  }
  
  public void init(Location loc)
  {
    lon = loc.getLongitude();
    lat = loc.getLatitude();
    ts = FastRunningFriend.time_now();
  }
  
  public double get_dist(GPSCoord other)
  {
    Location.distanceBetween(lat,lon,other.lat,other.lon,results);
    return (double)results[0];
  }

  public void reset()
  {
    lon = lat = 0.0;
    ts = 0;
  }
    
  public void add(GPSCoord other)
  {
    lon += other.lon;
    lat += other.lat;
    ts += other.ts;
  }
  
  public void div(int n)
  {
    lon /= n;
    lat /= n;
    ts /= n;
  }
};

class GPSCoordBuffer
{
    public static final int COORD_BUF_SIZE = 1024;
    public static final int SAMPLE_SIZE = 8;
    protected GPSCoord[] buf = new GPSCoord[COORD_BUF_SIZE];
    protected int buf_start = 0, buf_end = 0;
    long last_dist_time = 0;
    protected GPSCoord from_coord = new GPSCoord();
    protected GPSCoord to_coord = new GPSCoord();
    protected long points = 0;
    
    public GPSCoordBuffer()
    {
      for (int i = 0; i < COORD_BUF_SIZE; i++)
        buf[i] = new GPSCoord();
    }
    
    public void reset()
    {
      buf_start = buf_end = 0;
      points = 0;
    }
    
    public void push(Location coord)
    {
      if (buf_end < COORD_BUF_SIZE)
      {
        buf[buf_end++].init(coord);
        
        if (buf_end == COORD_BUF_SIZE)
        {
          buf_end = 0;
          buf_start++;
        }
        else if (buf_end <= buf_start)
          buf_start++;
         
        if (buf_start == COORD_BUF_SIZE)
            buf_start = 0;
        
        points++;    
      }  
    }
  
    public long get_last_ts()
    {
      if (points == 0)
        return 0;
      
      int last_ind = buf_end - 1;
      
      if (last_ind < 0)
        last_ind += COORD_BUF_SIZE;
        
      return buf[last_ind].ts;    
    } 
    
    public double get_dist()
    {
      if (points < SAMPLE_SIZE)
        return 0.0;
        
      int from_ind = buf_end - SAMPLE_SIZE ;
      int mid_ind = buf_end - SAMPLE_SIZE/2;
      int to_ind = buf_end ;
      
      if (from_ind < 0)
       from_ind += COORD_BUF_SIZE;
       
      if (to_ind < 0)
       to_ind += COORD_BUF_SIZE;
       
      if (mid_ind < 0)
       mid_ind += COORD_BUF_SIZE;
      
      from_coord.reset();
      to_coord.reset();
      
      GPSCoord col_coord = from_coord; 
      
      for (int i = from_ind; i != to_ind; )
      {
        col_coord.add(buf[i]);  
        i++;
        if (i == COORD_BUF_SIZE)
          i = 0;
          
        if (i == mid_ind)
          col_coord = to_coord;  
          
      }
      
      from_coord.div(SAMPLE_SIZE/2);
      to_coord.div(SAMPLE_SIZE/2);
      
      last_dist_time = (to_coord.ts - from_coord.ts) /
         (SAMPLE_SIZE/2);         
      return from_coord.get_dist(to_coord)/(SAMPLE_SIZE/2);
    }
    
    long get_last_dist_time() { return last_dist_time; }
};

public class FastRunningFriend extends Activity implements LocationListener
{
    LocationManager lm;
    TextView time_tv,dist_tv,pace_tv,status_tv,time_of_day_tv,battery_tv;
    public static final String PREFS_NAME = "FastRunningFriendPrefs";

    Handler timer_h = new Handler();
    Handler tod_timer_h = new Handler();
    ConfigState cfg = new ConfigState();
    GPSCoordBuffer coord_buf = new GPSCoordBuffer();
    double total_dist = 0.0;
    protected enum TimerState {RUNNING,PAUSED,INITIAL;}
    protected enum TimerAction {START,SPLIT,PAUSE,RESET,RESUME,START_GPS,STOP_GPS,
      IGNORE,PASS;}
    protected enum ButtonCode {START,SPLIT,IGNORE,BACK;};
    
    TimerState timer_state = TimerState.INITIAL; 
    boolean gps_running = false;
    Calendar cal = Calendar.getInstance();
    
    BroadcastReceiver battery_receiver = new BroadcastReceiver() {
        int scale = -1;
        int level = -1;
        int voltage = -1;
        int temp = -1;
        
        @Override
        public void onReceive(Context context, Intent intent) 
        {
            if (battery_tv != null)
            {  
              level = intent.getIntExtra("level", -1);
              scale = intent.getIntExtra("scale", -1);
              battery_tv.setText(String.format("Battery %d%%", level*100/scale));
            }  
        }
    };
    
    Runnable update_tod_task = new Runnable()
    {
      public void run()
      {
        if (time_of_day_tv != null)
        {
          cal.setTimeInMillis(System.currentTimeMillis());
          int h = cal.get(Calendar.HOUR_OF_DAY);
          final boolean is_am = (h < 12);
          
          if (!is_am)
            h -= 12;
          
          time_of_day_tv.setText(String.format("%02d:%02d:%02d%s %02d/%02d/%04d",
                                                h,
                                                cal.get(Calendar.MINUTE),
                                                cal.get(Calendar.SECOND),
                                                is_am ? "am":"pm",
                                                cal.get(Calendar.MONTH) + 1,
                                                cal.get(Calendar.DAY_OF_MONTH),
                                                cal.get(Calendar.YEAR)
                                               ));
        }
        tod_timer_h.postAtTime(this,SystemClock.uptimeMillis()+1000);
      }
    };
    
    Runnable update_time_task = new Runnable()
     {
       public void run()
       {
         final long now = time_now();
         post_time(now - cfg.start_time,time_tv);
         timer_h.postAtTime(this,SystemClock.uptimeMillis()+100);
         
         if (!cfg.gps_guess_mode)
         {
           final long last_ts = coord_buf.get_last_ts();
           
           if (last_ts != 0 || now - last_ts > cfg.max_dist_delay)
           {
             cfg.gps_guess_mode = true;
             update_status(String.format("No signal for %.3f s", 
                                           (float)(now - last_ts)/1000.0));
           }
         }
       }
     };

    /** Called when the activity is first created. */
    @Override
    public void onCreate(Bundle b)
    {
        super.onCreate(b);
        
        if (b != null)
          cfg.initFromBundle(b);
        else
          cfg.initFromPrefs(this);

        setContentView(R.layout.main);
        dist_tv = (TextView) findViewById(R.id.dist_tv);
        time_tv = (TextView) findViewById(R.id.time_tv);
        pace_tv = (TextView) findViewById(R.id.pace_tv);
        status_tv = (TextView) findViewById(R.id.status_tv);
        time_of_day_tv = (TextView) findViewById(R.id.time_of_day_tv);
        battery_tv = (TextView) findViewById(R.id.battery_tv);
        lm = (LocationManager) getSystemService(Context.LOCATION_SERVICE);
        
        getWindow().addFlags(
          WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
       
        if (cfg.start_time != 0)
          resume_timer_display();
        
        start_tod_timer();
        IntentFilter filter = new IntentFilter(Intent.ACTION_BATTERY_CHANGED);
        registerReceiver(battery_receiver, filter);
    }
    
    public void start_tod_timer()
    {
       tod_timer_h.removeCallbacks(update_tod_task);
       tod_timer_h.postDelayed(update_tod_task,1000);
    }
    
    public void onLocationChanged(Location arg0) 
    {
      update_status(String.format("Have signal at %.3f,%.3f",
                                     arg0.getLatitude(),
                                     arg0.getLongitude()));
      if (timer_state != TimerState.RUNNING)
      {  
        return;
      }
      
      coord_buf.push(arg0);
      double dist = coord_buf.get_dist(); 
      long t = coord_buf.get_last_dist_time();
      // milliseconds per meters is the same as seconds per kilometer
      int pace = (dist > 0.0) ? (int)((t/dist)*1.60934) : 0;
      
      if (pace > 0 && pace < cfg.min_running_pace)
      {  
        total_dist += dist;
        post_pace(pace);
        post_dist();
      }
      else
        update_status("Standing or walking");
    }
    
    public void onProviderDisabled(String arg0) 
    {
    }
    
    public void onProviderEnabled(String arg0) 
    {
    }
    
    public void onStatusChanged(String arg0, int arg1, Bundle arg2) 
    {
    }
    
    public void update_status(String msg)
    {
      status_tv.setText(msg);
    }
    
    @Override
    public boolean onKeyDown(int key, KeyEvent ev)
    {
      
      //dist_tv.setText("key=" + key + ", scan_code = " + ev.getScanCode());
      switch (get_timer_action(ev))
      {
        case START:
          start_timer();
          return false;
        case RESUME:
          resume_timer();
          return false;
        case RESET:
          reset_timer();
          return false;
        case START_GPS:
          start_gps();
          return false;
        case STOP_GPS:
          stop_gps();
          return false;
        case SPLIT:
          split_timer();
          return false;
        case PAUSE:
          pause_timer();
          return false;
        case IGNORE:
          return false;
        case PASS: 
        default:   
          break;
      }
      
      return super.onKeyDown(key,ev);
    }

    protected ButtonCode get_button_code(KeyEvent ev)
    {
      switch (ev.getKeyCode())
      {
        case KeyEvent.KEYCODE_BACK:
          return ButtonCode.BACK;
        case KeyEvent.KEYCODE_VOLUME_UP:
          return ButtonCode.START;
        case KeyEvent.KEYCODE_VOLUME_DOWN:
          return ButtonCode.SPLIT;
        default:
          break;
      }
      switch (ev.getScanCode())
      {
        case 220:
          return ButtonCode.START;
        case 387:
          return ButtonCode.SPLIT; 
        default:
          return ButtonCode.IGNORE;  
      }
    }
    
    protected TimerAction get_timer_action(KeyEvent ev)
    {
      switch (get_button_code(ev)) 
      {
        case START:
          switch(timer_state)
          {
            case INITIAL:
               return TimerAction.START;
            case PAUSED:
               return TimerAction.RESUME;   
            case RUNNING:
               return TimerAction.PAUSE;  
          }  
          break;
        case SPLIT:
          switch (timer_state)
          {
            case INITIAL:
              return gps_running ? TimerAction.STOP_GPS : TimerAction.START_GPS;
            case RUNNING:
              return TimerAction.SPLIT;  
            case PAUSED:
              return TimerAction.RESET;  
          }
          break;
        
        case BACK:
          return timer_state == TimerState.INITIAL ? 
            TimerAction.PASS : TimerAction.IGNORE;
        case IGNORE:
        default:
          break;
      }
      
      return TimerAction.PASS;  
    }

    protected long get_start_time()
    {
      return time_now();
    }
    
    protected void suspend_timer_display()
    {
      timer_h.removeCallbacks(update_time_task);
    }

    protected void start_gps()
    {
        lm.requestLocationUpdates(LocationManager.GPS_PROVIDER, 
            500, 1, this);
        update_status("Searching for GPS signal");    
        gps_running = true;    
   }
    
    protected void stop_gps()
    {
      lm.removeUpdates(this);
      update_status("Stopped GPS updates");    
      gps_running = false;
    }

    protected void pause_timer()
    {
      cfg.save_time();
      cfg.pause_time = get_start_time();
      timer_state = TimerState.PAUSED;
      suspend_timer_display();
      post_time(cfg.pause_time-cfg.start_time,time_tv);
      post_pace(0);
    }   
    
    public void onDestroy()
    {
      stop_gps();
      unregisterReceiver(battery_receiver);
      super.onDestroy();
    }
      
    protected void reset_timer()
    {
      cfg.save_time();
      cfg.pause_time = cfg.start_time ;
      cfg.resume_time = 0;
      total_dist = 0.0;
      coord_buf.reset();
      timer_state = TimerState.INITIAL;
      post_time(0,time_tv);
      post_pace(0);
      post_dist();
      suspend_timer_display();
      stop_gps();
    }     


    @Override
    public void onRestoreInstanceState(Bundle b) 
    {
      super.onRestoreInstanceState(b);
      
      if (b != null)
        cfg.initFromBundle(b);
    }  
    
    @Override
    public void onSaveInstanceState(Bundle b) 
    {
      cfg.saveToBundle(b);
      super.onSaveInstanceState(b);  
    }


    protected void start_timer()
    {
       cfg.save_time();
       cfg.resume_time = cfg.start_time = get_start_time();
       cfg.pause_time = 0;
       total_dist = 0.0;
       timer_state = TimerState.RUNNING;
       resume_timer_display();
   }   
    
    void post_pace(int pace)
    {
      int min = pace / 60;
      int sec = pace % 60;
      pace_tv.setText(String.format("%02d:%02d", min, sec));
    }
        
    void post_dist()
    {
      dist_tv.setText(String.format("%.3f", total_dist/1609.34));
    }
    
    void post_time(long ts, TextView tv)
    {
         long ss = ts/1000;
         long mm = ss / 60;
         long hh = mm / 60;
         long fract = (ts/100) % 10;
         ss = ss % 60;
         mm = mm % 60;
         tv.setText(String.format("%02d:%02d:%02d.%d",
           hh, mm, ss, fract));
           
    }
    
    public static long time_now()
    {
      return SystemClock.elapsedRealtime();
    }
  
    protected void split_timer()
    {
    }
    
    protected void resume_timer()
    {
      cfg.save_time();
      cfg.resume_time = get_start_time();
      cfg.start_time += cfg.resume_time - cfg.pause_time;
      cfg.pause_time = 0;
      timer_state = TimerState.RUNNING;
      resume_timer_display();
    }     
   
    protected void resume_timer_display()
    {
       timer_h.removeCallbacks(update_time_task);
       start_gps();
       timer_h.postDelayed(update_time_task,100);
    }
    
}
