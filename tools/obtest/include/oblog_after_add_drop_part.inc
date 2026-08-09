let $ip=deploy_get_value(ob1.proxy0.ip);
let $port=deploy_get_value(ob1.proxy0.port);
connect (oblog_conn_sys,$ip,root@sys,,oceanbase,$port);

# 连接sys租户
#connection oblog_conn_sys;
#use test;
#ob1.oblog.exist;

# 在oblog_tt租户下操作
connect (oblog_conn_tt,$ip,root@oblog_tt,,oceanbase,$port);
connection oblog_conn_tt;
use oblog_db;


ob1.oblog.next;

# 为t1表动态增加4个range分区
alter table t1 add partition (partition p2 values less than(30), partition p3 values less than(40));
ob1.oblog.next;
insert into t1 values (21, 1);
ob1.oblog.next;
insert into t1 values (31, 1);
ob1.oblog.next;

alter table t1 add partition (partition p4 values less than(50));
ob1.oblog.next;
insert into t1 values (41, 1);
ob1.oblog.next;

alter table t1 add partition (partition p5 values less than(60));
ob1.oblog.next;
insert into t1 values (51, 1);
ob1.oblog.next;

# t1表动态删除range分区
alter table t1 drop partition (p4);
ob1.oblog.next;

alter table t1 drop partition (p0, p3);
ob1.oblog.next;

alter table t1 drop partition (p1);
ob1.oblog.next;

# 为t2表动态增加4个range分区
alter table t2 add partition (partition p2 values less than(30), partition p3 values less than(40), partition p4 values less than(50));
ob1.oblog.next;
insert into t2 values (21, 1);
ob1.oblog.next;
insert into t2 values (31, 1);
ob1.oblog.next;
insert into t2 values (41, 1);
ob1.oblog.next;

alter table t2 add partition (partition p5 values less than(60));
ob1.oblog.next;
insert into t2 values (51, 1);
ob1.oblog.next;

# t2表动态删除range分区
alter table t2 drop partition (p5);
ob1.oblog.next;

alter table t2 drop partition (p4, p0, p1, p2);
ob1.oblog.next;

# 连接sys租户
connection oblog_conn_sys;
drop tenant oblog_tt;
ob1.oblog.next;

ob1.oblog.stop;

--disable_warnings
drop resource pool if exists oblog_pool;
--disable_warnings
drop resource unit if exists oblog_box;
--disable_warnings
drop database if exists oblog_db;
