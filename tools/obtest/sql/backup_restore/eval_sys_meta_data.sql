eval select database_name,__all_virtual_database.read_only from __all_virtual_database,__all_tenant where tenant_name = '$tntName' and __all_tenant.tenant_id = __all_virtual_database.tenant_id AND database_name not like '%_recycle_%' AND database_name not like '__OCEANBASE_INNER_RESTORE_USER' ORDER BY database_name;
eval select name, value from __all_virtual_sys_variable,__all_tenant where tenant_name = '$tntName' and __all_tenant.tenant_id = __all_virtual_sys_variable.tenant_id and name not like '%version%' order by 1,2;

eval select user_name,passwd from __all_user,__all_tenant where tenant_name = '$tntName' and __all_tenant.tenant_id = __all_user.tenant_id and __all_user.user_name not like '%restore%' ORDER BY user_name;


eval select count(*) from __all_table,__all_tenant where tenant_name = '$tntName' and table_name = 'base_table_ghost' and __all_tenant.tenant_id =__all_table.tenant_id;

eval select count(*) from __all_table,__all_tenant where tenant_name = '$tntName' and table_name = 'inc_table_ghost' and __all_tenant.tenant_id =__all_table.tenant_id;

eval select count(*) from __all_table,__all_tenant where tenant_name = '$tntName' and table_name = 'base_view_ghost' and __all_tenant.tenant_id =__all_table.tenant_id;

eval select count(*) from __all_table,__all_tenant where tenant_name = '$tntName' and table_name = 'inc_view_ghost' and __all_tenant.tenant_id =__all_table.tenant_id;
