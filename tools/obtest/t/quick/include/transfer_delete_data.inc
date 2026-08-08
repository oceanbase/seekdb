#######################################################################################
#owner : duzhuochen.dzc
#owner group: rs
#description: quick test
# 测试点：
#   删除transfer_prepare_data.inc中所构建表里面的所有内容
#########################################################################################
connection conn_mysql;
set recyclebin = off;

# transfer_prepare_data.inc 中所构建的表
let $i = 1;
while ($i <= 6)
{
  eval delete from t_nopart_$i;
  inc $i;
}
delete from t_part;
delete from t_subpart;
