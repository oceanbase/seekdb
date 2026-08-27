--disable_all_result_log
source sql/physical_backup/init_param.sql;

CREATE RESOURCE UNIT small_unit MIN_CPU=1, MAX_CPU=1, MEMORY_SIZE='4G';

CREATE RESOURCE POOL multi_pool UNIT small_unit, UNIT_NUM 1, ZONE_LIST ('z1', 'z2', 'z3');

CREATE RESOURCE UNIT medium_unit MIN_CPU=4, MAX_CPU=8, MEMORY_SIZE='8G';

CREATE RESOURCE UNIT large_unit MIN_CPU=8, MAX_CPU=16, MEMORY_SIZE='20G';

CREATE RESOURCE POOL small_pool UNIT medium_unit, UNIT_NUM 1, ZONE_LIST ('z1','z2','z3');

CREATE RESOURCE POOL small_pool_0 UNIT small_unit, UNIT_NUM 1, ZONE_LIST ('z1','z2','z3');

CREATE RESOURCE POOL small_pool_1 UNIT small_unit, UNIT_NUM 1, ZONE_LIST ('z1','z2','z3');

CREATE RESOURCE POOL small_pool_2 UNIT small_unit, UNIT_NUM 1, ZONE_LIST ('z1','z2','z3');

--enable_all_result_log
