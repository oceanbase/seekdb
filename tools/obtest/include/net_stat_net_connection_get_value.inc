--echo net_stat_net_connection_get_value.inc

let $connect_count = deploy_get_value(ob1.select show proxystat net_thread, current_connect_count, 1);
--echo connect_count:$connect_count;

let $thread_id = deploy_get_value(ob1.select show proxystat net_thread, net_thread_id, 1);
--echo thread_id:$thread_id;

while($connect_count >= 1)
{
  --echo connect_count:$connect_count;
  let $value = deploy_get_value(ob1.select show proxystat net_connection, thread_id, $connect_count);
  if($value != $thread_id)
  {
    --echo thread_id is not equal $thread_id:$value;
    --exit
  }

  let $value = deploy_get_value(ob1.select show proxystat net_connection, server_ip, $connect_count);
#  --echo server_ip:$value;
  let $value = deploy_get_value(ob1.select show proxystat net_connection, bind_style, $connect_count);
  --echo bind_style:$value;
  let $value = deploy_get_value(ob1.select show proxystat net_connection, read_enabled, $connect_count);
  --echo read_enabled:$value;
  let $value = deploy_get_value(ob1.select show proxystat net_connection, read_nbytes, $connect_count);
#  --echo read_nbytes:$value;
  let $value = deploy_get_value(ob1.select show proxystat net_connection, read_ndone, $connect_count);
#  --echo read_ndone:$value;
  let $value = deploy_get_value(ob1.select show proxystat net_connection, write_enabled, $connect_count);
  --echo write_enabled:$value;
  let $value = deploy_get_value(ob1.select show proxystat net_connection, write_nbytes, $connect_count);
#  --echo write_nbytes:$value;
  let $value = deploy_get_value(ob1.select show proxystat net_connection, write_ndone, $connect_count);
#  --echo write_ndone:$value;
  let $value = deploy_get_value(ob1.select show proxystat net_connection, inactivity_in-sec, $connect_count);
#  --echo inactivity_in-sec:$value;
  let $value = deploy_get_value(ob1.select show proxystat net_connection, activity_in-sec, $connect_count);
#  --echo activity_in-sec:$value;
  let $value = deploy_get_value(ob1.select show proxystat net_connection, shutdown, $connect_count);
  --echo shutdown:$value;
  let $value = deploy_get_value(ob1.select show proxystat net_connection, comments, $connect_count);
  --echo comments:$value;

  dec $connect_count;
}
