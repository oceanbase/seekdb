let $interval = 61;
while ($interval > 0)
{
  let $reload_status = query_get_value(select reload_config v from ob_all_proxy where proxy_ip = '$ip' and proxy_port = '$port', v, 1);
  if (reload success == $reload_status)
  {
    let $interval = 0;
    --echo proxy reload config successfully
  }
  if (reload failed == $reload_status)
  {
    let $interval = 0;
  }
  dec $interval;
  sleep 1;
}
