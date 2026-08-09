## OB_MAX_USER_ROW_LENGTH = 1572864L 
# 4kb size for each column
# 1572864 / (4 * 1024) = 384 

--disable_query_log
use test;
drop table if exists big_table;
--disable_result_log

# 创建包含383个varchar(4096)的表
let $const = 383;
--let $create_sql = create table big_table(pk int primary key
--let $select_sql = select pk
let $field = $const;
--disable_query_log
while($field)
{
    --let $create_sql = $create_sql , f$field varchar(4096)
    --let $select_sql = $select_sql , substr(f$field, 1, 5)
    dec $field;
}
--enable_query_log

--let $create_sql = $create_sql) DEFAULT CHARSET=binary
--disable_query_log
--let $select_sql = $select_sql from big_table

eval $create_sql;

# 为insert准备数据
# 1047 bytes
--let $value = 1
--let $new_value = 2
--let $byte = 111111111111111
--let $new_byte = 222222222222222
# 4096 / 15 = 273
let $kb = 273;
--disable_query_log
while($kb)
{
    --let $value = $value$byte
    --let $new_value = $new_value$new_byte
    dec $kb;
}

let $field = $const;
--let $insert_sql = insert into big_table values(1
while($field)
{
    --let $insert_sql = $insert_sql , '$value'
    dec $field;
}
--let $insert_sql = $insert_sql)
--disable_query_log
eval $insert_sql;

let $field = $const;
--let $update_sql = update big_table set f$field = '$new_value'
dec $field;
while($field)
{
    --let $update_sql = $update_sql , f$field = '$new_value'
    dec $field;
}
--let $update_sql = $update_sql where pk = 1
--disable_result_log
eval $update_sql;
eval $select_sql;
--enable_result_log