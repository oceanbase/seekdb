###本文件用于检查l类型的副本位于l_unit上；
##给定tenant_id;


#ob1.check_until_timeout select count(*) c from gv\$partition where replica_type = 5 and table_id  <<40>>40=135 and unit_id in (select unit_id from __all_unit where replica_type=5), 2, 3;
#
#ob1.check_until_timeout select count(*) from gv\$partition where replica_type != 5 and tenant_id = $tenant_id and unit_id in (select unit_id from __all_unit where replica_type=5), 0, 3; 
#
#ob1.check_until_timeout select min(c) from (select count(*) c from (select distinct svr_ip,svr_port from gv\$partition where tenant_id = $tenant_id group by partition_id, replica_type)s)t, 2, 3;

#ob1.check_until_timeout select count(*) c from  where replica_type = 5 and table_id  <<40>>40=135 and unit_id in (select unit_id from __all_unit where replica_type=5), 2, 3;

ob1.check_until_timeout select count(*) from  gv\$partition where replica_type != 5 and tenant_id = $tenant_id and unit_id in (select unit_id from __all_unit where replica_type=5), 0, 3; 

ob1.check_until_timeout select count(*) from  gv\$partition where replica_type = 5 and tenant_id = $tenant_id and unit_id not in (select unit_id from __all_unit where replica_type=5) and unit_id !=-1, 0, 3;

#ob1.check_until_timeout select min(c) from (select count(*) c from (select distinct svr_ip,svr_port from __all_virtual_meta_table where tenant_id = $tenant_id group by partition_id, replica_type)s)t, 2, 3;
