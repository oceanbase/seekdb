# Note: 只能在mysql模式租户下建表
# 入参:
# 1. __large_table_name__: 自定义的表名

--echo ===============================================================
--echo ================== start create large table ===================
--echo ===============================================================

--disable_query_log
--disable_result_log

let $__columns_stmt__ = pk1 INT, pk2 INT, pk3 INT;
let $__col_idx__ = 1;
while($__col_idx__ < 4092)
{
    let $__column_stmt__ = column$__col_idx__ INT DEFAULT 100;
    let $__columns_stmt__ = $__columns_stmt__,$__column_stmt__;
    inc $__col_idx__;
}

let $__columns_stmt__ = $__columns_stmt__, f1 varchar(1024), f2 varchar(262144), primary key(pk1,pk2,pk3);

eval CREATE TABLE $__large_table_name__($__columns_stmt__);

--enable_result_log
--enable_query_log

--echo ===============================================================
--echo ================== end create large table =====================
--echo ===============================================================
