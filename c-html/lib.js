<script>
function time_to_sec(t)
{
  var t_arr = t.split(':');
  var i = 0;
  var t = 0;
  if (t_arr.length < 2 || t_arr.length > 3) return NaN;
  if (t_arr.length == 3)
  {
    t = 3600 * t_arr[0];
    i++;
  }

  t += t_arr[i] * 60 + Number(t_arr[i+1]);
  return t;
}

function sec_to_time(t_sec)
{
  var ss, mm, hh;
  ss = t_sec % 60;
  t_sec = ((t_sec - ss) / 60);
  ss = ss.toFixed(1);
  mm = t_sec % 60;
  // we could get here because of rounding
  if (ss >= 60)
  {
    mm++;
    ss -= 60;
  }

  if (ss < 10)
    ss = '0' + ss.toString();
  else
    ss = ss.toString();


  if (mm < 10)
    mm = '0' + mm.toString();
  else
    mm = mm.toString();

  hh = (t_sec - mm) / 60;
  var hh_str = (hh > 0) ?  hh.toString() + ':' : '';
  return  hh_str + mm + ':' + ss;
}

</script>
