#############################################
#owner : chensen.cs
#owner group: trans
#description: quick test
# 测试点：验证minor freeze后sys租户的读写
#############################################
connection conn_sys;
set @@session.ob_query_timeout = 100000000000;
alter system minor freeze;

#再次验证sys租户下表的DML/SELECT
insert into t1_no_pk values (1001), (1002), (1003);
select * from t1_no_pk order by c1 asc;
update t1_no_pk set c1=c1+10;
select * from t1_no_pk order by c1 asc;

insert into t1_pk values (1011), (1012), (1013);
select * from t1_pk order by c1 asc;
update t1_pk set c1=c1+10;
select * from t1_pk order by c1 asc;

create table t1_part_1 (c1 int primary key) partition by hash(c1) partitions 4;
source include/check_schema_sync.inc;
insert into t1_part_1 values (1), (2), (3), (4);
select * from t1_part_1 order by c1 asc;
update t1_part_1 set c1=c1+10;
select * from t1_part_1 order by c1 asc;
