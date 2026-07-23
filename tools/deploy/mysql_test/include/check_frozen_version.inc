#--disable_query_log
#--disable_result_log
connection sys_conn;
let $__i__= 120;
while($__i__ > 0)
 {
     sleep 1;
     dec $__i__;
     let $__tt1_frozen_scn__ = query_get_value(select frozen_scn as value from oceanbase.DBA_OB_MAJOR_COMPACTION, value, 1);
     let $ret = query_get_value(select $__tt1_frozen_scn__ > $tt1_ori_frozen_scn as val, val, 1);
     if ($ret == 1) {
         let $__i__ = -5;
     }
}

if($__i__ != -5)
{
    --echo failed
}

connection default;
#--enable_query_log
#--enable_result_log
