let $unitname = obunit;
let $poolname = obpool;
let $ttname = obtt;
let $dbname = obdb;
let $tbname = obtable;
let $zone_name=z1;

ob1.mysql CREATE RESOURCE UNIT $unitname max_cpu 2, MEMORY_SIZE '2G';

ob1.mysql CREATE RESOURCE POOL $poolname UNIT = '$unitname', UNIT_NUM = 1, zone_list = ('$zone_name');
ob1.mysql CREATE TENANT $ttname replica_num = 1, primary_zone='$zone_name', resource_pool_list=('$poolname');

--disable_warnings
--disable_query_log
--disable_result_log
sleep 5;
--error 0,1064
ob1.mysql alter tenant $ttname set variables ob_tcp_invited_nodes='%';
--enable_warnings
--enable_query_log
--enable_result_log


ob1.mysql DROP TENANT $ttname;
ob1.mysql DROP RESOURCE POOL $poolname;
ob1.mysql DROP RESOURCE UNIT $unitname;
