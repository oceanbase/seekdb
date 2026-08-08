#####################
# owner : wangzhennan.wzn
# owner group : RS
# description : test tablet distribution for tablet creation
#####################

ob_connect(conn_sys, ob1.z1.obs0, root@sys, , oceanbase, , USE_MYSQL_CONNECTOR);
connection conn_sys;
set @@session.ob_query_timeout = 1000000000;
alter system set debug_sync_timeout = '1000s';

--echo # create mysql tenant
CREATE RESOURCE UNIT if not exists unit_1 MAX_CPU 2, MEMORY_SIZE='4G';
CREATE RESOURCE POOL if not exists pool_1 UNIT = 'unit_1', UNIT_NUM = 1;
CREATE TENANT mysql RESOURCE_POOL_LIST = ('pool_1'), primary_zone='z1,z2,z3', locality="F@z1,F@z2,F@z3" set ob_tcp_invited_nodes='%';

let $mysql_tenant_id = deploy_get_value(ob1.select select tenant_id from oceanbase.__all_tenant where tenant_name = 'mysql', tenant_id, 1);
let $mysql_leader_ip_port=deploy_get_value(ob1.select select concat(svr_ip,':',svr_port) as host from __all_virtual_ls_meta_table where tenant_id = $mysql_tenant_id and ls_id =  1 and role = 1 limit 1, host, 1);
let $mysql_leader_obs =deploy_get_value(ob1.get_obs_by_addr $mysql_leader_ip_port);
ob_connect(conn_mysql, $mysql_leader_obs, root@mysql, , test, , USE_MYSQL_CONNECTOR);

--echo ################################################
# block the balance thread to make the table creation distribution stable
ob1.mysql set ob_global_debug_sync = 'BEFORE_TENANT_BALANCE_SERVICE wait_for signal_balance';
connection conn_mysql;
--echo # nonpart table
create table t_nonpart_1(c1 int, c2 int, c3 longtext, index(c1) local, index(c2) global);
create table t_nonpart_2(c1 int, c2 int, c3 longtext);
create index local_index_t_nonpart_2_c1 on t_nonpart_2(c1);
create index global_index_t_nonpart_2_c2 on t_nonpart_2(c2) global;
create table t_nonpart_3(c1 int, c2 int, c3 longtext);
create index local_index_t_nonpart_3_c1 on t_nonpart_3(c1);
create unique index unique_global_index_t_nonpart_3_c2 on t_nonpart_3(c2) global partition by hash(c2) partitions 2;
--echo # part table
create table t_part_1(c1 int, c2 int, c3 longtext, index(c1) local, index(c2) global) partition by hash(c1) partitions 3;
create table t_part_2 (c1 int, c2 int, c3 longtext, index(c1) local, index(c2) global) partition by range(c1) (partition p0 values less than (100), partition p1 values less than (200));
create table t_part_3(c1 int, c2 int, c3 longtext) partition by hash(c1) partitions 3;
create index local_index_t_part_3_c1 on t_part_3(c1);
create unique index unique_global_index_t_part_3_c2 on t_part_3(c2) global partition by hash(c2) partitions 2;
# create table like
create table t_part_4 like t_part_1;
--echo # subpart table
# turn off parallel ddl
set session _ENABLE_PARALLEL_DDL = 0;
create table t_subpart_1(c1 int, c2 int, c3 longtext, index(c1) local, index(c2) global) partition by range(c1) subpartition by hash(c1) subpartitions 3 (partition p1 values less than (100), partition p2 values less than (200), partition p3 values less than MAXVALUE);
create table t_subpart_2(c1 int, c2 int, c3 longtext) partition by range(c1) subpartition by hash(c1) subpartitions 3 (partition p1 values less than (100), partition p2 values less than (200), partition p3 values less than (300));
create index local_index_t_subpart_2_c1 on t_subpart_2(c1);
create index global_index_t_subpart_2_c2 on t_subpart_2(c2) global partition by hash(c2) partitions 2;
set session _ENABLE_PARALLEL_DDL = 1;
# add partition
alter table t_part_2 add partition (partition p2 values less than (400));
# truncate table
truncate table t_part_1;

let $table_names = 't_nonpart_1', 't_nonpart_2', 't_nonpart_3', 't_part_1', 't_part_2', 't_part_3', 't_part_4', 't_subpart_1', 't_subpart_2';
let $table_ids = select distinct table_id from CDB_OB_TABLE_LOCATIONS where tenant_id = $mysql_tenant_id and table_name in ($table_names);
let $global_index_table_ids = select table_id from __all_virtual_table where tenant_id = $mysql_tenant_id and index_type in (3,4,11) and table_id > 500000;

--echo # test basic read and write
connection conn_mysql;
insert into t_nonpart_2 values (1, 2, 3);
select * from t_nonpart_2;
insert into t_part_2 values (1, 2, 3);
select * from t_part_2;
insert into t_part_3 values (1, 2, 3);
select * from t_part_3;
insert into t_part_4 values (1, 2, 3);
select * from t_part_4;
insert into t_subpart_2 values (1, 2, 3);
select * from t_subpart_2;

--echo ################################################
--echo # tablet distribution (include global index)
connection conn_sys;
eval select distinct ls_id, table_id, data_table_id, table_name, partition_name, subpartition_name, tablet_id from CDB_OB_TABLE_LOCATIONS where tenant_id = $mysql_tenant_id and (table_id in ($table_ids) or (data_table_id in ($table_ids) and table_id in ($global_index_table_ids))) order by table_id, tablet_id;

--echo ################################################
--echo # related tablet distribution
# global index include 3 types: INDEX_TYPE_NORMAL_GLOBAL = 3, INDEX_TYPE_UNIQUE_GLOBAL = 4, INDEX_TYPE_SPATIAL_GLOBAL = 11
--echo ##
--echo # global index tablet distribution (with primary table)
eval select distinct ls_id, table_id, data_table_id, table_name, partition_name, subpartition_name, tablet_id from CDB_OB_TABLE_LOCATIONS where tenant_id = $mysql_tenant_id and table_id in ($global_index_table_ids) order by table_id, tablet_id;
--echo ##
--echo # all local index and aux lob tables are bound to primary table
let $all_user_tablets = select distinct ls_id, table_id, data_table_id, table_name, partition_name, subpartition_name, tablet_id from CDB_OB_TABLE_LOCATIONS where tenant_id = $mysql_tenant_id and table_type = 'USER TABLE';
let $all_binding_tablets = select distinct ls_id, table_id, data_table_id, table_name, partition_name, subpartition_name, tablet_id from CDB_OB_TABLE_LOCATIONS where tenant_id = $mysql_tenant_id and table_type != 'USER TABLE' and table_id > 500000 and table_id not in ($global_index_table_ids);
ob1.check_until_timeout select count(*) > 0 from ($all_user_tablets) as A join ($all_binding_tablets) as B on A.table_id = B.data_table_id and A.partition_name = B.partition_name and A.subpartition_name = B.subpartition_name and A.ls_id = B.ls_id, 1, 1;
ob1.check_until_timeout select count(*) from ($all_user_tablets) as A join ($all_binding_tablets) as B on A.table_id = B.data_table_id and A.partition_name = B.partition_name and A.subpartition_name = B.subpartition_name and A.ls_id != B.ls_id, 0, 1;
--echo ##
--echo # specially, the global index of a non-partitioned table is actually a local index
ob1.check_until_timeout select count(distinct(ls_id)) from CDB_OB_TABLE_LOCATIONS where tenant_id = $mysql_tenant_id and (table_name = 't_nonpart_2' or index_name = 'global_index_t_nonpart_2_c2'), 1, 1;

ob1.mysql set ob_global_debug_sync = 'now signal signal_balance';
--echo drop tenants
connection conn_sys;
set recyclebin = off;
drop tenant mysql force;
