#############################################
#owner : @msy164651
#owner group: rs
#description: quick test
# 测试点：验证集群重启
#############################################
let $stop_tstamp_usec = query_get_value(select time_to_usec(now(6)) as now, now, 1);
create table t1_reboot (c1 int);
insert into t1_reboot values(1);
let $server_create_time = deploy_get_value(ob1.select SELECT create_time as server_create_time FROM oceanbase.__all_virtual_server_stat, server_create_time, 1);
select count(*) as cnt from oceanbase.__all_virtual_server_stat where create_time != 0;
ob1.force_stop;
sleep 6;
ob1.nstart;
sleep 6;
--disable_query_log
ob1.check_until_timeout select count(*) as cnt from oceanbase.__all_virtual_server_stat where start_service_time > $stop_tstamp_usec, 1, 180;
# check server create_time not changed after restart
ob1.check_until_timeout select count(*) as cnt from oceanbase.__all_virtual_server_stat where create_time = $server_create_time, 1, 10;
source include/check_schema_sync_for_restart.inc;
--enable_query_log
ob1.check_until_timeout select count(*) from __all_virtual_memory_info where mod_name like '%OB_MOD_DO_NOT_USE_ME%' and hold != 0, 0, 10;
ob_connect(conn_sys, ob1.z1.obs0, root@sys, , oceanbase, , USE_MYSQL_CONNECTOR);
select * from t1_reboot;
