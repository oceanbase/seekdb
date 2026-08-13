#######################################################################################
#owner : wangzhennan.wzn
#owner group: rs
#description: quick test
# 测试点：
#   准备transfer和负载均衡需要的非分区表数据
#########################################################################################

connection conn_mysql;

# 创建非分区表
create table if not exists t_nopart_1(c1 int);
create table if not exists t_nopart_2(c1 int);
create table if not exists t_nopart_3(c1 int);
create table if not exists t_nopart_4(c1 int);
create table if not exists t_nopart_5(c1 int);
create table if not exists t_nopart_6(c1 int);

insert into t_nopart_1 values (0);
insert into t_nopart_2 values (0);
insert into t_nopart_3 values (0);
insert into t_nopart_4 values (0);
insert into t_nopart_5 values (0);
insert into t_nopart_6 values (0);

SELECT TABLE_ID, TABLE_NAME, PARTITION_NAME, SUBPARTITION_NAME, TABLET_ID, LS_ID FROM oceanbase.DBA_OB_TABLE_LOCATIONS WHERE TABLE_ID >= 500000 AND ROLE = 'LEADER';
SELECT LS_ID, STATUS, PRIMARY_ZONE FROM oceanbase.DBA_OB_LS;