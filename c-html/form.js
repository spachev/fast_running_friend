<style>
.comment
{
  text-decoration: underline;
  color: #33c033;
}
.comment:hover
{
  color: #c04433;
  background-color: #c0c0c0;
}
</style>
<script>

function set_zone(leg_num,split_num,zone)
{
  var el = document.getElementsByName('z_' + leg_num + '_' + split_num);

  if (!el)
    return;

  var i;

  for (i = 0; i < el[0].options.length; i++)
  {
    if (el[0].options[i].value == zone)
    {
      el[0].selectedIndex = i;
      break;
    }
  }
}

function open_comment(leg_num,split_num)
{
  var el = document.getElementById('sc_' + leg_num + '_' + split_num);

  if (el)
  {
    el.style.display = 'none';
  }

  el = document.getElementById('c_' + leg_num + '_' + split_num);

  if (el)
  {
    el.style.display = 'inline';
  }
}
function close_comment(leg_num,split_num)
{
  var el = document.getElementById('c_' + leg_num + '_' + split_num);

  if (el)
  {
    el.style.display = 'none';
  }

  el = document.getElementById('sc_' + leg_num + '_' + split_num);

  if (el)
  {
    el.style.display = 'inline';
    el.innerHTML = (leg_num == 0 && split_num == 0) ? 'Edit Workout Comment' : 'Edit Comment';
  }
}

function update_leg(leg_num)
{
  var i,t_leg = 0,d_leg = 0.0;
  var in_els = document.getElementsByTagName("input");
  var t_prefix = "t_" + leg_num + "_";
  var d_prefix = "d_" + leg_num + "_";
  for (i = 0; i < in_els.length; i++)
  {
    if (in_els[i].name.substr(0,t_prefix.length) == t_prefix)
    {
      t_leg += time_to_sec(in_els[i].value)
    }
    else if(in_els[i].name.substr(0,d_prefix.length) == d_prefix)
    {
      d_leg += Number(in_els[i].value);
    }
  }

  var t_span_el,d_span_el;

  t_span_el = document.getElementById("t_" + leg_num);
  d_span_el = document.getElementById("d_" + leg_num);

  if (!t_span_el || !d_span_el)
    return;

  t_span_el.innerHTML = sec_to_time(t_leg);
  d_span_el.innerHTML = d_leg.toFixed(3);
}

</script>
