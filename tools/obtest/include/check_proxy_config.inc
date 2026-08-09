let $updated_value1 = query_get_value(show proxyconfig like "$config_name1", value, 1);
let $updated_value2 = query_get_value(show proxyconfig like "$config_name2", value, 1);
let $succ_count = 0;

if ($updated_value1 == $config_value1)
{
  --echo $config_name1 updated successfully
  inc $succ_count;
}
if ($updated_value2 == $config_value2)
{
  --echo $config_name2 updated successfully
  inc $succ_count;
}
if ($succ_count == 2)
{
  --echo proxy updates config successfully
}
