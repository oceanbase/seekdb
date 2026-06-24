--disable_query_log
--disable_result_log
connect (obsys_ydm,$OBMYSQL_MS0,admin,$OBMYSQL_PWD,test,$OBMYSQL_PORT);
connection obsys_ydm;

#reduce wait major compation time cost
alter system set ob_compaction_schedule_interval = '10s' tenant sys;
alter system set ob_compaction_schedule_interval = '10s' tenant all_user;
alter system set ob_compaction_schedule_interval = '10s' tenant all_meta;

let $__i__= 1200;
sleep 3;
while($__i__ > 0)
{
    sleep 1;
    dec $__i__;
    let $__frozen_scn__ = query_get_value(select /*+query_timeout(100000000)*/sum(frozen_scn/1000) as sum_value from oceanbase.DBA_OB_MAJOR_COMPACTION, sum_value, 1);
    let $__merged_version__ = query_get_value(select /*+query_timeout(100000000)*/sum(last_scn/1000) as sum_value from oceanbase.DBA_OB_MAJOR_COMPACTION, sum_value, 1);
    if($__frozen_scn__ == $__merged_version__)
    {
        let $__i__ = -5;
    }
}

if($__i__ != -5)
{
    --echo major freeze failed
    select * from oceanbase.DBA_OB_MAJOR_COMPACTION;
}
sleep 2;

disconnect obsys_ydm;
connection default;
--enable_query_log
--enable_result_log
