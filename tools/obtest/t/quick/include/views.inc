#############################################
#owner : @wanhong.wwh
#owner group: rs
#description: quick test
# 测试点：验证views
#############################################

--echo ================== 1. SYS tenant views ==================
connection conn_sys;

# 检查视图schema，要求列是完整的，全部大写
--echo 1.1 check views schema
desc oceanbase.V$OB_SERVER_STAT;
desc oceanbase.DBA_OB_ROOTSERVICE_JOBS;

# 验证各个视图语义，从用户角度出发，尽可能验证所有场景的所有输出，特别是CASE WHEN实现的列
--echo 1.2 verify all views
connection conn_sys;

# 通过__all_rootservice_job来验证DBA_OB_xxx_JOBS
SELECT JOB_ID, JOB_TYPE, JOB_STATUS, RESULT_CODE FROM DBA_OB_ROOTSERVICE_JOBS;
