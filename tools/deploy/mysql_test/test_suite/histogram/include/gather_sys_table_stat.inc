set ob_query_timeout=100000000;
call dbms_stats.gather_table_stats('OCEANBASE','__ALL_VIRTUAL_CORE_ALL_TABLE');
call dbms_stats.gather_table_stats('OCEANBASE','__ALL_VIRTUAL_CORE_COLUMN_TABLE');
call dbms_stats.gather_table_stats('OCEANBASE','__ALL_TABLE');
call dbms_stats.gather_table_stats('OCEANBASE','__ALL_COLUMN');
call dbms_stats.gather_table_stats('OCEANBASE','__ALL_TABLE_STAT');
call dbms_stats.gather_table_stats('OCEANBASE','__ALL_COLUMN_STAT');
set ob_query_timeout=default;
