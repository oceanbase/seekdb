######################################################
#owner : zhaoziqian.zzq
#owner group: rs
#description: quick test
# 测试点：验证__all_tablet_to_table_history以及删表
######################################################
# sys
let $sys_table_id_list_str = deploy_get_value(ob1.select select group_concat(table_id Separator ',') as id_list from oceanbase.__all_table where table_name in ("t1_no_pk", "t1_pk", "t1_part"), id_list, 1);
let $sys_tablet_id_list_str = deploy_get_value(ob1.select select group_concat(tablet_id Separator ',') as id_list from oceanbase.__all_tablet_to_table where table_id in ($sys_table_id_list_str) , id_list, 1);
connection conn_sys;
drop table t1_no_pk;
drop table t1_pk;
drop table t1_part;
ob1.z1.obs0 select count(*) from oceanbase.__all_tablet_to_table_history where tablet_id in ($sys_tablet_id_list_str) order by schema_version, tablet_id;
