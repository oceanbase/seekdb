source sql/physical_backup/init_param.sql;

CREATE RESOURCE UNIT large_unit MAX_CPU=20, MEMORY_SIZE='80G';

CREATE RESOURCE POOL large_pool UNIT large_unit, UNIT_NUM 1, ZONE_LIST ('z1', 'z2', 'z3');