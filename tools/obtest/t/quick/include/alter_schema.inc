#############################################
#owner : @jiahua.cjh
#owner group: storage
#description: quick test
# 测试点：验证修改无主键表的schema后写数据然后删除表并重启回放
#############################################

connection conn_sys;
create table t_no_pk(a int, b int);
insert into t_no_pk values(1, 1), (2, 2);
alter table t_no_pk add x varchar(10) after a;
insert into t_no_pk values(3, 'hehe', 3);
alter table t_no_pk add y int;
insert into t_no_pk values(4, '666', 4, 4), (5, '888', 5, 5);
select * from t_no_pk;
drop table t_no_pk;
