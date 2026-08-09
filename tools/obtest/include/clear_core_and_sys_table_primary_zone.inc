--disable_query_log
--disable_result_log
use oceanbase;
alter system set enable_sys_table_ddl = true;

#
### set core_table primary_zone field to ''
#
let $max_schema_version = `select max(schema_version) from __all_ddl_operation`;
#echo $max_schema_version;
eval update __all_core_table set column_value = $max_schema_version + 8 where table_name = '__all_global_stat' and column_name = 'core_schema_version';
alter system refresh schema;

#
### set sys_table primary_zone field to ''
#
let $count = `select count(*) from __all_virtual_table where tenant_id = 1 and table_type = 0 limit 1`;
let $max_table_id = query_get_value(select (1 << 40) | 50001 as max_table_id, max_table_id, 1);
let $cnt = $count;
while ($cnt)
{
  let $table_name = query_get_value(select table_name from __all_virtual_table where tenant_id = 1 and table_type = 0 and table_id < $max_table_id order by table_id desc limit 1, 'table_name', 1);
  let $table_id = query_get_value(select table_id from __all_virtual_table where tenant_id = 1 and table_type = 0 and table_id < $max_table_id order by table_id desc limit 1, 'table_id', 1);
  let $database_id = query_get_value(select database_id from __all_virtual_table where tenant_id = 1 and table_type = 0 and table_id < $max_table_id order by table_id desc limit 1, 'database_id', 1);
  let $database_name = query_get_value(select database_name from __all_virtual_database where tenant_id = 1 and database_id = $database_id, 'database_name', 1);
  let $max_table_id = $table_id;
  #echo $table_id, $table_name;

  eval alter table $database_name.$table_name set primary_zone = RANDOM; 
  dec $cnt;
}
--enable_query_log
--enable_result_log
