# set $__cnt__/$__row_cnt__ outside

connection conn_mysql;
--disable_query_log
while($__cnt__ < $__row_cnt__)
{
  eval insert into test01 values($__cnt__, $__cnt__, repeat('oceanbase', 4096), $__cnt__);
  eval insert into test02 values(repeat('k', 4096), $__cnt__, 2, 3, repeat('oceanbase', 4096), repeat('wakakakaka', 4096), 6, 7, 8, 9, 10, 11, 12);
  eval insert into test03 values(repeat('k', 4096), $__cnt__, 2, 3, repeat('oceanbase', 4096), repeat('wakakakaka', 4096), 6, 7, 8, 9, 10, 11, 12);
  eval insert into test04 values($__cnt__, $__cnt__, repeat('oceanbase', 4096), $__cnt__);
  inc $__cnt__;
}
--enable_query_log