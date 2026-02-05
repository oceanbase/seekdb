# Fork Table 专用的快速 Major Freeze 配置

connect (obsys_quick_major,$OBMYSQL_MS0,admin,$OBMYSQL_PWD,test,$OBMYSQL_PORT);
connection obsys_quick_major;

# 设置 merger_check_interval = 10s
alter system set merger_check_interval = '10s' tenant sys;

# 设置 ob_compaction_schedule_interval = 3s
alter system set ob_compaction_schedule_interval = '3s' tenant sys;
disconnect obsys_quick_major;
connection default;
