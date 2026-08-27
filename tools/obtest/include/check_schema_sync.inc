--disable_query_log
ob1.check_until_timeout select count(*) as cnt from oceanbase.__all_virtual_server_schema_info as a join oceanbase.__all_virtual_server_schema_info as b where a.refreshed_schema_version != b.refreshed_schema_version, 0, 60;
--enable_query_log
