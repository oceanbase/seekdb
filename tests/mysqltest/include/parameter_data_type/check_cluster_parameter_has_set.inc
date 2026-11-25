let $done = 0;
while($done == 0)
{
  let $done = query_get_value(select count(name) cnt from oceanbase.__all_virtual_sys_parameter_stat where name = '$para_name', cnt, 1);
  if ($done == 0)
  {
    sleep 1;
  }
}
