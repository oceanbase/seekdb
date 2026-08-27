# 对指定租户，查询他所有的视图
# 需要设置以下参数
# tenant_id 待测试的租户ID
# conn 连接这个租户的connection

let $tables_sql = select table_name, database_name, table_id, a.tenant_id as tenant_id from __all_virtual_database a, __all_virtual_table b where b.tenant_id=$tenant_id and b.tenant_id=a.tenant_id and a.database_id=b.database_id and length(b.view_definition) > 0;
let $view_count=deploy_get_value(ob1.select select count(*) a from ($tables_sql), a, 1);
let $limit1 = limit 1;

disable_result_log;
disable_query_log;

connection conn;
let $offset = 0;
while ($offset < $view_count)
{
  let $table_name=deploy_get_value(ob1.select select concat(tt.database_name, '.', tt.table_name) table_name from ($tables_sql) tt order by tt.table_id limit 1 offset $offset, table_name, 1);
  inc $offset;
  eval select * from $table_name $limit1;
}

enable_query_log;
enable_result_log;
