#############################################
#owner : jingyu.cr
#owner group: rs
#description: quick test
# 测试点：验证tablet汇报
#############################################
# test sys tenant tablet report
connection conn_sys;
let $max_sys_tablet_id = 10000;
let $min_lob_tablet_id = 50000;
let $max_lob_tablet_id = 70000;
let $min_index_tablet_id = 100000;
let $max_index_tablet_id = 200000;
let $server_count = 1;
let $partition_num = 3;

let $core_table_count = deploy_get_value(ob1.select select count(*) cnt from oceanbase.__all_virtual_core_all_table, cnt, 1);
let $sys_table_count = deploy_get_value(ob1.select select count(*) cnt from oceanbase.__all_table where ((table_id < $max_sys_tablet_id) or (table_id > $min_lob_tablet_id and table_id < $max_lob_tablet_id) or (table_id > $min_index_tablet_id and table_id < $max_index_tablet_id)), cnt, 1);
#sys_tables
ob1.check_until_timeout select count(*) = $server_count * ($sys_table_count + $core_table_count) from oceanbase.__all_virtual_tablet_meta_table where tablet_id < $max_index_tablet_id, 1, 200;
#user_tables single partition
create table sys_t1(c1 int);
let $table_id = deploy_get_value(ob1.select select table_id from oceanbase.__all_table where table_name = 'sys_t1', table_id, 1);
let $tablet_id = deploy_get_value(ob1.select select tablet_id from oceanbase.__all_table where table_id = $table_id, tablet_id, 1);
ob1.check_until_timeout select count(*) = $server_count from oceanbase.__all_virtual_tablet_meta_table where tablet_id = $tablet_id, 1, 200;
#user_tables multi partition
eval create table sys_t2(c1 int) partition by hash(c1) partitions $partition_num;
let $table_id = deploy_get_value(ob1.select select table_id from oceanbase.__all_table where table_name = 'sys_t2', table_id, 1);
let $tablet_ids = select tablet_id from oceanbase.__all_part where table_id = $table_id;
ob1.check_until_timeout select count(*) = $partition_num * $server_count from oceanbase.__all_virtual_tablet_meta_table where tablet_id in ($tablet_ids), 1, 200;
