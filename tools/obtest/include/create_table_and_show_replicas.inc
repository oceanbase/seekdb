# hard coded arguments:
# create_table_and_show_replicas_t1

create table test.create_table_and_show_replicas_t1 (c1 int primary key);
ob1.mysql_p select B.zone, B.replica_type from oceanbase.__all_virtual_table A, oceanbase.__all_virtual_meta_table B where A.table_name='create_table_and_show_replicas_t1' and A.table_id=B.table_id order by B.zone, B.replica_type;
drop table test.create_table_and_show_replicas_t1;
