connection conn_tenant;
--disable_all_result_log
let $i = 1;
while ($i < 27) 
{
    eval drop table if exists main_table_$i;
    eval create table main_table_$i (c1 int, c2 int, c3 varchar(40960), c4 int, hobbies json, index idx_c1_$i (c1), index idx_c2_$i (c2), index idx_hobbies_$i ((CAST(hobbies AS char(100) ARRAY))));
    inc $i;
}
--enable_all_result_log