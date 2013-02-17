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
import android.util.Log;

import java.util.Calendar;


class GPSCoord
{
  public double lon,lat,dist_to_prev;
  public float speed,bearing,accuracy;
  public long ts;
  public double angle_cos;
  public int angle_sign;
  public boolean angle_valid;
  public boolean good;
  static float[] results = new float[1];
  
  public GPSCoord()
  {
    reset();
  }
  
  public void init(Location loc, ConfigState cfg)
  {
    lon = loc.getLongitude();
    lat = loc.getLatitude();
    speed = loc.getSpeed();
    bearing = loc.getBearing();
    accuracy = loc.getAccuracy();
    dist_to_prev = 0.0;
    ts = FastRunningFriend.running_time(cfg);
    angle_valid = false;
    good = false;
  }
  
  public double get_dist(GPSCoord other)
  {
    Location.distanceBetween(lat,lon,other.lat,other.lon,results);
    return (double)results[0]/1609.34;
  }

  public void mark_point(GPSCoord prev, GPSCoord next, ConfigState cfg)
  {
    if (!angle_valid)
    {  
      good = false;
      return;
    }
    
    if (angle_cos > cfg.min_cos)
    {
      good = true;
      return;
    }
    
    boolean good_signs = (angle_sign == prev.angle_sign && angle_sign == next.angle_sign);
    
    if (!good_signs)
    {
      good = false;
      return;
    }
    
    if (prev.angle_cos > cfg.min_neighbor_cos && next.angle_cos > cfg.min_neighbor_cos)
    {
      good = true;
      return;
    }
    
    good = false;
  }

  public void set_angle(GPSCoord prev, GPSCoord next, double lat_cos)
  {
    double dx_prev,dy_prev,dx_next,dy_next,dot_p,sq_prod;
    dx_next = (next.lat - lat) * 1000.0;
    dx_prev = (lat - prev.lat) * 1000.0;
    dy_next = (next.lon - lon) * 1000.0 * lat_cos;
    dy_prev = (lon - prev.lon) * 1000.0 * lat_cos;
    dot_p = dx_next*dx_prev + dy_next * dy_prev;
    sq_prod = (dx_next*dx_next+dy_next*dy_next)*(dx_prev*dx_prev + dy_prev*dy_prev);
    
    if (sq_prod == 0.0)
    {
      return;
    }
    
    angle_valid = true;
    angle_cos = dot_p/Math.sqrt(sq_prod);
    double cross_p = dy_next * dx_prev - dy_prev * dx_next;
    
    if (cross_p > 0)
      angle_sign = -1;
    else if (cross_p < 0)
      angle_sign = 1;
    else 
      angle_sign = 0;
  }

  public void set_dist_to_prev(GPSCoord prev)
  {
    Location.distanceBetween(lat,lon,prev.lat,prev.lon,results);
    dist_to_prev = (double)results[0];
  }

  public void reset()
  {
    lon = lat = 0.0;
    ts = 0;
    angle_valid = false;
    good = false;
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

class DistInfo
{
  public double dist,pace_t;
  public long ts;
  public enum ConfidenceLevel { NORMAL,INITIAL,BAD_SIGNAL,SUSPECT_SIGNAL,SIGNAL_LOST,
          SIGNAL_SEARCH,
          SIGNAL_RECOVERY, 
          SIGNAL_RESTORED,
          SIGNAL_DISABLED
  };
  public ConfidenceLevel conf_level;
  public String debug_msg = "";
  
  public String get_status_str()
  {
    switch (conf_level)
    {
      case NORMAL:
        return "Good signal";
      case INITIAL:
        return "Initial guess";
      case BAD_SIGNAL:
        return "Bad signal";
      case SUSPECT_SIGNAL:
        return "Suspect signal";
      case SIGNAL_LOST:
        return "Signal lost";
      case SIGNAL_DISABLED:
        return "Signal disabled";
      case SIGNAL_SEARCH:
        return "Searching for signal";
      case SIGNAL_RECOVERY:
        return "Lost signal recovery";
      case SIGNAL_RESTORED:
        return "Signal restored";
      default:
        return "Unknown";
    }
  }
};

class GPSCoordBuffer
{
    public static final int COORD_BUF_SIZE = 1024;
    public static final int SAMPLE_SIZE = 8;
    protected GPSCoord[] buf = new GPSCoord[COORD_BUF_SIZE];
    protected int buf_start = 0, buf_end = 0, flush_ind = 0;
    long last_dist_time = 0;
    protected GPSCoord from_coord = new GPSCoord();
    protected GPSCoord to_coord = new GPSCoord();
    protected long points = 0, points_since_signal = 0;
    protected double total_dist = 0.0;
    protected int last_trusted_ind = 0;
    protected long last_trusted_ts = 0;
    protected double last_trusted_d = 0.0;
    protected double lat_cos = 0.0;
    protected boolean lat_cos_inited = false;
    protected int last_processed_ind = 0, last_good_ind = 0;
    protected double last_pace_t = 0.0;
    public DistInfo.ConfidenceLevel conf_level = DistInfo.ConfidenceLevel.INITIAL;
    
    protected ConfigState cfg = null;
    public static final String TAG = "FastRunningFriend";
    
    public native boolean init_data_dir(String dir_name);
    public native boolean open_data_file();
    public native boolean close_data_file();
    public native boolean flush();
    public native void debug_log(String msg);
    
    public GPSCoordBuffer(ConfigState cfg)
    {
      this.cfg = cfg;
      
      for (int i = 0; i < COORD_BUF_SIZE; i++)
        buf[i] = new GPSCoord();
    }
    
    public void reset()
    {
      flush();
      last_processed_ind = flush_ind = buf_start = buf_end = 0;
      last_trusted_d = 0.0;
      last_trusted_ind = 0;
      last_trusted_ts = 0;
      last_pace_t = 0.0;
      last_good_ind = 0;
      total_dist = 0.0;
      points_since_signal = points = 0;
    }
    
    public GPSCoord get_prev()
    {
      if (points < 2)
        return null;
      
      int ind = buf_end - 1;
      
      if (ind < 0)
        ind += COORD_BUF_SIZE;
      
      return buf[ind];
    }
    
    public void update_dist()
    {
      int i,update_end = buf_end - 2, start_ind = last_good_ind + 1;
      
      debug_log(String.format("total_dist=%f last_trusted_d=%f, start_ind=%d,update_end=%d",
                              total_dist, last_trusted_d, start_ind, update_end));
      
      if (update_end < 0)
        update_end += COORD_BUF_SIZE;
      
      if (start_ind == COORD_BUF_SIZE)
        start_ind = 0;
      
      for (i = start_ind; i != update_end; )
      {
        debug_log(String.format("Point %d at %d is %s", points, i, buf[i].good ? "good" : "bad"));
        
        if (buf[i].good)
        {
          last_trusted_d += buf[i].get_dist(buf[last_good_ind]);
          last_good_ind = i;
          debug_log(String.format("i=%d last_trusted_d=%f",i,last_trusted_d));
          
          if (last_trusted_d < cfg.min_d_last_trusted)
            return;
          
          long dt = buf[i].ts - buf[last_trusted_ind].ts;
          
          if (dt == 0)
            return;
          
          double pace_t = (double)dt/last_trusted_d;
          boolean good_pace = (
                       (last_pace_t == 0.0 && pace_t > cfg.top_pace_t) ||
                       Math.abs(last_pace_t/pace_t - 1.0) < cfg.max_pace_diff
                      );
          if (good_pace || last_trusted_d > cfg.max_d_last_trusted)
          {
            if (good_pace)
            {
              total_dist += last_trusted_d;
              last_trusted_d = 0.0;
              last_trusted_ts = buf[i].ts;
              last_pace_t = pace_t;
              last_trusted_ind = i;
              conf_level = DistInfo.ConfidenceLevel.NORMAL;
              debug_log("Trusted point at index " + i + ", good pace " + pace_t);
              return;
            }
            
            double dx_direct = buf[i].get_dist(buf[last_trusted_ind]);
            double direct_pace_t = 0.0;
            
            if (dx_direct > 0.0)
              direct_pace_t = dt/dx_direct;
            
            boolean use_direct = false;
            
            if (last_pace_t == 0.0)
            {
              if (pace_t < cfg.top_pace_t)
                use_direct = true;
            }
            else
            {
              if (Math.abs(last_pace_t - direct_pace_t) < 
                  Math.abs(last_pace_t - pace_t) && cfg.top_pace_t > pace_t)
                use_direct = true;
            }
            
            if (use_direct)
            {  
              total_dist += dx_direct;
              last_pace_t = direct_pace_t;
              conf_level = DistInfo.ConfidenceLevel.BAD_SIGNAL;
              debug_log("BAD_SIGNAL: Trusted point at index " + i 
                + ", direct pace " + direct_pace_t + ", dx_direct=" + dx_direct );
            }  
            else
            {  
              total_dist += last_trusted_d;
              last_pace_t = pace_t;
              conf_level = DistInfo.ConfidenceLevel.SUSPECT_SIGNAL;
              debug_log("SUSPECT_SIGNAL: Trusted point at index " + i 
                + ", integrated pace " + pace_t );
            }  
            
            last_trusted_ind = i;
            last_trusted_d = 0.0;
            
            last_trusted_ts = buf[i].ts;
            return;
          }
          
          break;
        }
        
        if (++i == COORD_BUF_SIZE)
          i = 0;
      }
    }
    
    public void push(Location coord)
    {
      if (buf_end < COORD_BUF_SIZE)
      {
        int save_buf_end = buf_end;
        buf[buf_end++].init(coord,cfg);
        
        if (buf_end == COORD_BUF_SIZE)
        {
          buf_end = 0;
          buf_start++;
        }
        else if (buf_end <= buf_start)
          buf_start++;
         
        if (buf_start == COORD_BUF_SIZE)
            buf_start = 0;

        if (!lat_cos_inited)
        {  
          lat_cos = Math.cos(buf[save_buf_end].lat*Math.PI/180.0);
          lat_cos_inited = true;
        }
          
        if (points > 0)
        {  
          int prev_ind = save_buf_end - 1;
          
          if (prev_ind < 0)
            prev_ind += COORD_BUF_SIZE;
          
          //buf[save_buf_end].set_dist_to_prev(buf[prev_ind]);
          if (points > 1)
          {
            int p_prev_ind = prev_ind - 1;
            
            if (p_prev_ind < 0)
              p_prev_ind += COORD_BUF_SIZE;
            
            buf[prev_ind].set_angle(buf[p_prev_ind],buf[save_buf_end],lat_cos);
            
            if (points > 2)
            {
              int pp_prev_ind = p_prev_ind - 1;
              
              if (pp_prev_ind < 0)
                pp_prev_ind += COORD_BUF_SIZE;
              
              buf[p_prev_ind].mark_point(buf[pp_prev_ind],buf[prev_ind], cfg);
            }
          }
        }
          
        points++;
        points_since_signal++;
        flush();
        
        switch(conf_level)
        {
          case SIGNAL_LOST:
            if (points_since_signal > 1)
            {  
              handle_no_signal();
              conf_level = DistInfo.ConfidenceLevel.SIGNAL_RESTORED;
            }
            break;
          case SIGNAL_SEARCH:
            conf_level = DistInfo.ConfidenceLevel.INITIAL;
            break;
          default:
            break;
        }
        
        if (points_since_signal >= 3)
          update_dist();
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
    
    public void handle_no_signal()
    {
      int cur_ind = buf_end - 1;
      
      if (cur_ind < 0)
        cur_ind += COORD_BUF_SIZE;
      
      last_trusted_ind = cur_ind;
      last_trusted_d = 0.0;
      last_good_ind = cur_ind;
      points_since_signal = 1;
      long dt = buf[cur_ind].ts - last_trusted_ts;
      double pace_t = (last_pace_t > 0.0) ? last_pace_t : cfg.start_pace_t;
      double dx = (double)dt/pace_t;
      
      if (dx > 0.0)
        total_dist += dx;
      
      last_trusted_ts = buf[cur_ind].ts;
    }
    
    public void get_dist_info(DistInfo di, long now_ts) // need now_ts to avoid checking time twice
    {
      di.dist = total_dist;
      di.pace_t = (last_pace_t > 0.0) ? last_pace_t : cfg.start_pace_t;
      di.ts = last_trusted_ts;
      di.conf_level = conf_level;      
      long dt = now_ts - di.ts; // if last_trusted_ts is 0 it still works
      
      if (dt > cfg.max_t_no_signal && points_since_signal > 0)
      {
        di.conf_level = conf_level = DistInfo.ConfidenceLevel.SIGNAL_LOST;
        debug_log("Signal lost");
      }
      
      if (dt > 0 && di.pace_t > 0.0)
      {  
        di.dist += (double)dt/di.pace_t;
      }  
    }
    
    /*
    public double get_dist_old()
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
    */
};

public class FastRunningFriend extends Activity implements LocationListener
{
    LocationManager lm;
    TextView time_tv,dist_tv,pace_tv,status_tv,time_of_day_tv,battery_tv;
    public static final String PREFS_NAME = "FastRunningFriendPrefs";

    Handler timer_h = new Handler();
    Handler tod_timer_h = new Handler();
    ConfigState cfg = new ConfigState();
    GPSCoordBuffer coord_buf = new GPSCoordBuffer(cfg);
    protected long dist_uppdate_ts = 0;
    protected DistInfo dist_info = new DistInfo();
    protected enum TimerState {RUNNING,PAUSED,INITIAL;}
    protected enum TimerAction {START,SPLIT,PAUSE,RESET,RESUME,START_GPS,STOP_GPS,
      IGNORE,PASS;}
    protected enum ButtonCode {START,SPLIT,IGNORE,BACK;};
    
    TimerState timer_state = TimerState.INITIAL; 
    boolean gps_running = false;
    Calendar cal = Calendar.getInstance();
    protected long dist_update_ts = 0;
    public native void set_system_time(long t_ms);
    
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
          
          if (h > 12)
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
         final long run_ts = now - cfg.start_time;
         post_time(run_ts,time_tv);
         timer_h.postAtTime(this,SystemClock.uptimeMillis()+100);
         
         if (dist_update_ts == 0 || now - dist_update_ts > cfg.dist_update_interval)
         {
           coord_buf.get_dist_info(dist_info,run_ts);
           show_dist_info(dist_info);
           dist_update_ts = now;
         }
       }
     };
     
     static
     {
       System.loadLibrary("fast_running_friend");  
     }

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
        
        update_tod_task.run(); // do one run so we do not wait a second before the time_now
                               // appears
        start_tod_timer();
        IntentFilter filter = new IntentFilter(Intent.ACTION_BATTERY_CHANGED);
        registerReceiver(battery_receiver, filter);
        
        if (!coord_buf.init_data_dir(cfg.data_dir))
        {
          update_status("Directory " + cfg.data_dir + 
           " does not exist and could not be created, will not save data");
        }
    }
    
    public void test_gps_bug()
    {
      /*
      40.314353,-111.655104,937778340,3.000639,16.000000,9.487171,3.280000
      40.314385,-111.655104,937779341,2950.528076,19.000000,9.487171,3.230000
      */
      GPSCoord p1 = new GPSCoord();
      GPSCoord p2 = new GPSCoord();
      p1.lat = 40.314353;
      p1.lon = -111.655104;
      p2.lat = 40.314385;
      p2.lon = -111.655104;
      double d1 = p1.get_dist(p2);
      double d2 = p2.get_dist(p1);
      update_status(String.format("Bug test: d1=%f,d2=%f",d1,d2));
    }
    
    public void start_tod_timer()
    {
       tod_timer_h.removeCallbacks(update_tod_task);
       tod_timer_h.postDelayed(update_tod_task,1000);
    }
    
    public void onLocationChanged(Location arg0) 
    {
      if (timer_state != TimerState.RUNNING)
      {  
         if (arg0 != null)
         {  
           update_status(String.format("Have signal at %.3f,%.3f",
                                       arg0.getLatitude(),
                                       arg0.getLongitude()));
                                       
           set_system_time(arg0.getTime());                            
         }
         return;
      }
      
      coord_buf.push(arg0);
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
        Location loc = lm.getLastKnownLocation(LocationManager.GPS_PROVIDER);
        lm.requestLocationUpdates(LocationManager.GPS_PROVIDER, 
            500, 1, this);
        String status_msg = "Searching for GPS signal";
        
        if (loc != null)
          status_msg += String.format(", last %.3f,%.3f",loc.getLatitude(),
                                      loc.getLongitude());
        update_status(status_msg);    
        gps_running = true;    
        coord_buf.conf_level = DistInfo.ConfidenceLevel.SIGNAL_SEARCH;
   }
    
    protected void stop_gps()
    {
      lm.removeUpdates(this);
      update_status("Stopped GPS updates");    
      gps_running = false;
      coord_buf.conf_level = DistInfo.ConfidenceLevel.SIGNAL_DISABLED;
    }

    protected void pause_timer()
    {
      cfg.save_time();
      cfg.pause_time = get_start_time();
      timer_state = TimerState.PAUSED;
      suspend_timer_display();
      post_time(cfg.pause_time-cfg.start_time,time_tv);
      post_pace(0);
      coord_buf.debug_log("Timer paused");
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
      coord_buf.reset();
      timer_state = TimerState.INITIAL;
      post_time(0,time_tv);
      post_pace(0);
      post_dist(0.0);
      suspend_timer_display();
      stop_gps();
      dist_update_ts = 0;
      coord_buf.debug_log("Timer reset");
      coord_buf.close_data_file();
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
       timer_state = TimerState.RUNNING;
       
       if (!coord_buf.open_data_file())
         update_status("Error opening GPS data file, cannot save data");
       
       coord_buf.debug_log("Timer started");
       resume_timer_display();
   }   
    
    void post_pace(int pace)
    {
      int min = pace / 60;
      int sec = pace % 60;
      pace_tv.setText(String.format("%02d:%02d", min, sec));
    }
    
    public void show_dist_info(DistInfo di)
    {
      post_dist(di.dist);
      post_pace((int)di.pace_t/1000);
      update_status("GPS: " + di.get_status_str());
    }
        
    void post_dist(double total_dist)
    {
      dist_tv.setText(String.format("%.3f", total_dist));
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
    
    public static long running_time(ConfigState cfg)
    {
      return time_now() - cfg.start_time;
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
      coord_buf.debug_log("Timer resumed");
    }     
   
    protected void resume_timer_display()
    {
       timer_h.removeCallbacks(update_time_task);
       
       if (!gps_running)
         start_gps();
       
       timer_h.postDelayed(update_time_task,100);
    }
    
}
