###############################################################
#owner : @shouju.zyp
#owner group: rs
#description: quick test
#测试点：执行root_inspection
###############################################################

ob_connect(conn_sys, ob1.z1.obs0, root@sys, , oceanbase, , USE_MYSQL_CONNECTOR);
connection conn_sys;

ob1.force_stop;
ob1.nstart;

source include/check_schema_sync_for_restart.inc;

ob_connect(conn_sys, ob1.z1.obs0, root@sys, , oceanbase, , USE_MYSQL_CONNECTOR);
connection conn_sys;

set @@session.ob_query_timeout = 1000000000;
