let $interval = 61;
let $register_num = 0;
while ($interval > 0)
{
  sleep 1;
  let $proxy0_num = query_get_value(select count(*) num from ob_all_proxy where proxy_ip = '$ip0' and proxy_port = $port0, num, 1);
  let $proxy1_num = query_get_value(select count(*) num from ob_all_proxy where proxy_ip = '$ip1' and proxy_port = $port1, num, 1);
  if ($proxy0_num == 1)
  {
    inc $register_num;
  }
  if ($proxy1_num == 1)
  {
    inc $register_num;
  }
  if ($register_num == 2)
  {
    let $interval = 0;
  }
  if ($register_num < 2)
  {
    let $register_num = 0;
  }
  dec $interval;
}

if ($intreval == 0)
{
  --echo proxy has not been registered into ob_all_proxy!!!
  --exit
}
