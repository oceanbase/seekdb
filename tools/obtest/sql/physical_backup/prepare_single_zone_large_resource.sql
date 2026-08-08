source sql/physical_backup/init_param.sql;

CREATE RESOURCE UNIT large_unit MIN_CPU=10, MAX_CPU=20, MEMORY_SIZE='100G';

CREATE RESOURCE POOL large_pool UNIT large_unit, UNIT_NUM 1, ZONE_LIST ('z1');
