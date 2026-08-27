connection conn_tenant;
--disable_all_result_log
let $i = 1;
while ($i < 27) 
{
    eval insert into main_table_$i values(1000, 1000, repeat('oceanbase', 4096), 1000, JSON_ARRAY('reading', 'knitting', 'hiking'));
    inc $i;
}
--enable_all_result_log