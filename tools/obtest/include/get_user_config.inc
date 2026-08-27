# 替换sys_unit_config 为 user_unit_config
# 为适配 SS 场景, LOG_DISK_SIZE 设置为 SS 下最小值 3G
CREATE RESOURCE UNIT user_unit_config MAX_CPU = 1, MEMORY_SIZE = '2G', MAX_IOPS = 1024, LOG_DISK_SIZE = '3G';
