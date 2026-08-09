# case 1: MySQL
connection conn_obtest;
# sys_variable
ob1.mysql alter tenant obtest set variables ob_query_timeout = 20000000;
ob1.mysql alter tenant obtest set variables ob_query_timeout = 10000000;
set @@global.ob_query_timeout = 20000000;
set @@global.ob_query_timeout = 10000000;


# database
create database schema_db_with_tg;
create database schema_db;

use schema_db;
create table schema_tb (c1 int primary key, c2 int) comment = 'simple table';
create table schema_tb_hash (c1 int primary key, c2 int) partition by hash(c1) partitions 2;
create table schema_tb_range (c1 int primary key, c2 int) partition by range(c1) (PARTITION c0 VALUES LESS THAN (1990), PARTITION c1 VALUES LESS THAN (2000), PARTITION c2 VALUES LESS THAN MAXVALUE);
create table schema_tb_range_hash (c1 int, c2 int, primary key(c1, c2)) partition by range(c1) subpartition by hash(c2) subpartitions 2 (PARTITION c0 VALUES LESS THAN (1990), PARTITION c1 VALUES LESS THAN (2000), PARTITION c2 VALUES LESS THAN MAXVALUE);
create table schema_tb_hash_range (c1 int, c2 int, primary key(c1, c2)) partition by hash(c1) subpartition by range(c2) subpartition template (SUBPARTITION p0 VALUES LESS THAN (1990),  SUBPARTITION p1 VALUES LESS THAN (2000),  SUBPARTITION p2 VALUES LESS THAN (MAXVALUE)) partitions 2;

# table with constraint and index
create table schema_tb_complex (a int primary key auto_increment, b int, constraint c1 check (a = b), unique key uk_a_b (a, b));
create table schema_tb_foreign(pk int, a int, b int, primary key (pk), unique key uk_a_b (a, b), constraint foreign key (a, b) references schema_tb_complex (a, b) on update CASCADE on delete CASCADE);

# 临时表
create temporary table schema_tb_temp(c1 int, c2 varchar(10), c3 date);

set recyclebin = on;
# table in recyclebin
create table schema_tb_in_recyclebin(c1 int, c2 int);
drop table schema_tb_in_recyclebin;

# database in recyclebin
create database schema_db_in_recyclebin;
use schema_db_in_recyclebin;
create table schema_tb_in_recyclebin_1(c1 int, c2 int);
drop database schema_db_in_recyclebin;
set recyclebin = off;

# user
create user schema_user;

# privilege(table、database)
grant select on schema_db_with_tg.* to schema_user;
grant select on schema_db.schema_tb to schema_user;

use schema_db;
# outline
create outline schema_outline on select * from schema_tb;
# synonym
create synonym synonym1 for schema_db.schema_tb;

# routine
create table schema_tb_for_routine(a1 int,a2 int,a3 int);
CREATE PROCEDURE schema_routine(x INT, y INT) BEGIN set x=y; INSERT INTO schema_tb_for_routine VALUES(1, 1, 1); END;
