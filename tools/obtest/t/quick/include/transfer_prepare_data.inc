#######################################################################################
#owner : wanhong.wwh
#owner group: rs
#description: quick test
# 测试点：
#   准备transfer和负载均衡需要的数据
#########################################################################################

# 创建表
connection conn_mysql;

# 创建一级分区表，有lob，有idx
create table if not exists t_part(pk int primary key, c1 longtext) partition by hash(pk) partitions 6;
create index if not exists idx_t_part on t_part(pk);
insert into t_part values (1, 1), (2,2), (3,3), (4,4), (5,5), (6,6), (7,7), (8,8), (9,9), (0,0), (11, 1), (12,2);

# 创建二级分区表，有lob，有idx
create table if not exists t_subpart(c0 int, c1 int, c2 longtext, primary key (c0, c1)) partition by hash(c0) subpartition by hash(c1) subpartitions 3 partitions 2;
create index if not exists idx_t_subpart on t_subpart(c0, c1);
insert into t_subpart (c0, c1) values (1, 1), (2,2), (3,3), (4,4), (5,5), (6,6), (7,7), (8,8), (9,9), (0,0), (11, 1), (12,2);

# 创建非分区表
source t/quick/include/transfer_create_nonpart_table.inc;