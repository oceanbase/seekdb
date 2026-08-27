#############################################
#owner : zhaoziqian.zzq
#owner group: rs
#description: quick test
# 测试点：验证sys租户下的dml/select
#############################################
#验证sys租户下的dml以及select
connection conn_sys;
create table t1_no_pk (c1 int);
source include/check_schema_sync.inc;
insert into t1_no_pk values (1), (2), (3);
select * from t1_no_pk order by c1 asc;
update t1_no_pk set c1=c1+10;
select * from t1_no_pk order by c1 asc;

create table t1_pk (c1 int primary key);
source include/check_schema_sync.inc;
insert into t1_pk values (1), (2), (3);
select * from t1_pk order by c1 asc;
update t1_pk set c1=c1+10;
select * from t1_pk order by c1 asc;

create table t1_part (c1 int primary key) partition by hash(c1) partitions 4;
source include/check_schema_sync.inc;
insert into t1_part values (1), (2), (3), (4);
select * from t1_part order by c1 asc;
update t1_part set c1=c1+10;
select * from t1_part order by c1 asc;
