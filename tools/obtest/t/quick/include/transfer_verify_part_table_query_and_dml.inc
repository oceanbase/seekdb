#######################################################################################
#owner : wangzhennan.wzn
#owner group: rs
#description: quick test
# 测试点：
#   验证transfer完成后分区表的查询和DML正常
#########################################################################################

connection conn_mysql;

# 验证query和DML
update test.t_part set pk = pk+100;
update test.t_subpart set c1=c1+100;
select * from test.t_part order by pk;
select * from test.t_subpart order by c0;