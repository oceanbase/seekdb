use test;

drop table if exists tb1;
drop table if exists tb2;
drop table if exists tb3;

# 创建表
create table tb1 (x int primary key, y int);
create table tb2 (x int primary key, y int) PARTITION BY LIST(x) (PARTITION p0 VALUES IN (1, 2, 3), PARTITION p1 VALUES IN (5, 6), PARTITION p2 VALUES IN (DEFAULT));
create table tb3 (x int, y int, z varchar(100)) PARTITION BY RANGE COLUMNS(x) SUBPARTITION BY HASH(y) SUBPARTITIONS 5 (PARTITION p0 VALUES LESS THAN(100),PARTITION p1 VALUES LESS THAN(200),PARTITION p2 VALUES LESS THAN(300));

insert into tb1 values (1,1),(2,2);

insert into tb2 values (1,1),(5,5),(9,9);

insert into tb3 values (0,0,'a'),(0,1,'a'),(0,2,'a'),(0,3,'a'),(0,4,'a');
insert into tb3 values (100,0,'a'),(100,1,'a'),(100,2,'a'),(100,3,'a'),(100,4,'a');
insert into tb3 values (200,0,'a'),(200,1,'a'),(200,2,'a'),(200,3,'a'),(200,4,'a');

# alter table
alter table tb1 add column a int default 1;
alter table tb2 add column a int default 2;
alter table tb3 add column a int default 3;

# 创建索引, 4.4 之后建索引& offline ddl 不接受来自历史的请求，返回4007
--error 0, 1235
create index i1 on tb1(a);
--error 0, 1235
create index i2 on tb2(a);
--error 0, 1235
create index i3 on tb3(a);

--disable_result_log
select*from tb1;
select*from tb2;
select*from tb3;
--enable_result_log
