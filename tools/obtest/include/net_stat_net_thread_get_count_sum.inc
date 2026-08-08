--echo net_stat_net_thread_get_count_sum.inc

let $sum_current_connect = 0;
let $sum_current_client_connect = 0;
let $num = $work_thread_num;

let $global_current_connect = 0;
let $global_current_connect = deploy_get_value(ob1.select show proxystat net_thread, global_current_connect_count, 1);
--echo global_current_connect:$global_current_connect;

let $global_current_do_accept = 0;
let $global_current_do_accept = deploy_get_value(ob1.select show proxystat net_thread, global_current_do_accept_count, 1);
--echo global_current_do_accept:$global_current_do_accept;

while($num >= 1)
{
  let $id = deploy_get_value(ob1.select show proxystat net_thread, net_thread_id, $num);
  --echo id:$id;

  let $count = 0;
  let $count = deploy_get_value(ob1.select show proxystat net_thread, current_connect_count, $num);
  --echo current_connect_count:$count;
  let $sum_current_connect = math($sum_current_connect + $count);

  let $count = 0;
  let $count = deploy_get_value(ob1.select show proxystat net_thread, current_client_connect_count, $num);
  --echo current_client_connect_count:$count;
  let $sum_current_client_connect = math($sum_current_client_connect + $count);

  let $count = deploy_get_value(ob1.select show proxystat net_thread, global_current_connect_count, $num);
  --echo global_current_connect:$count;
  if($count != $global_current_connect)
  {
    --echo global_current_connect is not equal $global_current_connect:$count;
    --exit
  }

  let $count = deploy_get_value(ob1.select show proxystat net_thread, global_current_do_accept_count, $num);
  --echo global_current_do_accept:$count;
  if($count != $global_current_do_accept)
  {
    --echo global_current_do_accept is not equal $global_current_do_accept:$count;
    --exit
  }

  let $count = deploy_get_value(ob1.select show proxystat net_thread, total_epoll_wait, $num);
#  --echo total_epoll_wait:$count;
  let $count = deploy_get_value(ob1.select show proxystat net_thread, last_epoll_size, $num);
#  --echo last_epoll_size:$count;
  let $count = deploy_get_value(ob1.select show proxystat net_thread, total_cop_lock_failure, $num);
#  --echo total_cop_lock_failure:$count;
  let $count = deploy_get_value(ob1.select show proxystat net_thread, total_lru_timeout_count, $num);
#  --echo total_lru_timeout_count:$count;
  let $count = deploy_get_value(ob1.select show proxystat net_thread, total_lru_timeout_time-sec, $num);
#  --echo total_lru_timeout_time-sec:$count;
  let $count = deploy_get_value(ob1.select show proxystat net_thread, total_set_default_inactive, $num);
#  --echo total_set_default_inactive:$count;
  let $count = deploy_get_value(ob1.select show proxystat net_thread, total_calls_read_from_net, $num);
#  --echo total_calls_read_from_net:$count;
  let $count = deploy_get_value(ob1.select show proxystat net_thread, total_calls_read, $num);
#  --echo total_calls_read:$count;
  let $count = deploy_get_value(ob1.select show proxystat net_thread, total_calls_read_nodata, $num);
#  --echo total_calls_read_nodata:$count;
  let $count = deploy_get_value(ob1.select show proxystat net_thread, total_read_bytes, $num);
#  --echo total_read_bytes:$count;
  let $count = deploy_get_value(ob1.select show proxystat net_thread, total_calls_write_to_net, $num);
#  --echo total_calls_write_to_net:$count;
  let $count = deploy_get_value(ob1.select show proxystat net_thread, total_calls_write, $num);
#  --echo total_calls_write:$count;
  let $count = deploy_get_value(ob1.select show proxystat net_thread, total_calls_write_nodata, $num);
#  --echo total_calls_write_nodata:$count;
  let $count = deploy_get_value(ob1.select show proxystat net_thread, total_write_bytes, $num);
#  --echo total_write_bytes:$count;

  dec $num;
}
#--echo sum_current_connect:$sum_current_connect;
#--echo sum_current_client_connect:$sum_current_client_connect;


