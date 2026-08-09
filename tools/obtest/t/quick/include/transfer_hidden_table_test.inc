#######################################################################################
#owner : wangzhennan.wzn
#owner group: RS
#description: quick test
# 测试点：
#   测试transfer能够跳过offline ddl隐藏表的处理，最终offline ddl结束后再将主表transfer走
#########################################################################################

--echo # create user table (primary table)
connection conn_mysql;
create table if not exists t_hidden_1(c1 int, c2 int) partition by hash(c1) partitions 2;

--echo # create hidden table with debug_sync
ob1.mysql alter system set debug_sync_timeout = '1000s';
# 保证让所有机器都能刷到这个配置项
ob1.check_until_timeout select count(*) from gv\$ob_parameters where name = 'debug_sync_timeout' and value != '1000s', 0, 3;
ob1.mysql set ob_global_debug_sync = 'TABLE_REDEFINITION_REPLICA_BUILD wait_for signal_ddl execute 10000';
ob1.mysql set ob_global_debug_sync = 'AFTER_TRANSFER_PROCESS_INIT_TASK_AND_BEFORE_NOTIFY_STORAGE wait_for signal_transfer execute 10000';

connection conn_mysql;
send alter table t_hidden_1 modify c2 char(10);
ob1.check_until_timeout select count(*) from __all_virtual_table where tenant_id = $mysql_tenant_id and table_name = 't_hidden_1' and association_table_id != -1 , 1, 10;
let $primary_table_id = deploy_get_value(ob1.select select table_id from __all_virtual_table where tenant_id = $mysql_tenant_id and table_name = 't_hidden_1', table_id, 1);
let $hidden_table_id = deploy_get_value(ob1.select select association_table_id from __all_virtual_table where tenant_id = $mysql_tenant_id and table_name = 't_hidden_1', association_table_id, 1);

--echo # primary 1->2
ob1.mysql alter tenant mysql set primary_zone = 'z1,z2';

--echo # verify offline ddl hidden table is treated as lock conflict table in transfer
ob1.check_until_timeout select count(*) from oceanbase.__all_virtual_transfer_task where tenant_id = $mysql_tenant_id and status = 'START', 1, 300;
ob1.check_until_timeout select count(*) from oceanbase.__all_virtual_transfer_task where tenant_id = $mysql_tenant_id and status = 'START' and lock_conflict_part_list like '%$primary_table_id%' and lock_conflict_part_list like '%$hidden_table_id%', 1, 1;
connection conn_sys;
eval select lock_conflict_part_list from oceanbase.__all_virtual_transfer_task where tenant_id = $mysql_tenant_id and status = 'START';
eval select tenant_id, table_name, partition_name, object_id, ls_id from CDB_OB_TABLE_LOCATIONS where tenant_id = $mysql_tenant_id and table_id in ($primary_table_id, $hidden_table_id) order by object_id;

--echo # signal transfer
ob1.mysql set ob_global_debug_sync = 'AFTER_TRANSFER_PROCESS_INIT_TASK_AND_BEFORE_NOTIFY_STORAGE clear';
ob1.mysql set ob_global_debug_sync = 'now signal signal_transfer';

--echo # verify that the transfer task fails when all tables have table locks applied.
ob1.check_until_timeout select count(*) > 0 from oceanbase.__all_virtual_transfer_task_history where tenant_id = $mysql_tenant_id and status = 'FAILED' and lock_conflict_part_list like '%$primary_table_id%' and lock_conflict_part_list like '%$hidden_table_id%' and comment like '%Unable to process task due to all partitions being locked%', 1, 60;

--echo # change the retry interval for failures to 1 second to allow immediate retries and reduce the case's time consumption.
ob1.mysql alter system set _transfer_task_retry_interval = '1s';
ob1.check_until_timeout select count(*) from gv\$ob_parameters where name = '_transfer_task_retry_interval' and tenant_id = $mysql_tenant_id and value != '1s', 0, 3;

--echo # turn off debug_sync
ob1.mysql set ob_global_debug_sync = 'reset';
ob1.mysql set ob_global_debug_sync = 'now signal signal_ddl';
ob1.mysql alter system set debug_sync_timeout = 0;

--echo # hidden table converts to primary table
connection conn_mysql;
reap;
let $new_primary_table_id = $hidden_table_id;
ob1.check_until_timeout select count(*) from __all_virtual_table where tenant_id = $mysql_tenant_id and table_name = 't_hidden_1' and association_table_id = -1 and table_id = $new_primary_table_id, 1, 20;

--echo # wait balance job finish
ob1.check_until_timeout select count(*) from __all_virtual_balance_job where tenant_id = $mysql_tenant_id, 0, 100;
ob1.check_until_timeout select count(*) from __all_virtual_balance_task where tenant_id = $mysql_tenant_id, 0, 30;
ob1.check_until_timeout select count(*) from __all_virtual_transfer_task where tenant_id = $mysql_tenant_id, 0, 30;

--echo # change _transfer_task_retry_interval back to its default value.
ob1.mysql alter system set _transfer_task_retry_interval = '1m';
ob1.check_until_timeout select count(*) from gv\$ob_parameters where name = '_transfer_task_retry_interval' and tenant_id = $mysql_tenant_id and value != '1m', 0, 3;

--echo # verify transfer new_primary_table after offline ddl finished
ob1.check_until_timeout select count(*) from __all_virtual_transfer_task_history where tenant_id = $mysql_tenant_id and part_list like '%$new_primary_table_id%' and not_exist_part_list like '%$primary_table_id%' and lock_conflict_part_list is null, 1, 10;
ob1.check_until_timeout select count(*) from __all_virtual_balance_task_history where tenant_id = $mysql_tenant_id and finished_part_list like '%$new_primary_table_id%', 1, 10;
connection conn_sys;
eval select tenant_id, table_name, partition_name, object_id, ls_id from CDB_OB_TABLE_LOCATIONS where tenant_id = $mysql_tenant_id and table_id in ($primary_table_id, $hidden_table_id, $new_primary_table_id) order by object_id;

--echo # delete data
connection conn_mysql;
set recyclebin = off;
DROP TABLE IF EXISTS t_nopart_1, t_nopart_2, t_nopart_3, t_nopart_4, t_nopart_5, t_nopart_6, t_hidden_1;