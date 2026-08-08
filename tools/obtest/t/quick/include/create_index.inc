#############################################
#owner : @zhenjiang.xzj
#owner group: storage
#description: quick test
# 测试点：验证创建索引
#############################################

connection conn_sys;
create table test_create_index(c1 int, c2 int);
insert into test_create_index values(1,1);
create index i1 on test_create_index(c2);
