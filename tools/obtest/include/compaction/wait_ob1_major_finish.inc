--disable_result_log


ob1.check_until_timeout select count(1) from CDB_OB_MAJOR_COMPACTION where (GLOBAL_BROADCAST_SCN > LAST_SCN or STATUS != 'IDLE'), 0, 600;

--enable_result_log
