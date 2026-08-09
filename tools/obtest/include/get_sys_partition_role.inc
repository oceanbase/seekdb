let $role = deploy_get_value(ob1.select select role from __all_root_table where table_id = '$table_id' and partition_id = '$partition_id' and partition_cnt = '$partition_cnt', role, 1);
if ($role == 1)
{
  echo this sys partition is leader;
}
if ($role == 2)
{
  echo this sys partition is follower;
}
