--disable_query_log
ob1.check_until_timeout select count(*) > 0 from oceanbase.__all_virtual_server_schema_info, 1, 120;
ob1.check_until_timeout select count(*) as cnt from oceanbase.__all_virtual_server_schema_info as a join oceanbase.__all_virtual_server_schema_info as b where a.refreshed_schema_version != b.refreshed_schema_version or a.refreshed_schema_version % 8 != 0, 0, 180;
--enable_query_log
