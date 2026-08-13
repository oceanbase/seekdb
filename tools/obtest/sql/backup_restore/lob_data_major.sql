create database lob_major;
use lob_major;

create table t1(pk int primary key, tiny_text tinytext, tiny_blob tinyblob, text_type text, blob_type blob, medium_text mediumtext, medium_blob mediumblob, long_text longtext, long_blob longblob);

# NULL
insert into t1 values (1, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);

# 空
insert into t1 values (2, "", "", "", "", "", "", "", "");

# 达到单列和单行限制
# 除了longtext和longblob外，其他列达到限制
insert into t1 values (3, repeat('1', 256), repeat('2', 256), repeat('3', 65536), repeat('4', 65536), repeat('5', 16777216), repeat('6', 16777216), "", "");

# longtext达到48M限制 + longblob 11M = 59M
# 注意：不能达到60M，还有其他列，总和加起来一定大于了60M
insert into t1 values (4, "", "", "", "", "", "", repeat('a', 50331648), repeat('b', 11534336));

# longtext 11M + longblob 48M = 59M
insert into t1 values (5, "", "", "", "", "", "", repeat('c',11534336), repeat('d', 50331648));

# longtext 11M + longblob 48M = 59M
insert into t1 values (6, "", "", "", "", "", "", repeat('c',11534336), repeat('d', 50331648));

# longtext 11M + longblob 48M = 59M
# update 把longtext的值与longblob的值交换
# 单行数据118M(BinlogRecord包含新旧列值59M * 2 = 118M)
#update t1 set long_text=repeat('d', 50331648), long_blob=repeat('c', 11534336) where pk=6;
update t1 set long_text=repeat('d', 5331648), long_blob=repeat('c', 1534336) where pk=6;

# update
# medium_text: 11M
# medium_blob: 16M
# long_text: 16M
# long_blob: 16M
#update t1 set medium_text=repeat('e', 11534336), medium_blob=repeat('f', 16777216), long_text=repeat('g', 16777216), long_blob=repeat('h', 16777216) where pk=6;
update t1 set medium_text=repeat('e', 11534336), medium_blob=repeat('f', 6777216), long_text=repeat('g', 6777216), long_blob=repeat('h', 16777216) where pk=6;

# 単分区事务多行
begin;
# 小行
insert into t1 values (7, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
# 大行 2M
update t1 set long_blob=repeat('1', 2097152) where pk = 7;

# 小行 16K
insert into t1 (pk, text_type) values (8, repeat('2', 16384));

# 大行 8M
update t1 set medium_text=repeat('3', 8388608) where pk = 8;

# 大行 48M + 8M
update t1 set long_text=repeat('4', 50331648) where pk = 8;
commit;

# 分布式事务
create table t2(pk int primary key, tiny_text tinytext, tiny_blob tinyblob, text_type text, blob_type blob, medium_text mediumtext, medium_blob mediumblob, long_text longtext, long_blob longblob);
create table t3(pk int primary key, tiny_text tinytext, tiny_blob tinyblob, text_type text, blob_type blob, medium_text mediumtext, medium_blob mediumblob, long_text longtext, long_blob longblob);

begin;
# 普通行
insert into t2 (pk) values (1);
insert into t3 (pk) values (1);

# 2M LOB行
insert into t2 values (2, repeat('1', 256), repeat('2', 256), repeat('3', 65536), repeat('4', 65536), repeat('5', 1048576), repeat('6', 1048576), "", "");
insert into t3 values (2, repeat('1', 256), repeat('2', 256), repeat('3', 65536), repeat('4', 65536), repeat('5', 1048576), repeat('6', 1048576), "", "");

# 普通行
insert into t2 values (3, 'a', 'bb', 'ccc', 'dddd', 'eeee', 'fffff', 'gggggg', 'hhhhhhh');
insert into t3 values (3, 'a', 'bb', 'ccc', 'dddd', 'eeee', 'fffff', 'gggggg', 'hhhhhhh');

# 16 M LOB行
insert into t2 values (4, repeat('1', 256), repeat('2', 256), repeat('3', 65536), repeat('4', 65536), repeat('5', 1048576), repeat('6', 1048576), repeat('7', 6291456), repeat('8', 6291456));
insert into t3 values (4, repeat('1', 256), repeat('2', 256), repeat('3', 65536), repeat('4', 65536), repeat('5', 1048576), repeat('6', 1048576), repeat('7', 6291456), repeat('8', 6291456));

# 60 M LOB行
insert into t2 values (5, repeat('1', 256), repeat('2', 256), repeat('3', 65536), repeat('4', 65536), repeat('5', 16777216), repeat('6', 16777216), repeat('7', 13631488), repeat('8', 13631488));
insert into t3 values (5, repeat('1', 256), repeat('2', 256), repeat('3', 65536), repeat('4', 65536), repeat('5', 16777216), repeat('6', 16777216), repeat('7', 13631488), repeat('8', 13631488));
commit;

begin;
update t2 set long_blob="clear longblob";

update t3 set medium_blob="clear mediumblob";

update t3 set medium_blob="clear mediumblob";

# 更新主键
update t2 set pk = 6 where pk = 5;
commit;
