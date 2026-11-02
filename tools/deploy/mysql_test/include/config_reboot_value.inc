set @@session.ob_query_timeout = 200000000;
set @@session.ob_query_timeout = 10000000;
alter system set _enable_adaptive_compaction = false tenant sys;
alter system set _enable_adaptive_compaction = false tenant all_user;
alter system set _enable_adaptive_compaction = false tenant all_meta;
