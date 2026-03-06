#
# owner: zg410411
# owner group: transaction
# description: The basic test cases of behaviors in SERIALIZABLE or REPEATABLE READ isolation level.
#              MySQL mode version.

--disable_query_log
set @@session.explicit_defaults_for_timestamp=off;
--enable_query_log

--disable_warnings
drop table if exists t1;
drop table if exists t2;
--enable_warnings

### MySQL模式下SET TRANSACTION语句不开事务，只设置下一个事务的隔离级别
--source mysql_test/test_suite/trx/include/serializable_trans_init.inc

connection conn1;
eval SET TRANSACTION ISOLATION LEVEL $ser_or_rr;

connection conn2;
update t1 set c='aaa' where i=1;
commit;

# SELECT语句开启新事务，能够读到aaa
connection conn1;
select * from t1;

DROP TABLE t1;
disconnect conn1;
disconnect conn2;

### 事务内允许执行SET SESSION/GLOBAL TRANSACTION，但不允许执行SET TRANSACTION
--source mysql_test/test_suite/trx/include/serializable_trans_init.inc

connection conn1;
eval SET TRANSACTION ISOLATION LEVEL $ser_or_rr;
select * from t1;

eval SET SESSION TRANSACTION ISOLATION LEVEL $ser_or_rr;
eval SET GLOBAL TRANSACTION ISOLATION LEVEL $ser_or_rr;
# FIXME
# --error 1568
# eval SET TRANSACTION ISOLATION LEVEL $ser_or_rr;

DROP TABLE t1;
disconnect conn1;
disconnect conn2;

### 事务第一条语句失败后回滚整个事务，下条语句生成新快照、新事务。
### 非第一条语句报6235(OB_TRANS_CANNOT_SERIALIZE)后，当前事务不会结束
--source mysql_test/test_suite/trx/include/serializable_trans_init.inc

connection conn2;
eval SET SESSION TRANSACTION ISOLATION LEVEL $ser_or_rr;
rollback;

# case 1
connection conn1;
rollback;
update t1 set c='a111' where i=1;

connection conn2;
--source mysql_test/include/replace_object_name_created_by_sys.inc
--error 1062
insert into t1 values (2, 'b');

connection conn1;
commit;

# 能够看到'a111'
connection conn2;
select * from t1;
rollback;

# case 2
connection conn1;
update t1 set c='a222' where i=1;

connection conn2;
--error 1146
insert into t2 values (2, 'b');

connection conn1;
commit;

# 能够看到'a222'
connection conn2;
select * from t1;
rollback;

# case 3
connection conn2;
select * from t1;

connection conn1;
update t1 set c='a333' where i=1;
commit;

# 持续报6235
# 只能看到'a222'
connection conn2;
--error 6235
update t1 set c='a444' where i=1;
--error 6235
update t1 set c='a444' where i=1;
select * from t1;
rollback;

DROP TABLE t1;
disconnect conn1;
disconnect conn2;

### Nonrepeatable read
# case 1, serializable or rr
--source mysql_test/test_suite/trx/include/serializable_trans_init.inc

connection conn1;
eval SET SESSION TRANSACTION ISOLATION LEVEL $ser_or_rr;
select * from t1 where i = 1;

connection conn2;
update t1 set c = 'aaa' where i = 1;
commit;

connection conn1;
select * from t1 where i = 1;

DROP TABLE t1;
disconnect conn1;
disconnect conn2;

# case 2, rc
--source mysql_test/test_suite/trx/include/serializable_trans_init.inc

connection conn1;
SET SESSION TRANSACTION ISOLATION LEVEL READ COMMITTED;
select * from t1 where i = 1;

connection conn2;
update t1 set c = 'aaa' where i = 1;
commit;

connection conn1;
select * from t1 where i = 1;

DROP TABLE t1;
disconnect conn1;
disconnect conn2;

### Phantoms
# case 1, serializable or rr
--source mysql_test/test_suite/trx/include/serializable_trans_init.inc

connection conn1;
eval SET SESSION TRANSACTION ISOLATION LEVEL $ser_or_rr;
select count(*) from t1;

connection conn2;
delete from t1 where i = 1;
commit;

connection conn1;
select count(*) from t1;

DROP TABLE t1;
disconnect conn1;
disconnect conn2;

# case 2, rc
--source mysql_test/test_suite/trx/include/serializable_trans_init.inc

connection conn1;
SET SESSION TRANSACTION ISOLATION LEVEL READ COMMITTED;
select count(*) from t1;

connection conn2;
delete from t1 where i = 1;
commit;

connection conn1;
select count(*) from t1;

DROP TABLE t1;
disconnect conn1;
disconnect conn2;

### tsc
--source mysql_test/test_suite/trx/include/serializable_trans_init.inc

connection conn1;
eval SET SESSION TRANSACTION ISOLATION LEVEL $ser_or_rr;
select * from t1;

connection conn2;
update t1 set c = 'aaa' where i = 1;
commit;

connection conn1;
--error 6235
update t1 set c = '2aaa' where i = 1;
select * from t1;

DROP TABLE t1;
disconnect conn1;
disconnect conn2;

### 4.x中，在serializable或rr隔离级别下，不应该报OB_SNAPSHOT_DISCARDED(4138)
# -----init-----
connect (conn0,$OBMYSQL_MS0,$OBMYSQL_USR,$OBMYSQL_PWD,test,$OBMYSQL_PORT);

connection conn0;

let $original_undo_retention = query_get_value(show parameters like 'undo_retention', value, 1);

drop table if exists t1;
alter system set undo_retention=3;
create table t1(id int);
insert into t1 values(1);

sleep 5;

connect (conn1,$OBMYSQL_MS0,$OBMYSQL_USR,$OBMYSQL_PWD,test,$OBMYSQL_PORT);
connect (conn2,$OBMYSQL_MS0,$OBMYSQL_USR,$OBMYSQL_PWD,test,$OBMYSQL_PORT);
connect (conn3,$OBMYSQL_MS0,admin,$OBMYSQL_PWD,oceanbase,$OBMYSQL_PORT);

# ----conn1-----
connection conn1;

set session transaction_isolation = 'REPEATABLE-READ';
set autocommit=0;
select * from t1;

# ----conn2-----
connection conn2;

insert into t1 values(1);
commit;
insert into t1 values(1);
commit;
delete from t1 where id = 1;
commit;
select * from t1;

# ----conn3-----
connection conn3;

sleep 10;
alter system minor freeze;
sleep 10;

# ----conn1-----
connection conn1;

# should not return 4138
select * from t1;

# ----finish-----
connection conn0;

--disable_query_log
eval alter system set undo_retention=$original_undo_retention;
# -- recovery the default isolation level
eval SET GLOBAL TRANSACTION ISOLATION LEVEL READ COMMITTED;
--enable_query_log

drop table t1;
disconnect conn0;
disconnect conn1;
disconnect conn2;
disconnect conn3;
