alter system set merger_check_interval='10s';
alter system set merge_thread_count = 64;
alter system set minor_merge_concurrency = 64;

