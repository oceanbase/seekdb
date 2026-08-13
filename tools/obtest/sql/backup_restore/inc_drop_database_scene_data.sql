drop database test_drop_db;
CREATE DATABASE IF NOT EXISTS test_drop_db;
use test_drop_db;
drop table if exists t1;

create table t1(pk int, a int, b int, primary key (pk), unique key uk_a_b (a, b));

insert into t1 values (10, 11, 12), (20, 21, 22), (30, 31, 32), (40, 41, 42);
