connection conn0;

let $__i__= 600;
while($__i__ > 0)
{
  sleep 1;
  dec $__i__;
  let $__memstore_cnt__= query_get_value(select count(*) as cnt from oceanbase.__all_virtual_table_mgr where table_type=0 and is_active like '%NO%', cnt, 1);
  if ($__memstore_cnt__ == 0)
  {
    let $__i__ = -5;
  }
}

if ($__i__ != -5)
{
  --echo minor freeze failed
}
