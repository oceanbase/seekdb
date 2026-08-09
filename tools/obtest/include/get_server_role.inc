let $server_user_table_leader_count = 0;
let $part_num = deploy_get_value(ob1.select select part_num from __all_virtual_table where tenant_id = 1 and table_name = '__all_virtual_meta_table', part_num, 1);
let $part_id = 0;
while ($part_id < $part_num)
{
  let $leader_count = deploy_get_value(ob1.select select count(*) c from __all_virtual_meta_table partition (p$part_id) where svr_ip = '$ip' and svr_port = $port and role = 1, c, 1);
  if ($leader_count != 0)
  {
    inc $server_user_table_leader_count;
  }
  inc $part_id;
}
let $server_sys_table_leader_count = deploy_get_value(ob1.select select count(*) c from __all_root_table where svr_ip = '$ip' and svr_port = $port and role = 1, c, 1);
let $server_leader_count = $server_user_table_leader_count:$server_sys_table_leader_count;

let $server_user_table_follower_count = 0;
let $part_id = 0;
while ($part_id < $part_num)
{
  let $follower_count = deploy_get_value(ob1.select select count(*) c from __all_virtual_meta_table partition (p$part_id) where svr_ip = '$ip' and svr_port = $port and role = 2, c, 1);
  if ($follower_count != 0)
  {
    inc $server_user_table_follower_count;
  }
  inc $part_id;
}
let $server_sys_table_follower_count = deploy_get_value(ob1.select select count(*) c from __all_root_table where svr_ip = '$ip' and svr_port = $port and role = 2, c, 1);
let $server_follower_count = $server_user_table_follower_count:$server_sys_table_follower_count;
if ($server_leader_count == 0:0)
{
  if ($server_follower_count != 0:0)
  {
    echo server is follower;
  }
}

if ($server_follower_count == 0:0)
{
  if ($server_leader_count != 0:0)
  {
    echo server is leader;
  }
}
