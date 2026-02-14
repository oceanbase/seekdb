# set __table_name__ outside

--disable_query_log
--disable_result_log

connect (obsys_minor,$OBMYSQL_MS0,admin,$OBMYSQL_PWD,test,$OBMYSQL_PORT);
connection obsys_minor;

let $__table_id__ = query_get_value(select table_id from oceanbase.__all_virtual_table where table_name = $__table_name__, table_id, 1);

let $__i__=180;
while($__i__ > 0)
{
  sleep 1;
  dec $__i__;
  let $__memstore_cnt__ = query_get_value(select count(*) as cnt from oceanbase.__all_virtual_table_mgr where table_type=4 and tablet_id in (select tablet_id from oceanbase.__all_virtual_tablet_to_ls where table_id = $__table_id__), cnt, 1);
  if ($__memstore_cnt__ == 0)
  {
    let $__i__ = -5;
  }
}

if ($__i__ != -5)
{
  --echo direct load memtable dump failed
}

disconnect obsys_minor;
connection default;

--enable_query_log
--enable_result_log
