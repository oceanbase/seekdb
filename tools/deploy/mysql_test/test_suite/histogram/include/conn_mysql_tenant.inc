let $__tenant_name__ = 'sys';
--source mysql_test/include/get_tenant_server.inc
connect (conn_stat_test_sys_mysql, $TE_SERVER_IP, root@sys,,*NO-ONE*, $TE_SERVER_PORT);
connection conn_stat_test_sys_mysql;

use test;
