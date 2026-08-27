sleep 2;

let $__i__=180;
while($__i__ > 0)
{
  sleep 1;
  dec $__i__;
  let $__memstore_cnt__ = query_get_value(select count(*) as cnt from oceanbase.__all_virtual_memstore_info where tenant_id = $tenant_id and is_active='NO', cnt, 1);
  if ($__memstore_cnt__ == 0)
  {
    let $__i__ = -5;
  }
}

if ($__i__ != -5)
{
  --echo minor freeze failed
}