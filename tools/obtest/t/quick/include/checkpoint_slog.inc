####################################################
#owner : fenggu.yh
#owner group: cloud_storage
#description: quick test
# 测试点：验证checkpoint slog
####################################################
connection conn_sys;
let $svr_ip_0 = deploy_get_value(ob1.select select svr_ip from oceanbase.V\$OB_SERVER_STAT limit 1, svr_ip, 1);
let $svr_port_0 = deploy_get_value(ob1.select select svr_port from oceanbase.V\$OB_SERVER_STAT limit 1, svr_port, 1);
--disable_query_log
eval alter system checkpoint slog server = '$svr_ip_0:$svr_port_0';
eval alter system checkpoint slog tenant_id = 500 server = '$svr_ip_0:$svr_port_0';
--enable_query_log
