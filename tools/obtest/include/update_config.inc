--disable_query_log

let $table1 = ob_all_proxy_kv_table;
let $table2 = ob_all_proxy;
let $table3 = ob_all_proxy_config;

eval update $table1 set value = cast((cast(value as signed) + 1) as char) where name = "ob_proxy_config_version";
eval update $table3 set value = '$config_value1' where name = "$config_name1";
eval update $table3 set value = '$config_value2' where name = "$config_name2";

if ($update_proxy0 == 1)
{
  let $ip = $ip0;
  let $port = $port0;
  connect (conn_proxy,$ip,admin@obproxy#MetaDataBase,admin,test,$port);
  connection conn_proxy;
  eval update $table2 set reload_config = 'reloading' where proxy_ip = '$ip' and proxy_port = '$port';
  source include/check_reload_status.inc;
  source include/check_proxy_config.inc;
}

if ($update_proxy1 == 1)
{
  let $ip = $ip1;
  let $port = $port1;
  connect (conn_proxy,$ip,admin@obproxy#MetaDataBase,admin,test,$port);
  connection conn_proxy;
  eval update $table2 set reload_config = 'reloading' where proxy_ip = '$ip' and proxy_port = '$port';
  source include/check_reload_status.inc;
  source include/check_proxy_config.inc;
}



let $reload0 = query_get_value(select reload_config v from ob_all_proxy where proxy_ip = '$ip0' and proxy_port = '$port0', v, 1);
let $reload1 = query_get_value(select reload_config v from ob_all_proxy where proxy_ip = '$ip1' and proxy_port = '$port1', v, 1);
--echo proxy0's current reload status $reload0
--echo proxy1's current reload status $reload1
