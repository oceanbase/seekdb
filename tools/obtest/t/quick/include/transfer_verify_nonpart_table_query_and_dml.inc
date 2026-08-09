#######################################################################################
#owner : wangzhennan.wzn
#owner group: rs
#description: quick test
# 测试点：
#   验证transfer完成后非分区表的查询和DML正常
#########################################################################################

connection conn_mysql;

# 验证query和DML
update test.t_nopart_1 set c1=c1+1;
update test.t_nopart_2 set c1=c1+1;
update test.t_nopart_3 set c1=c1+1;
update test.t_nopart_4 set c1=c1+1;
update test.t_nopart_5 set c1=c1+1;
update test.t_nopart_6 set c1=c1+1;
select t1.c1, t2.c1, t3.c1, t4.c1, t5.c1, t6.c1 from test.t_nopart_1 t1, test.t_nopart_2 t2, test.t_nopart_3 t3, test.t_nopart_4 t4, test.t_nopart_5 t5, test.t_nopart_6 t6;