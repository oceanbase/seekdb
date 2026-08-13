#############################################
#owner : msy164651
#owner group: rs
#description: quick test
# 测试点：验证日志流的自动创建
#############################################
connection conn_mysql;
let $mysql_tenant_id= deploy_get_value(ob1.select select tenant_id from oceanbase.__all_tenant where tenant_name = 'mysql', tenant_id, 1);
ob1.mysql alter system set debug_sync_timeout = '1000s';
#sleep a time longer than heartbeat like 5s to let debug_sync_timeout enabled on followers
sleep 5;
ob1.mysql set ob_global_debug_sync = 'BEFORE_CREATE_USER_LS wait_for signal';
ob1.mysql ALTER TENANT mysql set primary_zone='z1, z3';
ob1.check_until_timeout select count(*) from oceanbase.__all_virtual_ls where tenant_id = $mysql_tenant_id and status = 'CREATING', 1, 30;
sleep 10;
ob1.mysql_p select ls_id, ls_group_id, status from  oceanbase.__all_virtual_ls where tenant_id = $mysql_tenant_id;
ob1.mysql set ob_global_debug_sync = 'now signal signal';
#ob1.check_until_timeout select count(*) from oceanbase.__all_virtual_ls where tenant_id = $mysql_tenant_id and status = 'CREATING', 0, 30;
#ob1.mysql_p select ls_id, ls_group_id, status from  oceanbase.__all_virtual_ls where tenant_id = $mysql_tenant_id;
