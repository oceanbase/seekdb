--disable_query_log
--disable_result_log

connect (obsys_major,$OBMYSQL_MS0,admin,$OBMYSQL_PWD,test,$OBMYSQL_PORT);
connection obsys_major;

let $__previous_frozen_scn__ = query_get_value(select max(frozen_scn) as frozen_scn from oceanbase.__all_freeze_info, frozen_scn, 1);
eval alter system major freeze;
let $__target_frozen_scn__ = query_get_value(select max(frozen_scn) as frozen_scn from oceanbase.__all_freeze_info, frozen_scn, 1);
let $__freeze_advanced__ = query_get_value(select $__target_frozen_scn__ > $__previous_frozen_scn__ as advanced, advanced, 1);

if (!$__freeze_advanced__)
{
  --die major freeze did not generate a new frozen_scn
}

let $__i__= 600;
while($__i__ > 0)
{
  sleep 1;
  dec $__i__;
  let $__finish_major__= query_get_value(select count(*) as cnt from oceanbase.DBA_OB_MAJOR_COMPACTION where frozen_scn >= $__target_frozen_scn__ and last_scn >= $__target_frozen_scn__ and status = 'IDLE', cnt, 1);
  if ($__finish_major__ > 0)
  {
    let $__i__ = -5;
  }
}

if ($__i__ != -5)
{
  --echo major freeze failed
  eval select $__target_frozen_scn__ as target_frozen_scn;
  select * from oceanbase.DBA_OB_MAJOR_COMPACTION;
  --die timeout waiting for target major freeze
}

disconnect obsys_major;
connection default;

--enable_query_log
--enable_result_log
