# Fork Table 专用的等待 Major Freeze 完成脚本

--disable_query_log
--disable_result_log
connect (obsys_wait_merge,$OBMYSQL_MS0,admin,$OBMYSQL_PWD,test,$OBMYSQL_PORT);
connection obsys_wait_merge;

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
    eval select * from oceanbase.__all_virtual_server_compaction_event_history where compaction_scn = $__target_frozen_scn__;
    select * from oceanbase.__all_virtual_compaction_diagnose_info;
    --die timeout waiting for target major freeze
}
sleep 1;

disconnect obsys_wait_merge;
connection default;
--enable_query_log
--enable_result_log
