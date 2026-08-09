let $role = deploy_get_value(ob1.select select role from __all_virtual_meta_table where table_id = '$table_id' and partition_id = '$partition_id' and svr_ip = '$ip' and svr_port = '$port', role, 1);
if ($role == 1)
{
  echo this user partition is leader;
}
if ($role == 2)
{
  echo this user partition is follower;
}
