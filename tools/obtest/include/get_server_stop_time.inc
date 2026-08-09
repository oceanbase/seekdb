let $stop_time = deploy_get_value(ob1.select select stop_time from oceanbase.__all_server where svr_ip = '$ip' and svr_port = $port, stop_time, 1);
if ($stop_time > 0)
{
  echo server is stopped;
}
if ($stop_time == 0)
{
  echo server is started;
}
