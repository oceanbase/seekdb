--disable_query_log
--disable_result_log
connect (obsys_ydm,$OBMYSQL_MS0,admin,$OBMYSQL_PWD,test,$OBMYSQL_PORT);
connection obsys_ydm;

#reduce wait major compation time cost
alter system set ob_compaction_schedule_interval='10s';

# The freeze info is persisted before ALTER SYSTEM MAJOR FREEZE returns, while
# DBA_OB_MAJOR_COMPACTION may still describe the previous completed round.
# Remember the persisted SCN and wait for that exact round instead of treating
# an old frozen_scn == last_scn snapshot as completion.
let $__target_frozen_scn__ = query_get_value(select max(frozen_scn) as frozen_scn from oceanbase.__all_freeze_info, frozen_scn, 1);
let $__i__= 1200;
while($__i__ > 0)
{
    sleep 1;
    dec $__i__;
    let $__major_merge_finished__ = query_get_value(select /*+query_timeout(100000000)*/count(*) as cnt from oceanbase.DBA_OB_MAJOR_COMPACTION where frozen_scn >= $__target_frozen_scn__ and last_scn >= $__target_frozen_scn__ and status = 'IDLE', cnt, 1);
    if($__major_merge_finished__ > 0)
    {
        let $__i__ = -5;
    }
}

if($__i__ != -5)
{
    --echo major freeze failed
    eval select $__target_frozen_scn__ as target_frozen_scn;
    select * from oceanbase.DBA_OB_MAJOR_COMPACTION;
    --die timeout waiting for target major freeze
}
sleep 2;

disconnect obsys_ydm;
connection default;
--enable_query_log
--enable_result_log
