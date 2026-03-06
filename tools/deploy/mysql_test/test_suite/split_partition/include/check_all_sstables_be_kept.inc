--disable_query_log
--disable_result_log
let $__i__ = 180;
while($__i__ > 0)
{
  sleep 1;
  dec $__i__;
  let $status = query_get_value(select status from oceanbase.__all_virtual_ddl_task_status where ddl_type = 102, status, 1);
  if ($status == 100)
  {
    let $__i__ = -5;
  }
}
--enable_query_log
--enable_result_log

--echo check for data table:
--disable_query_log
let $tenant_id = query_get_value(select tenant_id from oceanbase.__all_tenant where tenant_name = 'sys', tenant_id, 1);
let $data_tid = query_get_value(select table_id from oceanbase.__all_virtual_table where tenant_id = $tenant_id and table_name = 'test_mult_sstables', table_id, 1);
let $src_tablet_id = query_get_value(select tablet_id from oceanbase.__all_virtual_part where tenant_id = $tenant_id and table_id = $data_tid and part_name = 'p1', tablet_id, 1);
let $dst_tablet_id1 = query_get_value(select tablet_id from oceanbase.__all_virtual_part where tenant_id = $tenant_id and table_id = $data_tid and part_name = 'p1_0', tablet_id, 1);
let $dst_tablet_id2 = query_get_value(select tablet_id from oceanbase.__all_virtual_part where tenant_id = $tenant_id and table_id = $data_tid and part_name = 'p1_1', tablet_id, 1);
eval select * from __all_virtual_table_mgr a where a.tenant_id = $tenant_id and a.tablet_id = $src_tablet_id and a.table_type = 10 and end_log_scn not in (select end_log_scn from __all_virtual_table_mgr b where b.tenant_id = $tenant_id and b.tablet_id = $dst_tablet_id1 and a.svr_ip = b.svr_ip and a.svr_port = b.svr_port);
eval select * from __all_virtual_table_mgr a where a.tenant_id = $tenant_id and a.tablet_id = $src_tablet_id and a.table_type = 10 and end_log_scn not in (select end_log_scn from __all_virtual_table_mgr b where b.tenant_id = $tenant_id and b.tablet_id = $dst_tablet_id2 and a.svr_ip = b.svr_ip and a.svr_port = b.svr_port);
--enable_query_log

--echo check for lob table:
--disable_query_log
let $lob_meta_tid = query_get_value(select table_id from oceanbase.__all_virtual_table where tenant_id = $tenant_id and data_table_id = $data_tid and table_name like '%AUX_LOB_META%', table_id, 1);
let $src_tablet_id = query_get_value(select tablet_id from oceanbase.__all_virtual_part where tenant_id = $tenant_id and table_id = $lob_meta_tid and part_name = 'p1', tablet_id, 1);
let $dst_tablet_id1 = query_get_value(select tablet_id from oceanbase.__all_virtual_part where tenant_id = $tenant_id and table_id = $lob_meta_tid and part_name = 'p1_0', tablet_id, 1);
let $dst_tablet_id2 = query_get_value(select tablet_id from oceanbase.__all_virtual_part where tenant_id = $tenant_id and table_id = $lob_meta_tid and part_name = 'p1_1', tablet_id, 1);
eval select count(*) from (select distinct end_log_scn from __all_virtual_table_mgr where tenant_id = $tenant_id and table_type = 10 and tablet_id in ($dst_tablet_id1, $dst_tablet_id2));
--enable_query_log
