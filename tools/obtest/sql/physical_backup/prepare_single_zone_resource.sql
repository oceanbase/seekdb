--disable_all_result_log
source sql/physical_backup/init_param.sql;

CREATE RESOURCE UNIT small_unit MIN_CPU=2, MAX_CPU=4, MEMORY_SIZE='4G';
CREATE RESOURCE UNIT medium_unit MIN_CPU=4, MAX_CPU=8, MEMORY_SIZE='8G';
CREATE RESOURCE UNIT large_unit MIN_CPU=8, MAX_CPU=16, MEMORY_SIZE='20G';

CREATE RESOURCE POOL small_pool UNIT small_unit, UNIT_NUM 1, ZONE_LIST ('z1');
CREATE RESOURCE POOL small_pool_0 UNIT small_unit, UNIT_NUM 1, ZONE_LIST ('z1');
CREATE RESOURCE POOL small_pool_1 UNIT small_unit, UNIT_NUM 1, ZONE_LIST ('z1');
CREATE RESOURCE POOL small_pool_2 UNIT small_unit, UNIT_NUM 1, ZONE_LIST ('z1');
--enable_all_result_log
