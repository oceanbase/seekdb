alter system set merge_thread_count = 64;
alter system set minor_merge_concurrency = 64;
alter system set memory_chunk_cache_size ='1G';
CREATE RESOURCE UNIT small_unit MIN_CPU=2, MAX_CPU=4, MEMORY_SIZE='1G';

CREATE RESOURCE POOL small_pool_0 UNIT small_unit, UNIT_NUM 1, ZONE_LIST ('z1');
CREATE RESOURCE POOL small_pool UNIT small_unit, UNIT_NUM 1, ZONE_LIST ('z1');

CREATE RESOURCE POOL small_pool_1 UNIT small_unit, UNIT_NUM 1, ZONE_LIST ('z1');
