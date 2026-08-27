#############################################
#owner : caoting.cao
#owner group: trans
#description: quick test
# 测试点：验证minor freeze
#############################################
connection conn_sys;
create table test_uncommitted_ctx (a int);
begin;
insert into test_uncommitted_ctx value(1);
set @@session.ob_query_timeout = 10000000000;
alter system minor freeze;
sleep 3;
ob1.z1.obs0.check_log_until_success -c "succeed to release head data memtable";

#再次验证sys租户下表的DML/SELECT
insert into t1_no_pk values (2001), (2002), (2003);
select * from t1_no_pk order by c1 asc;
update t1_no_pk set c1=c1+10;
select * from t1_no_pk order by c1 asc;

insert into t1_pk values (2011), (2012), (2013);
select * from t1_pk order by c1 asc;
update t1_pk set c1=c1+10;
select * from t1_pk order by c1 asc;

create table t1_part_2 (c1 int primary key) partition by hash(c1) partitions 4;
source include/check_schema_sync.inc;
insert into t1_part_2 values (1), (2), (3), (4);
select * from t1_part_2 order by c1 asc;
update t1_part_2 set c1=c1+10;
select * from t1_part_2 order by c1 asc;
