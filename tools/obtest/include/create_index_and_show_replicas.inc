# @pre
# table create_index_and_show_replicas_t1 created using:
# create table create_index_and_show_replicas_t1 (c1 int primary key);
# @hard coded arguments:
# create_index_and_show_replicas_i2

create index create_index_and_show_replicas_i1 on test.create_index_and_show_replicas_t1(c1);
ob1.mysql_p select B.zone, B.replica_type from oceanbase.__all_virtual_table A, oceanbase.__all_virtual_meta_table B where A.table_name like '%create_index_and_show_replicas_i1%' and A.table_id=B.table_id order by B.zone, B.replica_type;
drop index create_index_and_show_replicas_i1 on test.create_index_and_show_replicas_t1;
