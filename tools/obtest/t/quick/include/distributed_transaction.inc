#############################################
#owner : @handora.qc
#owner group: trans
#description: quick test
# 测试点：验证分布式事务
#############################################
connection conn_mysql;
create table test_dist_tx (test_dist_tx int primary key);
connection conn_sys;
alter system change tenant mysql;
create table test_dist_tx_inner (id int primary key);
begin;
--error 1044
insert into test.test_dist_tx values(1);
insert into test_dist_tx_inner values(1);
commit;
--error 1044
select * from test.test_dist_tx;
select * from test_dist_tx_inner;
drop table test_dist_tx_inner;
alter system change tenant sys;
