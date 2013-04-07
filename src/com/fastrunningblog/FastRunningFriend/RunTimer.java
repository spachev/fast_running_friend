package com.fastrunningblog.FastRunningFriend;

public class RunTimer
{
  public static native boolean init(String file_prefix);
  public static native boolean start();
  public static native long now();  
  public static native boolean pause();
  public static native boolean resume();
  public static native boolean reset();
  public static native boolean start_leg(double d);
  public static native boolean split(double d);
  public static native boolean get_run_info(RunInfo i);
};