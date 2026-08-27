# @pre
# table truncate_table_and_show_replicas_t1 created using:
# create table truncate_table_and_show_replicas_t1 (c1 int primary key);
# @post
# drop table truncate_table_and_show_replicas_t1 created using:
# drop table table truncate_table_and_show_replicas_t1;
# @hard coded arguments:
# truncate_table_and_show_replicas_t1

truncate table test.truncate_table_and_show_replicas_t1;
ob1.mysql_p select B.zone, B.replica_type from oceanbase.__all_virtual_table A, oceanbase.__all_virtual_meta_table B where A.table_name='truncate_table_and_show_replicas_t1' and A.table_id=B.table_id order by B.zone, B.replica_type;
