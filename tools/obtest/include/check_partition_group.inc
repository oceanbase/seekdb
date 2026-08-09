let $count = 0;
while ($count < 10)
{
  ob1.check_until_timeout select count(*) from (select distinct svr_ip, svr_port from  __all_virtual_meta_table where zone = 'z1' and replica_type =0 and unit_id != -1 and tenant_id =$tenant_id and partition_id = $count)s, 1, 3;
  inc $count;
}
ob1.check_until_timeout select count(*) c from (select distinct unit_id  from __all_virtual_meta_table where zone = 'z1' and replica_type =5 and unit_id != -1 and tenant_id =$tenant_id)s, 1, 3;
ob1.check_until_timeout select count(*) c from (select distinct unit_id  from __all_virtual_meta_table where zone = 'z2' and replica_type =5 and unit_id != -1 and tenant_id =$tenant_id)s, 1, 3;
ob1.check_until_timeout select count(*) c from (select distinct unit_id  from __all_virtual_meta_table where zone = 'z3' and replica_type =5 and unit_id != -1 and tenant_id =$tenant_id)s, 1, 3;

