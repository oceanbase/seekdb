alter system minor freeze;
--disable_query_log
--disable_result_log

let $__i__=180;
let $__j__ = 10;
while($__i__ > 0)
{
  sleep 1;
  dec $__i__;
  dec $__j__;

  let $__memstore_cnt__ = query_get_value(select count(*) as cnt from oceanbase.__all_virtual_tablet_memstore_info where tablet_id >= 200000, cnt, 1);

  if ($__memstore_cnt__ == 0)
  {
    let $__i__ = -5;
  }
  if ($__j__ == 0)
  {
   alter system minor freeze;
   let $__j__ = 10;
  }
}

if ($__i__ != -5)
{
  --echo minor freeze failed
}

--enable_query_log
--enable_result_log
