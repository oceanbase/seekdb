#目的：建tenant
##传入$case_index
##传入$unit_num
##传入locality
##传入zone_list

let $cpu = 1;
let $memory = 1073741824;
let $min_memory = 1073741824;
let $disk = 536870912;
let $iops = 100000;

ob1.mysql CREATE RESOURCE UNIT unit_$case_index MAX_CPU $cpu, MAX_MEMORY $memory, MAX_IOPS $iops, MAX_DISK_SIZE $disk, MAX_SESSION_NUM 64, MIN_CPU=1, MIN_MEMORY=$min_memory, MIN_IOPS=128;
ob1.mysql CREATE RESOURCE POOL pool_$case_index UNIT = 'unit_$case_index', UNIT_NUM = $unit_num, ZONE_LIST = ($zone_list);
ob1.mysql CREATE TENANT tenant_$case_index RESOURCE_POOL_LIST = ('pool_$case_index'), locality=$locality;


--disable_warnings
--disable_query_log
--disable_result_log
sleep 5;
--error 0,1064
ob1.mysql alter tenant tenant_$case_index set variables ob_tcp_invited_nodes='%';
--enable_warnings
--enable_query_log
--enable_result_log
