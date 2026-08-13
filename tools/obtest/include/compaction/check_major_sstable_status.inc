## input: $observer_id, $tenant_name, $table_name
## output: number and types of major sstables of $table_name in $tenant_id.
## notice: only 1 tablet in $table_name, no partition


--echo check major sstable status for $table_name in tenant $tenant_name in observer $observer_id
if ($observer_id == 1)
{
    let $tablet_id = deploy_get_value(ob1.select select tablet_id from oceanbase.__all_virtual_table where table_name=$table_name, tablet_id, 1);
    let $tenant_id = deploy_get_value(ob1.select select tenant_id from oceanbase.__all_tenant where tenant_name=$tenant_name, tenant_id, 1);
    let $last_scn  = deploy_get_value(ob1.select select LAST_SCN  from oceanbase.CDB_OB_MAJOR_COMPACTION where tenant_id = $tenant_id, LAST_SCN, 1);
}
if ($observer_id == 2)
{
    let $tablet_id = deploy_get_value(ob2.select select tablet_id from oceanbase.__all_virtual_table where table_name=$table_name, tablet_id, 1);
    let $tenant_id = deploy_get_value(ob2.select select tenant_id from oceanbase.__all_tenant where tenant_name=$tenant_name, tenant_id, 1);
    let $last_scn  = deploy_get_value(ob2.select select LAST_SCN  from oceanbase.CDB_OB_MAJOR_COMPACTION where tenant_id = $tenant_id, LAST_SCN, 1);
}
--disable_query_log
eval select count(1)   from oceanbase.V\$OB_SSTABLES WHERE TENANT_ID = $tenant_id and TABLET_ID = $tablet_id and END_LOG_SCN = $last_scn and TABLE_TYPE in ('MAJOR', 'CO_MAJOR', 'NORMAL_CG', 'ROWKEY_CG');
eval select TABLE_TYPE from oceanbase.V\$OB_SSTABLES WHERE TENANT_ID = $tenant_id and TABLET_ID = $tablet_id and END_LOG_SCN = $last_scn and TABLE_TYPE in ('MAJOR', 'CO_MAJOR', 'NORMAL_CG', 'ROWKEY_CG');
--enable_query_log