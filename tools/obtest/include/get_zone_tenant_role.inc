let $tenant_id = deploy_get_value(ob1.select select tenant_id from __all_tenant where tenant_name = '$tenant_name', tenant_id, 1);
let $zone_tenant_user_table_leader_count = deploy_get_value(ob1.select select count(*) c from __all_virtual_meta_table where zone = '$zone' and tenant_id = $tenant_id and role = 1, c, 1);
let $zone_tenant_sys_table_leader_count = deploy_get_value(ob1.select select count(*) c from __all_root_table where zone = '$zone' and tenant_id = $tenant_id and role = 1, c, 1);
let $zone_tenant_leader_count = $zone_tenant_user_table_leader_count:$zone_tenant_sys_table_leader_count;

let $zone_tenant_user_table_follower_count = deploy_get_value(ob1.select select count(*) c from __all_virtual_meta_table where zone = '$zone' and tenant_id = $tenant_id and role = 2, c, 1);
let $zone_tenant_sys_table_follower_count = deploy_get_value(ob1.select select count(*) c from __all_root_table where zone = '$zone' and tenant_id = $tenant_id and role = 2, c, 1);
let $zone_tenant_follower_count = $zone_tenant_user_table_follower_count:$zone_tenant_sys_table_follower_count;
if ($zone_tenant_leader_count == 0:0)
{
  if ($zone_tenant_follower_count != 0:0)
  {
    echo zone tenant is follower;
  }
}

if ($zone_tenant_follower_count == 0:0)
{
  if ($zone_tenant_leader_count != 0:0)
  {
    echo zone tenant is leader;
  }
}
