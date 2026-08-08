let $user_table_leader_count = deploy_get_value(ob1.select select count(*) c from __all_virtual_ls_meta_table where svr_ip = '$ip' and svr_port = $port and role = 1, c, 1);
let $sys_table_leader_count = deploy_get_value(ob1.select select count(*) c from __all_virtual_core_meta_table where svr_ip = '$ip' and svr_port = $port and role = 1, c, 1);
let $server_leader_count = $user_table_leader_count:$sys_table_leader_count;
if ($server_leader_count == 0:0)
{
  echo server switches leaders out;
}
