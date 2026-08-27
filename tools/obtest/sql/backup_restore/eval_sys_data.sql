#table_type 5 is unique index and index table which we do not want to iterate on here.
let $cnt=query_get_value(select count(*) as num from oceanbase.gv\$table where  table_type!=5 and database_name='$dataDB' and table_name != 't1_varbinary' ,num,1);

let $inc=1;
while ($inc < $cnt)
{
  let $table_name=query_get_value(select table_name from oceanbase.gv\$table where  table_type!=5 and database_name='$dataDB' and table_name != 't1_varbinary' order by table_id asc limit $inc,1,table_name,1);
  eval select * from `$table_name` order by 1;
  inc $inc;
}



#Make sure the index works as well
eval select `table`,key_name,column_name,seq_in_index,comment from oceanbase.__tenant_virtual_table_index where table_schema='$dataDB' AND comment='available' ORDER BY `table`,key_name,column_name;

