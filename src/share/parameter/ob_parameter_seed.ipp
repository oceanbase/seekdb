/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "share/parameter/ob_parameter_macro.h"

#ifndef OB_CLUSTER_PARAMETER
#define OB_CLUSTER_PARAMETER(args...)
#endif

//// sstable config
// "/ob/storage/path/dir" means use local dir
// "ofs://0.0.0.0,1.1.1.1,2.2.2.2/dir" means use ofs dir
DEF_PARAM(data_dir, STR, OB_CLUSTER_PARAMETER, "store", "the directory for the data file",
        ObParameterAttr(Section::SSTABLE, Source::DEFAULT, EditLevel::READONLY));
DEF_PARAM(redo_dir, STR, OB_CLUSTER_PARAMETER, "", "the directory for the redo/clog file",
        ObParameterAttr(Section::SSTABLE, Source::DEFAULT, EditLevel::READONLY));
// background information about disk space configuration
// ObServerUtils::get_data_disk_info_in_config()
DEF_PARAM(datafile_size, CAP, OB_CLUSTER_PARAMETER, "32M", "[0M,)", "size of the data file. Range: [0, +∞)",
        ObParameterAttr(Section::SSTABLE, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(datafile_next, CAP, OB_CLUSTER_PARAMETER, "0M", "[0M,)", "the auto extend step. "
        "0 means using adaptive extend step size. Range: [0, +∞)",
        ObParameterAttr(Section::SSTABLE, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(datafile_maxsize, CAP, OB_CLUSTER_PARAMETER, "1T", "[0M,)", "the auto extend max size. Range: [0, +∞)",
        ObParameterAttr(Section::SSTABLE, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(datafile_disk_percentage, INT, OB_CLUSTER_PARAMETER, "0", "[0,99]",
        "the percentage of disk space used by the data files. Range: [0,99] in integer",
        ObParameterAttr(Section::SSTABLE, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_datafile_usage_upper_bound_percentage, INT, OB_CLUSTER_PARAMETER, "90", "[5,99]",
        "the percentage of disk space usage upper bound to trigger datafile extend. Range: [5,99] in integer",
        ObParameterAttr(Section::SSTABLE, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
//// observer config
DEF_PARAM(enable_rpc_tls, BOOL, OB_CLUSTER_PARAMETER, "False",
        "specifies whether mutual TLS (mTLS) is enabled for inter-node RPC communication. "
        "When True, certificates must exist in the wallet directory. "
        "Value: True: enabled False: disabled",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::STATIC_EFFECTIVE));
DEF_PARAM(rpc_port, INT, OB_CLUSTER_PARAMETER, "2882", "(1024,65536)",
        "the port number for RPC protocol. Range: (1024, 65536) in integer",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(mysql_port, INT, OB_CLUSTER_PARAMETER, "2881", "(1024,65536)",
        "port number for mysql connection. Range: (1024, 65536) in integer",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(internal_sql_execute_timeout, TIME, OB_CLUSTER_PARAMETER, "30s", "[1000us, 1h]",
         "the number of microseconds an internal DML request is permitted to "
         "execute before it is terminated. Range: [1000us, 1h]",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(net_thread_count, INT, OB_CLUSTER_PARAMETER, "0", "[0,128]",
        "the number of MySQL I/O threads. Range: [0, 128] in integer, 0 selects an adaptive value",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::STATIC_EFFECTIVE));
DEF_PARAM(server_task_queue_size, INT, OB_CLUSTER_PARAMETER, "16384", "[1024,]",
        "the size of the local server runtime task queue. Range: [1024,+∞)",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_memory_budget, CAP_WITH_CHECKER, OB_CLUSTER_PARAMETER, "0M",
        common::MemoryBudgetConfigChecker, "[0M,)",
        "the logical memory budget used to size caches and buffers. "
        "0 means max(1G, 50% of effective system memory), where effective memory is "
        "the smaller of physical memory and the cgroup memory limit. Range: 0, [1G,).",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(memory_limit, CAP_WITH_CHECKER, OB_CLUSTER_PARAMETER, "0M",
        common::MemoryBudgetConfigChecker, "[0M,)",
        "deprecated compatibility parameter. The configured value is accepted and persisted, "
        "but is ignored by memory sizing and memory control. Range: 0, [1G,).",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(kvcache_memory_limit, CAP_WITH_CHECKER, OB_CLUSTER_PARAMETER, "0M",
        common::KVCacheMemoryLimitConfigChecker, "[0M,1T]",
        "the maximum memory used by KV cache. 0 derives the limit from effective system memory. "
        "The automatic value is min(1T, 30% of effective system memory). "
        "A dynamic increase cannot exceed the capacity reserved at startup. Range: [0M,1T].",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(memstore_memory_limit, CAP, OB_CLUSTER_PARAMETER, "0M", "[0M,)",
        "the maximum memory used by Memstore. 0 derives the limit from _memory_budget. "
        "The automatic value is 80% of _memory_budget. "
        "Range: [0M,).",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(vector_memory_limit, CAP, OB_CLUSTER_PARAMETER, "0M", "[0M,)",
        "the maximum memory used by the vector module. 0 derives the limit from _memory_budget. "
        "The automatic value is 80% of _memory_budget. "
        "Range: [0M,).",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(cpu_count, INT, OB_CLUSTER_PARAMETER, "0", "[0,]",
        "the number of CPU\\'s in the system. "
        "If this parameter is set to zero, the number will be set according to sysconf; "
        "otherwise, this parameter is used. Range: [0,+∞) in integer",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(trace_log_slow_query_watermark, TIME, OB_CLUSTER_PARAMETER, "1s", "[1ms,]",
        "the threshold of execution time (in milliseconds) of a query beyond "
        "which it is considered to be \\'slow query\\'. Range: [1ms,+∞)",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(enable_record_trace_log, BOOL, OB_CLUSTER_PARAMETER, "True",
         "specifies whether to always record the trace log. The default value is True.",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(max_string_print_length, INT, OB_CLUSTER_PARAMETER, "500", "[0,]",
        "truncate very long string when printing to log file. Range:[0,]",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(enable_record_trace_id, BOOL, OB_CLUSTER_PARAMETER, "False",
         "specifies whether record app trace id is turned on.",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(enable_rich_error_msg, BOOL, OB_CLUSTER_PARAMETER, "false",
         "specifies whether add ip:port, time and trace id to user error message. "
         "The default value is FALSE.",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(debug_sync_timeout, TIME, OB_CLUSTER_PARAMETER, "0", "[0,)",
         "Enable the debug sync facility and "
         "optionally specify a default wait timeout in micro seconds. "
         "A zero value keeps the facility disabled, Range: [0, +∞]",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(dead_socket_detection_timeout, TIME, OB_CLUSTER_PARAMETER, "3s", "[0s,2h]",
         "specify a tcp_user_timeout for RFC5482. "
         "A zero value makes the option disabled, Range: [0, 2h]",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(enable_perf_event, BOOL, OB_CLUSTER_PARAMETER, "True",
         "specifies whether to enable perf event feature. The default value is True.",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(schema_history_expire_time, TIME, OB_CLUSTER_PARAMETER, "7d", "[1m, 30d]",
         "the expire time for schema history, from 1min to 30days, "
         "with default 7days. Range: [1m, 30d]",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(default_compress_func, STR_WITH_CHECKER, OB_CLUSTER_PARAMETER, "zstd_1.3.8", common::ObConfigCompressFuncChecker,
                     "default compress function name for create new table, "
                     "values: none, zlib_1.0, zstd_1.3.8",
                     ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE),
                     "none, zlib_1.0, zstd_1.3.8");

DEF_PARAM(default_row_format, STR_WITH_CHECKER, OB_CLUSTER_PARAMETER, "dynamic", common::ObConfigRowFormatChecker,
                     "default row format in mysql mode",
                     ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE),
                     "REDUNDANT, COMPACT, DYNAMIC, COMPRESSED, CONDENSED");

DEF_PARAM(storage_rowsets_size, INT, OB_CLUSTER_PARAMETER, "8192", "(0,1048576]",
        "the row number processed by vectorized storage engine within one batch. Range: (0,1048576]",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(weak_read_version_refresh_interval, TIME, OB_CLUSTER_PARAMETER, "1000ms", "[50ms,)",
         "the time interval to refresh cluster weak read version "
         "Range: [50ms, +∞)",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(syslog_level, LOG_LEVEL, OB_CLUSTER_PARAMETER, ObLogger::get_level_str(DEFAULT_LOG_LEVEL), "specifies the current level of logging. There are DEBUG, TRACE, WDIAG, EDIAG, INFO, WARN, ERROR, seven different log levels.",
              ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE),
             "DEBUG, TRACE, WDIAG, EDIAG, INFO, WARN, ERROR");
DEF_PARAM(syslog_io_bandwidth_limit, CAP, OB_CLUSTER_PARAMETER, "5MB",
        "Syslog IO bandwidth limitation, exceeding syslog would be truncated. Use 0 to disable ERROR log.",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(diag_syslog_per_error_limit, INT, OB_CLUSTER_PARAMETER, "200", "[0,]",
        "DIAG syslog limitation for each error per second, exceeding syslog would be truncated",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(max_syslog_file_count, INT_WITH_CHECKER, OB_CLUSTER_PARAMETER, "2", common::ObConfigMaxSyslogFileCountChecker,
                     "specifies the maximum number of the log files "
                     "that can co-exist before the log file recycling kicks in. "
                     "Each log file can occupy at most 256MB disk space. "
                     "When this value is set to 0, no log file will be removed. Range: [0, +∞) in integer",
                     ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(enable_async_syslog, BOOL, OB_CLUSTER_PARAMETER, "True",
         "specifies whether use async log for observer.log, elec.log and rs.log",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(syslog_disk_size, CAP, OB_CLUSTER_PARAMETER, "0M", "[0M,)",
        "the size of disk space used by the syslog files. Range: [0, +∞)",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(syslog_compress_func, STR_WITH_CHECKER, OB_CLUSTER_PARAMETER, "none", common::ObConfigSyslogCompressFuncChecker,
                     "compress function name for syslog files, "
                     "values: none, zstd_1.3.8",
                     ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE),
                     "none, zstd_1.3.8");
DEF_PARAM(syslog_file_uncompressed_count, INT_WITH_CHECKER, OB_CLUSTER_PARAMETER, "0", common::ObConfigSyslogFileUncompressedCountChecker,
                     "specifies the minimum number of the syslog files that will not be compressed. "
                     "Each syslog file can occupy at most 256MB disk space. "
                     "When this value is set to 0, all syslog file may be compressed. Range: [0, +∞) in integer",
                     ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(cache_wash_threshold, CAP, OB_CLUSTER_PARAMETER, "64M", "[0B,]",
        "size of remaining memory at which cache eviction will be triggered. Range: [0,+∞)",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(memory_chunk_cache_size, CAP, OB_CLUSTER_PARAMETER, "0M", "[0M,]", "the maximum size of memory cached by memory chunk cache. Range: [0M,], 0 stands for adaptive",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(enable_sql_operator_dump, BOOL, OB_CLUSTER_PARAMETER, "True", "specifies whether sql operators "
         "(sort/hash join/material/window function/interm result/...) allowed to write to disk",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_chunk_row_store_mem_limit, CAP, OB_CLUSTER_PARAMETER, "0M", "[0M,]",
        "the maximum size of memory used by ChunkRowStore, 0 means follow operator's setting. Range: [0, +∞)",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_sort_area_size, CAP, OB_CLUSTER_PARAMETER, "32M", "[2M,]",
        "size of maximum memory that could be used by SORT. Range: [2M,+∞)",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_hash_area_size, CAP, OB_CLUSTER_PARAMETER, "32M", "[4M,]",
        "size of maximum memory that could be used by HASH JOIN. Range: [4M,+∞)",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

//
DEF_PARAM(_enable_partition_level_retry, BOOL, OB_CLUSTER_PARAMETER, "True",
         "specifies whether allow the partition level retry when the leader changes",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
//
DEF_PARAM(_enable_defensive_check, INT_WITH_CHECKER, OB_CLUSTER_PARAMETER, "1", common::ObConfigEnableDefensiveChecker,
                     "specifies whether allow to do some defensive checks when the query is executed, "
                     "0 means defensive check is disabled, "
                     "1 means normal defensive check is enabled, "
                     "2 means more strict defensive check is enabled, such as check partition id validity",
                     ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

//
DEF_PARAM(_sql_insert_multi_values_split_opt, BOOL, OB_CLUSTER_PARAMETER, "True",
         "True means that the split + batch optimization for inserting multiple rows of the insert values ​​statement can be done",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(_min_malloc_sample_interval, INT, OB_CLUSTER_PARAMETER, "16", "[1, 10000]",
        "the min malloc times between two samples, "
        "which is not more than _max_malloc_sample_interval. "
        "10000 means not to sample any malloc, Range: [1, 10000]",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_max_malloc_sample_interval, INT, OB_CLUSTER_PARAMETER, "256", "[1, 10000]",
        "the max malloc times between two samples, "
        "which is not less than _min_malloc_sample_interval. "
        "1 means to sample all malloc, Range: [1, 10000]",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_enable_values_table_folding, BOOL, OB_CLUSTER_PARAMETER, "True",
         "whether enable values statement folds self params",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

//https://yuque.antfin-inc.com/ob/product_functionality_review/lfdrc64b0xpv79bw
DEF_PARAM(spill_compression_codec, STR_WITH_CHECKER, OB_CLUSTER_PARAMETER, "NONE", common::ObConfigSQLSpillCompressionCodecChecker,
        "specific the compression algorithm type to compress the spilled data in temp block store "\
        "during the sql execution phase. "\
        "The supported compression codec is ZSTD. NONE means no compression."\
        "The default value is NONE.",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE),
        "zstd");

//// server runtime config
DEF_PARAM(max_stale_time_for_weak_consistency, TIME_WITH_CHECKER, OB_CLUSTER_PARAMETER, "5s", common::ObConfigStaleTimeChecker,
                      "[5s,)",
                      "the max data stale time that cluster weak read version behind current timestamp,"
                      "no smaller than weak_read_version_refresh_interval, range: [5s, +∞)",
                      ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(enable_monotonic_weak_read, BOOL, OB_CLUSTER_PARAMETER, "false",
         "specifies observer supportting atomicity and monotonic order read",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(server_cpu_quota_min, DBL, OB_CLUSTER_PARAMETER, "0", "[0,16]",
        "the minimum number of vCPUs allocated to the server runtime. "
        "0 stands for adaptive. Range: [0, 16]",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(server_cpu_quota_max, DBL, OB_CLUSTER_PARAMETER, "0", "[0,16]",
        "the maximum number of vCPUs allocated to the server runtime. "
        "0 stands for adaptive. Range: [0, 16]",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(workers_per_cpu_quota, INT, OB_CLUSTER_PARAMETER, "10", "[2,20]",
        "the ratio(integer) between the number of system allocated workers vs "
        "the maximum number of threads that can be scheduled concurrently. Range: [2, 20]",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_ob_max_thread_num, INT, OB_CLUSTER_PARAMETER, "0", "[0,10000)",
         "ob max thread number "
         "upper limit of observer thread count. Range: [0, 10000), 0 means no limit.",
         ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(cpu_quota_concurrency, DBL, OB_CLUSTER_PARAMETER, "10", "[1,20]",
        "max allowed concurrency for 1 CPU quota. Range: [1,20]",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(px_workers_per_cpu_quota, INT, OB_CLUSTER_PARAMETER, "10", "[0,20]",
        "the ratio(integer) between the number of system allocated px workers vs "
        "the maximum number of threads that can be scheduled concurrently. Range: [0, 20]",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(undo_retention, INT, OB_CLUSTER_PARAMETER, "1800", "[0, 4294967295]",
        "the low threshold value of undo retention. The system retains undo for at least the time specified in this config when active txn protection is banned. Range: [0, 4294967295]",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_mvcc_gc_using_min_txn_snapshot, BOOL, OB_CLUSTER_PARAMETER, "True",
        "specifies enable mvcc gc using active txn snapshot",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_rowsets_target_maxsize, INT, OB_CLUSTER_PARAMETER, "524288", "[262144, 8388608]",
        "the size of the memory reserved for vectorized sql engine. Range: [262144, 8388608]",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_rowsets_max_rows, INT, OB_CLUSTER_PARAMETER, "256", "[0, 65535]",
        "the row number processed by vectorized sql engine within one batch. Range: [0, 65535]",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_ctx_memory_limit, STR_WITH_CHECKER, OB_CLUSTER_PARAMETER, "", common::ObCtxMemoryLimitChecker,
        "specifies server runtime context memory limits.",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_enable_convert_real_to_decimal, BOOL, OB_CLUSTER_PARAMETER, "False",
         "specifies whether convert column type float(M,D), double(M,D) to decimal(M,D) in DDL",
         ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_enable_decimal_int_type, BOOL, OB_CLUSTER_PARAMETER, "True",
         "specifies wether use decimal_int type as backend for decimal values",
         ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_optimizer_ads_time_limit, INT, OB_CLUSTER_PARAMETER, "10", "[0, 300]",
        "the maximum optimizer dynamic sampling time limit. Range: [0, 300]",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_hash_join_enabled, BOOL, OB_CLUSTER_PARAMETER, "True",
         "enable/disable hash join",
         ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_optimizer_sortmerge_join_enabled, BOOL, OB_CLUSTER_PARAMETER, "True",
         "enable/disable merge join",
         ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
//
DEF_PARAM(_nested_loop_join_enabled, BOOL, OB_CLUSTER_PARAMETER, "True",
  "enable/disable nested loop join",
  ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
//
DEF_PARAM(_enable_mysql_compatible_dates, BOOL, OB_CLUSTER_PARAMETER, "True",
  "Specifies whether to use MySQL-compatible date format that allows for invalid dates.",
  ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
//
DEF_PARAM(_min_const_integer_precision, INT, OB_CLUSTER_PARAMETER, "1", "[1, 20]",
        "the minimum precision of integer constant",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(_lob_rowsets_max_rows, INT, OB_CLUSTER_PARAMETER, "65535", "[1, 65535]",
        "max batch size of physical plan with lob data",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_use_hash_rollup, STR_WITH_CHECKER, OB_CLUSTER_PARAMETER, "AUTO", common::ObConfigEnableHashRollupChecker,
        "policy of hash based rollup plan:"\
        "AUTO: hash rollup plan is up to optimizer;"\
        "FORCED: hash rollup plan is used by default;"\
        "DISABLED: hash rollup plan is disabled;",
         ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE),
         "auto, forced, disabled");

//https://yuque.antfin.com/ob/product_functionality_review/quy4ol4wtu9ihkpx
DEF_PARAM(_enable_constant_type_demotion, BOOL, OB_CLUSTER_PARAMETER, "True",
         "Controls whether to enable constant type demotion to optimize comparisons between "
         "constants and columns by downgrading the constant's type to match the column's type.",
         ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_non_standard_comparison_level, STR_WITH_CHECKER, OB_CLUSTER_PARAMETER, "NONE", common::ObConfigNonStdCmpLevelChecker,
        "Enable non-standard comparisons to optimize filtering by aligning constants with column "
        "types. Currently only affects comparisons between string columns and int constants "
        "NONE: all comparison types use standard comparison. "
        "EQUAL: non-standard comparisons rules will applied in equal conditions. "
        "RANGE: non-standard comparisons rules will applied in range conditions. ",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE),
        "none, equal, range");

// Kept only for compatibility with tools that still set this retired parameter.
DEF_PARAM(_memstore_limit_percentage, INT, OB_CLUSTER_PARAMETER, "0", "[0, 100)",
        "Deprecated compatibility parameter. The configured value is accepted and persisted, "
        "but is ignored by memstore sizing and memory control.",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(freeze_trigger_percentage, INT, OB_CLUSTER_PARAMETER, "20", "(0, 100)",
        "the threshold of the size of the mem store when freeze will be triggered. Rang:(0,100)",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(writing_throttling_trigger_percentage, INT, OB_CLUSTER_PARAMETER, "60", "(0, 100]",
          "the threshold of the size of the mem store when writing_limit will be triggered. Rang:(0,100]. setting 100 means turn off writing limit",
          ObParameterAttr(Section::TRANS, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(data_disk_write_limit_percentage, INT, OB_CLUSTER_PARAMETER, "0", "[0, 100)",
        "used to stop user write operations. "
        "When the user data disk reaches this watermark, SQL requests will report that the disk is full. "
        "The configuration should be greater than data_disk_usage_limit_percentage, "
        "with the recommended setting being: (1 - memstore_limit_size / data_disk_size) * 100%",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(writing_throttling_maximum_duration, TIME, OB_CLUSTER_PARAMETER, "2h", "[1s, 3d]",
          "maximum duration of writting throttling(in minutes), max value is 3 days",
          ObParameterAttr(Section::TRANS, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(plan_cache_evict_interval, TIME, OB_CLUSTER_PARAMETER, "5s", "[0s,)",
         "time interval for periodic plan cache eviction. Range: [0s, +∞)",
         ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(default_progressive_merge_num, INT, OB_CLUSTER_PARAMETER, "0", "[0,)",
         "default progressive_merge_num for a newly created table. "
         "Range:[0,)",
         ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_parallel_min_message_pool, CAP, OB_CLUSTER_PARAMETER, "16M", "[16M, 8G]",
        "DTL message buffer pool reserve the mininum size after extend the size. Range: [16M,8G]",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_parallel_server_sleep_time, DBL, OB_CLUSTER_PARAMETER, "1", "[0, 2000]",
        "sleep time between get channel data in millisecond. Range: [0, 2000]",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_px_max_message_pool_pct, DBL, OB_CLUSTER_PARAMETER, "40", "[0,90]",
        "The maximum percentage of server runtime memory available to the DTL message buffer pool. Range: [0,90]",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_px_message_compression, BOOL, OB_CLUSTER_PARAMETER, "True",
        "Enable DTL send message with compression"
        "Value: True: enable compression False: disable compression",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_px_chunklist_count_ratio, INT, OB_CLUSTER_PARAMETER, "1", "[1, 128]",
        "the ratio of the dtl buffer manager list. Range: [1, 128]",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_sqlexec_disable_hash_based_distagg_tiv, BOOL, OB_CLUSTER_PARAMETER, "False",
         "disable hash based distinct aggregation in the second stage of three stage aggregation for gby queries"
         "Value:  True:turned on  False: turned off",
         ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_force_hash_groupby_dump, BOOL, OB_CLUSTER_PARAMETER, "False",
         "force hash groupby to dump"
         "Value:  True:turned on  False: turned off",
         ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_force_hash_join_spill, BOOL, OB_CLUSTER_PARAMETER, "False",
         "force hash join to dump after get all build hash table "
         "Value:  True:turned on  False: turned off",
         ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_enable_hash_join_hasher, INT, OB_CLUSTER_PARAMETER, "1", "[1, 7]",
         "which hash function to choose for hash join "
         "1: murmurhash, 2: crc, 4: xxhash",
         ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_enable_hash_join_processor, INT, OB_CLUSTER_PARAMETER, "7", "[1, 7]",
         "which path to process for hash join, default 7 to auto choose "
         "1: nest loop, 2: recursive, 4: in-memory",
         ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_pushdown_storage_level, INT, OB_CLUSTER_PARAMETER, "4", "[0, 4]",
        "the level of storage pushdown. Range: [0, 4] "
        "0: disabled, 1:blockscan, 2: blockscan & filter, 3: blockscan & filter & aggregate, 4: blockscan & filter & aggregate & group by",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(workarea_size_policy, WORK_AREA_POLICY, OB_CLUSTER_PARAMETER, "AUTO", "policy used to size SQL working areas (MANUAL/AUTO)",
              ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(temporary_file_max_disk_size, CAP, OB_CLUSTER_PARAMETER, "0M", "[0,)",
        "maximum disk usage of temporary file on a single node, 0 means no limit. "
        "Range: [0,+∞)",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_temporary_file_io_area_size, INT, OB_CLUSTER_PARAMETER, "1", "[0, 50)",
         "memory buffer size of temporary files, as a percentage of total server runtime memory. "
         "Range: [0, 50), percentage",
         ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_storage_meta_memory_limit_percentage, INT, OB_CLUSTER_PARAMETER, "20", "[0, 50)",
         "maximum memory for storage metadata, as a percentage of total server runtime memory. "
         "Range: [0, 50), percentage, 0 means no limit to storage meta memory",
         ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_max_tablet_cnt_per_gb, INT, OB_CLUSTER_PARAMETER, "20000", "[1000, 50000)",
         "The maximum number of tablets supported per 1GB of server runtime memory. Range: [1000, 50000)",
         ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_enable_filter_reordering, BOOL, OB_CLUSTER_PARAMETER, "True",
         "enable filter reordering in storage engine",
         ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(enable_sys_table_ddl, BOOL, OB_CLUSTER_PARAMETER, "False",
         "specifies whether a \\'system\\' table is allowed be to created manually. "
         "Value: True: allowed; False: not allowed",
         ObParameterAttr(Section::ROOT_SERVICE, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(tablet_meta_table_scan_batch_count, INT, OB_CLUSTER_PARAMETER, "999", "(0, 65536]",
        "the number of tablet replica info "
        "that will be read by each request on the tablet-related system tables "
        "during procedures such as load-balancing, daily merge, election and etc. "
        "Range:(0,65536]",
        ObParameterAttr(Section::ROOT_SERVICE, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(tablet_meta_table_check_interval, TIME, OB_CLUSTER_PARAMETER, "30m", "[1m,)",
         "the time interval that observer compares tablet meta table with local ls replica info "
         "and make adjustments to ensure the correctness of tablet meta table. Range: [1m,+∞)",
         ObParameterAttr(Section::ROOT_SERVICE, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(enable_ddl, BOOL, OB_CLUSTER_PARAMETER, "True", "specifies whether DDL operation is turned on. "
         "Value:  True:turned on;  False: turned off",
         ObParameterAttr(Section::ROOT_SERVICE, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_enable_parallel_table_creation, BOOL, OB_CLUSTER_PARAMETER, "True", "specifies whether create table parallelly. "
         "Value:  True: create table parallelly;  False: create table serially",
         ObParameterAttr(Section::ROOT_SERVICE, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(enable_major_freeze, BOOL, OB_CLUSTER_PARAMETER, "True", "specifies whether major_freeze function is turned on. "
         "Value:  True:turned on;  False: turned off",
         ObParameterAttr(Section::ROOT_SERVICE, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(ob_event_history_recycle_interval, TIME, OB_CLUSTER_PARAMETER, "7d", "[1d, 180d]",
         "the time to recycle event history. Range: [1d, 180d]",
         ObParameterAttr(Section::ROOT_SERVICE, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_recyclebin_object_purge_frequency, TIME, OB_CLUSTER_PARAMETER, "10m", "[0m,)",
         "the time to purge recyclebin. Range: [0m, +∞)",
         ObParameterAttr(Section::ROOT_SERVICE, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(recyclebin_object_expire_time, TIME, OB_CLUSTER_PARAMETER, "0s", "[0s,)",
         "recyclebin object expire time, "
         "default 0 that means auto purge recyclebin off. Range: [0s, +∞)",
         ObParameterAttr(Section::ROOT_SERVICE, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(_parallel_ddl_control, MODE_WITH_PARSER, OB_CLUSTER_PARAMETER, "",
        common::ObParallelDDLControlParser,
        "switch for parallel capability of parallel DDL",
        ObParameterAttr(Section::ROOT_SERVICE, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
// ========================= LogService Config Begin =====================

DEF_PARAM(log_disk_size, CAP, OB_CLUSTER_PARAMETER, "0M", "[0M,)",
        "the size of disk space used by the log files. Range: [0, +∞)",
        ObParameterAttr(Section::LOGSERVICE, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(log_disk_percentage, INT, OB_CLUSTER_PARAMETER, "0", "[0,99]",
        "the percentage of disk space used by the log files. Range: [0,99] in integer;"
        "only effective when parameter log_disk_size is 0;"
        "when log_disk_percentage is 0:"
        " a) if the data and the log are on the same disk, means log_disk_percentage = 30"
        " b) if the data and the log are on the different disks, means log_disk_perecentage = 90",
        ObParameterAttr(Section::LOGSERVICE, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(log_disk_utilization_limit_threshold, INT, OB_CLUSTER_PARAMETER, "95",
        "[80, 100]",
        "maximum of log disk usage percentage before stop submitting or receiving logs, "
        "should be bigger than log_disk_utilization_threshold. "
        "Range: [80, 100]",
        ObParameterAttr(Section::LOGSERVICE, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(log_disk_utilization_threshold, INT, OB_CLUSTER_PARAMETER,"0",
        "[0, 100)",
        "log disk utilization threshold before reuse log files, "
        "should be smaller than log_disk_utilization_limit_threshold. "
        "0 means recycle log files as soon as possible"
        "Range: [0, 100)",
        ObParameterAttr(Section::LOGSERVICE, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(log_disk_throttling_percentage, INT, OB_CLUSTER_PARAMETER, "60",
        "[40, 100]",
        "the threshold of the size of the log disk when writing_limit will be triggered. Rang:[40,100]. setting 100 means turn off writing limit",
        ObParameterAttr(Section::LOGSERVICE, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(log_disk_throttling_maximum_duration, TIME, OB_CLUSTER_PARAMETER, "2h", "[1s, 3d]",
          "maximum duration of log disk throttling, that is the time remaining until the log disk space is exhausted after log disk throttling triggered.",
          ObParameterAttr(Section::LOGSERVICE, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(log_storage_warning_tolerance_time, TIME, OB_CLUSTER_PARAMETER, "5s",
        "[1s,300s]",
        "time to tolerate log disk io delay, after that, the disk status will be set warning. "
        "Range: [1s,300s]",
        ObParameterAttr(Section::LOGSERVICE, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(_enable_log_cache, BOOL, OB_CLUSTER_PARAMETER, "True",
         "specifies whether allow to fill log kv cache. "
         "Value:  True:turned on  False: turned off",
         ObParameterAttr(Section::LOGSERVICE, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

// ========================= LogService Config End   =====================
DEF_PARAM(_lcl_op_interval, TIME, OB_CLUSTER_PARAMETER, "5s", "[0ms, 5s]",
         "Scan interval for every detector node, smaller interval support larger deadlock scale, but cost more system resource. "
         "0ms means disable deadlock, default value is 5s. Range:[0ms, 5s]",
         ObParameterAttr(Section::TRANS, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

//// daily merge  config
// set to disable if don't want major freeze launch auto
DEF_PARAM(major_freeze_duty_time, MOMENT, OB_CLUSTER_PARAMETER, "02:00",
           "the start time of system daily merge procedure. Range: [00:00, 24:00)",
           ObParameterAttr(Section::DAILY_MERGE, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(merger_check_interval, TIME, OB_CLUSTER_PARAMETER, "10m", "[10s, 60m]",
         "the time interval between the schedules of the task "
         "that checks on the progress of MERGE. Range: [10s, 60m]",
         ObParameterAttr(Section::DAILY_MERGE, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
//// transaction config
DEF_PARAM(trx_commit_retry_interval, TIME, OB_CLUSTER_PARAMETER, "100ms", "[1ms, 5000ms]",
         "the time interval between the retries in case of failure "
         "during a transaction commit. Range: [1ms,5000ms]",
         ObParameterAttr(Section::TRANS, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(clog_sync_time_warn_threshold, TIME, OB_CLUSTER_PARAMETER, "100ms", "[1ms, 10000ms]",
         "the time given to the commit log synchronization between a leader and its followers "
         "before a \\'warning\\' message is printed in the log file.  Range: [1ms,1000ms]",
         ObParameterAttr(Section::TRANS, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(row_compaction_update_limit, INT, OB_CLUSTER_PARAMETER, "6", "[1, 6400]",
        "maximum update count before trigger row compaction. "
        "Range: [1, 6400]",
        ObParameterAttr(Section::TRANS, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(enable_early_lock_release, BOOL, OB_CLUSTER_PARAMETER, "True",
         "enable early lock release",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_tx_result_retention, INT, OB_CLUSTER_PARAMETER, "300", "[0, 36000]",
        "The tx data can be recycled after at least _tx_result_retention seconds. "
        "Range: [0, 36000]",
        ObParameterAttr(Section::TRANS, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_enable_parallel_redo_logging, BOOL, OB_CLUSTER_PARAMETER, "True",
         "enable parallel write redo log.",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_parallel_redo_logging_trigger, CAP, OB_CLUSTER_PARAMETER, "16M", "[0B,)",
        "size of single transaction's pending redo log to trigger parallel writes redo log. "
        "Range: [0B,+∞)",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_tx_debug_level, INT, OB_CLUSTER_PARAMETER, "0", "[0, 10]",
        "the debug level of transaction module. Range: [0, 10] in integer.",
        ObParameterAttr(Section::TRANS, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

//// rpc config
DEF_PARAM(rpc_timeout, TIME, OB_CLUSTER_PARAMETER, "2s",
         "the time during which a RPC request is permitted to execute before it is terminated",
         ObParameterAttr(Section::RPC, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
//// location cache config
DEF_PARAM(location_cache_refresh_sql_timeout, TIME, OB_CLUSTER_PARAMETER, "1s", "[1ms,)",
        "The timeout used for refreshing location cache by SQL. Range: [1ms, +∞)",
        ObParameterAttr(Section::LOCATION_CACHE, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

//// cache config
DEF_PARAM(bf_cache_miss_count_threshold, INT, OB_CLUSTER_PARAMETER, "100", "[0,)", "bf cache miss count threshold, 0 means disable bf cache. Range:[0, )",
        ObParameterAttr(Section::CACHE, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

//background limit config
DEF_PARAM(_data_storage_io_timeout, TIME, OB_CLUSTER_PARAMETER, "10s", "[1s,600s]",
        "io timeout for data storage, Range [1s,600s]. "
        "The default value is 10s",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(data_storage_warning_tolerance_time, TIME, OB_CLUSTER_PARAMETER, "5s", "[1s,300s]",
        "time to tolerate disk read failure, after that, the disk status will be set warning. Range [1s,300s]. The default value is 5s",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(data_disk_usage_limit_percentage, INT, OB_CLUSTER_PARAMETER, "90", "[50,100]",
        "the safe use percentage of data disk"
        "Range: [50,100] in integer",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(sys_bkgd_net_percentage, INT, OB_CLUSTER_PARAMETER, "60", "[0,100]",
        "the net percentage of sys background net. Range: [0, 100] in integer",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(disk_io_thread_count, INT_WITH_CHECKER, OB_CLUSTER_PARAMETER, "1", common::ObConfigEvenIntChecker,
                     "[1,32]",
                     "The number of io threads on each disk. The default value is 8. Range: [2,32] in even integer",
                     ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(sync_io_thread_count, INT, OB_CLUSTER_PARAMETER, "0",
        "[0,1024]",
        "The number of io threads for synchronizing request on each device. The default value is 0. Range: [0,1024] in integer",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_io_callback_thread_count, INT, OB_CLUSTER_PARAMETER, "0", "[0,128]",
        "The number of io callback threads. The default value is 0. Range: [0,128] in integer. If not specified, The number of threads is dynamically configured according to the memory size",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(_enable_parallel_minor_merge, BOOL, OB_CLUSTER_PARAMETER, "True",
         "specifies whether enable parallel minor merge. "
         "Value: True:turned on;  False: turned off",
         ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_enable_adaptive_compaction, BOOL, OB_CLUSTER_PARAMETER, "True",
         "specifies whether allow adaptive compaction schedule and information collection",
         ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(compaction_dag_cnt_limit, INT, OB_CLUSTER_PARAMETER, "50000", "[10000,500000]",
        "the compaction dag count limit. Range: [10000,500000] in integer. default value is 50000",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(compaction_schedule_tablet_batch_cnt, INT, OB_CLUSTER_PARAMETER, "50000", "[10000,500000]",
        "the batch size when scheduling tablet to execute compaction task. Range: [10000,500000] in integer. default value is 50000",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(compaction_low_thread_score, INT, OB_CLUSTER_PARAMETER, "0", "[0,100]",
        "the current work thread score of low priority compaction. Range: [0,100] in integer. Especially, 0 means default value",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(compaction_mid_thread_score, INT, OB_CLUSTER_PARAMETER, "0", "[0,100]",
        "the current work thread score of middle priority compaction. Range: [0,100] in integer. Especially, 0 means default value",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(compaction_high_thread_score, INT, OB_CLUSTER_PARAMETER, "0", "[0,100]",
        "the current work thread score of high priority compaction. Range: [0,100] in integer. Especially, 0 means default value",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(storage_high_thread_score, INT, OB_CLUSTER_PARAMETER, "0", "[0,100]",
        "the current work thread score of high priority storage tasks. Range: [0,100] in integer. Especially, 0 means default value",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(ddl_thread_score, INT, OB_CLUSTER_PARAMETER, "0", "[0,100]",
        "the current work thread score of ddl thread. Range: [0,100] in integer. Especially, 0 means default value",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(minor_compact_trigger, INT, OB_CLUSTER_PARAMETER, "2", "[0,16]",
        "minor_compact_trigger, Range: [0,16] in integer",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_enable_compaction_diagnose, BOOL, OB_CLUSTER_PARAMETER, "False",
         "enable compaction diagnose function"
         "Value:  True:turned on;  False: turned off",
         ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_force_skip_encoding_partition_id, STR, OB_CLUSTER_PARAMETER, "",
        "force the specified partition to major without encoding row store, only for emergency!",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_private_buffer_size, CAP, OB_CLUSTER_PARAMETER, "16K", "[0B,)"
         "the trigger remaining data size within transaction for immediate logging, 0B represents not trigger immediate logging"
         "Range: [0B, total size of memory]",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_fast_commit_callback_count, INT, OB_CLUSTER_PARAMETER, "10000", "[0,)"
        "trigger max callback count allowed within transaction for durable callback checkpoint, 0 represents not allow durable callback"
        "Range: [0, not limited callback count",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_trx_max_log_cb_limit, INT, OB_CLUSTER_PARAMETER, "16", "[0,)",
        "Control the upper limit of TxLogCbs involved in the participant to manage the maximum "
        "concurrency of  submiting logs in a transaction",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_minor_compaction_amplification_factor, INT, OB_CLUSTER_PARAMETER, "0", "[0,100]",
        "thre L1 compaction write amplification factor, 0 means default 25, Range: [0,100] in integer",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(major_compact_trigger, INT, OB_CLUSTER_PARAMETER, "0", "[0,65535]",
        "specifies how many minor freeze should be triggered between two major freeze, Range: [0,65535] in integer",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_minor_merge_write_amplification_threshold, INT, OB_CLUSTER_PARAMETER, "2000000", "[0,)",
         "The write amplification threshold about minor compaction. The larger the value, the smaller the write amplicification",
         ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(ob_compaction_schedule_interval, TIME, OB_CLUSTER_PARAMETER, "120s", "[3s,5m]",
         "the time interval to schedule compaction, Range: [3s,5m]"
         "Range: [3s, 5m]",
         ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_ob_elr_fast_freeze_threshold, INT, OB_CLUSTER_PARAMETER, "500000", "[10000,)",
         "per row update counts threshold to trigger minor freeze for tables with ELR optimization",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_ob_enable_fast_freeze, BOOL, OB_CLUSTER_PARAMETER, "True",
         "specifies whether fast freeze is enabled for the server runtime. "
         "Value: True:turned on;  False: turned off",
         ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_compaction_prewarm_percentage, INT, OB_CLUSTER_PARAMETER, "0", "[0,100]",
        "specifies the fixed percentage data to prewarm in compaction"
        "Range: [0, 100] in integer"
        "0 means not use this method, value > 0 means the corresponding percentage of data will be prewarmed",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

//Tablet config
DEF_PARAM(tablet_size, CAP_WITH_CHECKER, OB_CLUSTER_PARAMETER, "128M", common::ObConfigTabletSizeChecker,
                     "default tablet size, has to be a multiple of 2M",
                     ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(builtin_db_data_verify_cycle, INT, OB_CLUSTER_PARAMETER, "20", "[0, 360]",
        "check cycle of db data. Range: [0, 360] in integer. Unit: day. "
        "0: check nothing. "
        "1-360: check all data every specified days. "
        "The default value is 20. "
        "The real check cycle maybe longer than the specified value for insuring performance.",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(micro_block_merge_verify_level, INT, OB_CLUSTER_PARAMETER, "2", "[0,3]",
        "specify what kind of verification should be done when merging micro block. "
        "0 : no verification will be done "
        "1 : verify encoding algorithm, encoded micro block will be read to ensure data is correct "
        "2 : verify encoding and compression algorithm, besides encoding verification, compressed block will be decompressed to ensure data is correct"
        "3 : verify encoding, compression algorithm and lost write protect",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(_cache_wash_interval, TIME, OB_CLUSTER_PARAMETER, "200ms", "[1ms, 3s]",
        "specify interval of cache background wash",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(_io_read_batch_size, CAP, OB_CLUSTER_PARAMETER, "0K", "[0K,16M]", "Maximum batch size in one read io request. Range:[0K,16M]",
        ObParameterAttr(Section::SSTABLE, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_io_read_redundant_limit_percentage, INT, OB_CLUSTER_PARAMETER, "0", "[0, 99]",
        "Maximum percentage of redundant size in one read io request, redundant data means blocks in the middle of the batch that hit in cache or filtered by skipping index but must be read. Range:[0,99]",
        ObParameterAttr(Section::SSTABLE, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

// TODO bin.lb: to be remove
DEF_PARAM(dtl_buffer_size, CAP, OB_CLUSTER_PARAMETER, "64K", "[4K,2M]", "to be removed",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

// TODO bin.lb: to be remove
DEF_PARAM(px_task_size, CAP, OB_CLUSTER_PARAMETER, "2M", "[1K,)", "to be removed",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_max_elr_dependent_trx_count, INT, OB_CLUSTER_PARAMETER, "0", "[0,)", "max elr dependent transaction count",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(stack_size, CAP, OB_CLUSTER_PARAMETER, "256K", "[256K, 20M]",
        "the size of routine execution stack"
        "Range: [256K, 20M]",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::STATIC_EFFECTIVE));
DEF_PARAM(_px_max_pipeline_depth, INT, OB_CLUSTER_PARAMETER, "2", "[2,3]",
        "max parallel execution pipeline depth, "
        "range: [2,3]",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
//ssl
DEF_PARAM(ssl_client_authentication, BOOL, OB_CLUSTER_PARAMETER, "False",
         "enable server SSL support. Takes effect after ca/cert/key file is configured correctly. ",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(use_ipv6, BOOL, OB_CLUSTER_PARAMETER, "False",
         "Whether this server uses ipv6 address",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::STATIC_EFFECTIVE));

//DDL timeout
DEF_PARAM(_ob_ddl_timeout, TIME, OB_CLUSTER_PARAMETER, "1000s", "[1s,)",
         "the config parameter of ddl timeout"
         "Range: [1s, +∞)",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(ob_enable_batched_multi_statement, BOOL, OB_CLUSTER_PARAMETER, "False",
         "enable use of batched multi statement",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(_bloom_filter_ratio, INT, OB_CLUSTER_PARAMETER, "35", "[0, 100]",
        "The px bloom filter false-positive rate. Range: [0,100]",
         ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(schema_history_recycle_interval, TIME, OB_CLUSTER_PARAMETER, "10m", "[0s,]",
         "the time interval between the schedules of schema history recyle task. "
         "Range: [0s, +∞)",
         ObParameterAttr(Section::LOAD_BALANCE, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

// for bloom filter
DEF_PARAM(_bloom_filter_enabled, BOOL, OB_CLUSTER_PARAMETER, "True",
         "enable join bloom filter",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(ob_ssl_invited_common_names, STR, OB_CLUSTER_PARAMETER, "NONE",
        "when server use ssl, use it to control client identity with ssl subject common name. default NONE",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(_ob_ssl_invited_nodes, STR, OB_CLUSTER_PARAMETER, "NONE",
        "when rpc need use ssl, we will use it to store invited server ipv4 during grayscale change."
        "when it is finish, it can use ALL instead of all server ipv4",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_max_schema_slot_num, INT, OB_CLUSTER_PARAMETER, "128", "[2,256]",
        "the max schema slot number for multi-version schema memory management, "
        "Range: [2, 256] in integer",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_enable_add_fulltext_index_to_existing_table, BOOL, OB_CLUSTER_PARAMETER, "False",
         "enable create fulltext index after table is created",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(open_cursors, INT, OB_CLUSTER_PARAMETER, "50", "[0,65535]",
        "specifies the maximum number of open cursors a session can have at once."
        "can use this parameter to prevent a session from opening an excessive number of cursors."
        "Range: [0, 65535] in integer",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_enable_px_batch_rescan, BOOL, OB_CLUSTER_PARAMETER, "True",
         "enable px batch rescan for nlj or subplan filter",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_enable_spf_batch_rescan, BOOL, OB_CLUSTER_PARAMETER, "False",
         "enable das batch rescan for subplan filter",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_enable_das_keep_order, BOOL, OB_CLUSTER_PARAMETER, "True",
         "enable das keep order optimization",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_enable_index_merge, BOOL, OB_CLUSTER_PARAMETER, "False",
         "enable index merge optimization",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_enable_distributed_das_scan, BOOL, OB_CLUSTER_PARAMETER, "True",
         "enable distributed DAS scan",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_enable_das_batch_rescan_flag, INT, OB_CLUSTER_PARAMETER, "0",
        "enable das batch rescan for multiple scenarios.",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(_parallel_max_active_sessions, INT, OB_CLUSTER_PARAMETER, "0", "[0,]",
        "maximum active parallel sessions allowed on the server runtime. Range: [0,+∞)",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(enable_tcp_keepalive, BOOL, OB_CLUSTER_PARAMETER, "true",
         "enable TCP keepalive for the TCP connection of sql protocol. Take effect for "
         "new established connections.",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(tcp_keepidle, TIME, OB_CLUSTER_PARAMETER, "7200s", "[1s,]",
         "The time (in seconds) the connection needs to remain idle before TCP "
         "starts sending keepalive probe. Take effect for new established connections. "
         "Range: [1s, +∞]",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(tcp_keepintvl, TIME, OB_CLUSTER_PARAMETER, "6s", "[1s,]",
         "The time (in seconds) between individual keepalive probes. Take effect for new "
         "established connections. Range: [1s, +∞]",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(tcp_keepcnt, INT, OB_CLUSTER_PARAMETER, "10", "[1,]",
        "The maximum number of keepalive probes TCP should send before dropping "
        "the connection. Take effect for new established connections. Range: [1,+∞)",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_send_bloom_filter_size, INT, OB_CLUSTER_PARAMETER, "1024", "[1,]",
         "Set send bloom filter slice size"
         "Range: [1, +∞)",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_enable_px_ordered_coord, BOOL, OB_CLUSTER_PARAMETER, "false",
         "enable px task ordered coord",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(job_queue_processes, INT, OB_CLUSTER_PARAMETER, "1000", "[0,16384]",
        "specifies the maximum number of job slaves per instance that can be created "
        "for the execution of DBMS_JOB and DBMS_SCHEDULER jobs.",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_px_bloom_filter_group_size, STR_WITH_CHECKER, OB_CLUSTER_PARAMETER, "auto", common::ObConfigPxBFGroupSizeChecker,
         "specifies the px bloom filter each group size in sending to the other sqc"
         "Range: [1, +∞) or auto, the default value is auto",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(_enable_block_file_punch_hole, BOOL, OB_CLUSTER_PARAMETER, "False",
         "specifies whether to punch whole when free blocks in block_file",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_ob_enable_fast_parser, BOOL, OB_CLUSTER_PARAMETER, "True",
         "control if enable fast parser",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(_ob_obj_dep_maint_task_interval, TIME, OB_CLUSTER_PARAMETER, "1ms", "[0,10s]",
         "The execution interval of the task of maintaining the dependency of the object. "\
         "Range: [0, 10s]",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(_enable_newsort, BOOL, OB_CLUSTER_PARAMETER, "True",
         "control if enable encode sort",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(_px_object_sampling, INT, OB_CLUSTER_PARAMETER, "200", "[1, 100000]"
        "parallel query sampling for base objects (100000 = 100%)",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_follower_snapshot_read_retry_duration, TIME, OB_CLUSTER_PARAMETER, "0ms", "[0ms,]",
         "the waiting time after the first judgment failure of strong reading on follower"
         "Range: [0ms, +∞)",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_print_sample_ppm, INT, OB_CLUSTER_PARAMETER, "0", "[0, 1000000]",
        "In the full link diagnosis, control the frequency of printing traces to the log (unit is ppm, parts per million).",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_enable_parallel_das_dml, BOOL, OB_CLUSTER_PARAMETER, "False",
         "By default, the das service is allowed to use multiple threads to submit das tasks",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_ob_immediate_row_conflict_check, BOOL, OB_CLUSTER_PARAMETER, "False",
         "By default, OB's MySQL mode will check unique conflicts row by row after the update."
         "When the switch is turned off, "
         "it will only check whether the final state of a batch of data after the update satisfies the unique constraint.",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
// Add a config to enable use das if the sql statement has variable assignment
DEF_PARAM(_enable_var_assign_use_das, BOOL, OB_CLUSTER_PARAMETER, "False",
         "enable use das if the sql statement has variable assignment",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(sql_net_thread_count, INT, OB_CLUSTER_PARAMETER, "0", "[0,64]",
        "the number of global mysql I/O threads. Range: [0, 64] in integer, "
        "default value is 0, 0 stands for GCONF.net_thread_count",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_display_non_session_cursor, BOOL, OB_CLUSTER_PARAMETER, "True",
         "whether the content of non session cursors is displayed.",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(_ob_plan_cache_auto_flush_interval, TIME, OB_CLUSTER_PARAMETER, "0s", "[0s,)",
         "time interval for auto periodic flush plan cache. Range: [0s, +∞)",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_px_join_skew_handling, BOOL, OB_CLUSTER_PARAMETER, "True",
        "enables skew handling for parallel joins. The  default value is True.",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_px_join_skew_minfreq, INT, OB_CLUSTER_PARAMETER, "30", "[1,100]",
        "sets minimum frequency(%) for skewed value for parallel joins. Range: [1, 100] in integer",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_enable_transaction_internal_routing, BOOL, OB_CLUSTER_PARAMETER, "True",
         "enable SQLs of transaction routed to any servers in the cluster on demand",
         ObParameterAttr(Section::TRANS, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_enable_hgby_skew_detection, BOOL, OB_CLUSTER_PARAMETER, "True",
         "specifies whether hgby skew detection is enabled",
         ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_pdml_thread_cache_size, CAP, OB_CLUSTER_PARAMETER, "2M", "[1B,)",
        "The cache size of per pdml thread."
        "Range:[1B, )",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_enable_hgby_llc_ndv_adaptive, BOOL, OB_CLUSTER_PARAMETER, "True",
         "specifies whether llc ndv adptive is activated",
         ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(rpc_client_authentication_method, STR_WITH_CHECKER, OB_CLUSTER_PARAMETER, "NONE", common::ObCallClientAuthMethodChecker,
        "specifies rpc client authentication method. "
        "NONE: without authentication. "
        "SSL_NO_ENCRYPT: authentication by SSL handshake but not encrypt the communication channel. "
        "SSL_IO: authentication by SSL handshake and encrypt the communication channel",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE),
        "NONE, SSL_NO_ENCRYPT, SSL_IO");
DEF_PARAM(rpc_server_authentication_method, STR_WITH_CHECKER, OB_CLUSTER_PARAMETER, "ALL", common::ObCallServerAuthMethodChecker,
        "specifies rpc server authentication method. "
        "ALL: support all authentication methods. "
        "NONE: without authentication. "
        "SSL_NO_ENCRYPT: authentication by SSL handshake but not encrypt the communication channel. "
        "SSL_IO: authentication by SSL handshake and encrypt the communication channel",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE),
        "ALL, NONE, SSL_NO_ENCRYPT, SSL_IO");
DEF_PARAM(_enable_backtrace_function, BOOL, OB_CLUSTER_PARAMETER, "True",
         "Decide whether to let the backtrace function take effect",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_with_subquery, INT, OB_CLUSTER_PARAMETER, "0", "[0,2]",
        "WITH subquery transformation,0: optimizer,1: materialize,2: inline",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(_optimizer_group_by_placement, BOOL, OB_CLUSTER_PARAMETER, "True",
        "enable group by placement transform rule",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_complex_cbqt_table_num, INT, OB_CLUSTER_PARAMETER, "10", "[0,)",
        "cost-based transform will be disabled when table count in a single stmt exceeds threshold",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_enable_in_range_optimization, BOOL, OB_CLUSTER_PARAMETER, "True",
        "Enable extract query range optimization for in predicate",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_force_subquery_unnest, BOOL, OB_CLUSTER_PARAMETER, "FALSE",
        "aggressively unnest all subqueries that can be unnested, with correctness guaranteed.",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(range_optimizer_max_mem_size, CAP, OB_CLUSTER_PARAMETER, "128M", "[0M,)",
        "to limit the memory consumption for the query range optimizer. Range: [0M,+∞), 0 stands for unlimited",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_schema_memory_recycle_interval, TIME, OB_CLUSTER_PARAMETER, "15m", "[0s,)",
        "the time interval between the schedules of schema memory recycle task. "
        "0 means only turn off gc current allocator, "
        "and other schema memory recycle task's interval will be 15mins. "
        "Range [0s,)",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_optimizer_better_inlist_costing, BOOL, OB_CLUSTER_PARAMETER, "True",
        "enable improved costing of index access using in-list(s)",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_enable_skip_index, BOOL, OB_CLUSTER_PARAMETER, "True",
        "enable the skip index in storage engine",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_ob_ddl_temp_file_compress_func, STR_WITH_CHECKER, OB_CLUSTER_PARAMETER, "AUTO", common::ObConfigTempStoreFormatChecker,
        "specific compression in ObTempBlockStore."\
        "AUTO: use compression algorithm from table schema;"\
        "ZSTD: use ZSTD compression algorithm;"\
        "NONE: do not use compression.",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE),
        "AUTO, ZSTD, NONE");
DEF_PARAM(_enable_prefetch_limiting, BOOL, OB_CLUSTER_PARAMETER, "False",
         "enable limiting memory in prefetch for single query",
         ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_ob_sqlstat_enable, BOOL, OB_CLUSTER_PARAMETER, "True", "enable/disable sql stat",
         ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(_enable_inner_session_mgr, BOOL, OB_CLUSTER_PARAMETER, "True", "enable/disable inner session mgr",
         ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_enable_trace_tablet_leak, BOOL, OB_CLUSTER_PARAMETER, "False", 
        "enable t3m tablet leak checker. The default value is False",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::STATIC_EFFECTIVE));
DEF_PARAM(_inlist_rewrite_threshold, INT, OB_CLUSTER_PARAMETER, "1000", "[1, 2147483647]"
        "specifies transform how much const params in IN list to values table",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(vector_index_optimize_duty_time, STR_WITH_CHECKER, OB_CLUSTER_PARAMETER, "[24:00:00, 24:00:00]", common::ObVecIndexOptDutyTimeChecker,
    "A runtime range bounded by start time and end time for vector index background task, e.g., [23:00:00, 24:00:00]",
    ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
// AI / LLM
DEF_PARAM(model_request_timeout, TIME, OB_CLUSTER_PARAMETER, "60s", "[1s,)",
        "Used to control the HTTP timeout for accessing the  model. Especially, the default value is 60s.",
        ObParameterAttr(Section::AI, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(model_max_retries, INT, OB_CLUSTER_PARAMETER, "2", "[1,)",
    "Used to control the retry times after a failed model interaction. Especially, the default value is 2",
    ObParameterAttr(Section::AI, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));


DEF_PARAM(sql_protocol_min_tls_version, STR_WITH_CHECKER, OB_CLUSTER_PARAMETER, "none", common::ObConfigSQLTlsVersionChecker,
                     "SQL SSL control options, used to specify the minimum SSL/TLS version number. "
                     "values: none, TLSv1, TLSv1.1, TLSv1.2, TLSv1.3",
                     ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE),
                     "none, TLSv1, TLSv1.1, TLSv1.2, TLSv1.3");

DEF_PARAM(shared_log_retention, TIME, OB_CLUSTER_PARAMETER, "1d", "[0s,7d]",
        "Retention time of log files on shared storage",
        ObParameterAttr(Section::TRANS, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_enable_optimizer_qualify_filter, BOOL, OB_CLUSTER_PARAMETER, "True",
        "Enable extracting qualify filters for window function",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_enable_range_extraction_for_not_in, BOOL, OB_CLUSTER_PARAMETER, "True",
        "Enable extract query range for not in predicate",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(optimizer_index_cost_adj, INT, OB_CLUSTER_PARAMETER, "0", "[0,100]",
        "adjust costing of index scan",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(enable_rpc_authentication_bypass, BOOL, OB_CLUSTER_PARAMETER, "True",
        "specifies whether allow OMS service to connect "
        "cluster and provide service when rpc authentication is turned on.",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(_preserve_order_for_pagination, BOOL, OB_CLUSTER_PARAMETER, "False",
        "enable preserver order for limit",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_preserve_order_for_groupby, BOOL, OB_CLUSTER_PARAMETER, "False",
        "Control whether the query results are sorted according to the GROUP BY expression",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(max_partition_num, INT, OB_CLUSTER_PARAMETER, "8192", "[8192, 65536]",
        "set max partition num in mysql mode",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(json_document_max_depth, INT, OB_CLUSTER_PARAMETER, "100", "[100,1024]",
        "maximum nesting depth allowed in a JSON document",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_multimodel_memory_trace_level, INT, OB_CLUSTER_PARAMETER, "0", "[0,100)", 
        "Multi-mode memory tracking mechanism",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_query_record_size_limit, INT, OB_CLUSTER_PARAMETER, "65536", "[0, 67108864] in integer",
        "set sql_audit and plan stat query sql size. Range: [0,67108864] in integer in integer.",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_allow_skip_replay_redo_after_detete_tablet, BOOL, OB_CLUSTER_PARAMETER, "FALSE",
         "allow skip replay invalid redo log after tablet delete transaction is committed."
         "The default value is FALSE. Value: TRUE means we allow skip replaying this invalid redo log, False means we do not alow such behavior.",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

// Deprecated: strict OS parameter check has been removed. Keep for compatibility only.
DEF_PARAM(strict_check_os_params, BOOL, OB_CLUSTER_PARAMETER, "False",
         "Deprecated. The strict OS parameter check logic has been removed and this parameter has no effect. "
         "Default: False. Value: True/False are accepted for compatibility only.",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::STATIC_EFFECTIVE));
DEF_PARAM(_enable_tree_based_io_scheduler, BOOL, OB_CLUSTER_PARAMETER, "True",
         "A switch that allows enabling the tree-based IO scheduler."
         "Value: True: allowed; False: disabled",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(clog_io_isolation_mode, INT, OB_CLUSTER_PARAMETER, "1", "[1,2]",
         "Specifies the I/O isolation mode for Commit Log (clog). "
         "Values: "
         "1 - Non-isolation mode (disable I/O isolation), "
         "2 - Full isolation mode (enable I/O isolation). "
         "Example: 1=Off, 2=On",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_enable_memleak_light_backtrace, BOOL, OB_CLUSTER_PARAMETER, "True",
        "specifies whether allow memleak to get the backtrace of malloc by light_backtrace",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_enable_dbms_job_package, BOOL, OB_CLUSTER_PARAMETER, "True",
         "Control whether can use DBMS_JOB package.",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(lob_enable_block_cache_threshold, CAP, OB_CLUSTER_PARAMETER, "256K", "[0B, 512M]",
        "For outrow-stored LOBs, if the length is less than or equal to that threshold, "
        "they can be admitted into the block cache to speed up the next query.",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_ob_pl_compile_max_concurrency, INT, OB_CLUSTER_PARAMETER, "4", "[0,)",
        "The maximum number of threads that an observer node can compile PL concurrently.",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(enable_lock_priority, BOOL, OB_CLUSTER_PARAMETER, "False",
         "specifies whether to enable lock priority, which, when activated, gives certain DDL operations the highest table lock precedence.",
         ObParameterAttr(Section::TRANS, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_preset_runtime_bloom_filter_size, BOOL, OB_CLUSTER_PARAMETER, "False",
         "Whether build runtime bloom filter with row count estimated by optimizor."
         "Value:  True:turned on  False: turned off",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_enable_ddl_worker_isolation, BOOL, OB_CLUSTER_PARAMETER, "False",
         "a switch controling ddl thread isolation",
         ObParameterAttr(Section::ROOT_SERVICE, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

// Kept only for compatibility with tools that still set this retired parameter.
DEF_PARAM(ob_vector_memory_limit_percentage, INT, OB_CLUSTER_PARAMETER, "0", "[0, 100)",
        "Deprecated compatibility parameter. The configured value is accepted and persisted, "
        "but is ignored by vector memory sizing and memory control.",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_BOOL(vector_index_memory_saving_mode, OB_CLUSTER_PARAMETER, "True",
        "Specifies whether to enable the vector index memory saving mode. This can reduce the memory used by the partition table vector index rebuild.",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(_enable_drop_and_add_index, BOOL, OB_CLUSTER_PARAMETER, "False",
         "it specifies that whether we can drop and add index in single statement",
         ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(ob_encoding_granularity, INT, OB_CLUSTER_PARAMETER, "65536", "[8192, 1048576]",
        "Maximum rows for encoding in one micro block. Range:[8192,1048576]",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(_partition_wise_plan_enabled, BOOL, OB_CLUSTER_PARAMETER, "True",
         "enable/disable optimizer partition wise plan",
         ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(_enable_adaptive_auto_dop, BOOL, OB_CLUSTER_PARAMETER, "False",
         "Enable or disable adaptive auto dop feature.",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(query_memory_limit_percentage, INT, OB_CLUSTER_PARAMETER, "50", "[0,100]",
        "the percentage of server runtime memory that can be used by a single SQL. The default value is 50. Range: [0,100]",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(_ndv_runtime_bloom_filter_size, BOOL, OB_CLUSTER_PARAMETER, "True",
         "whether to use NDV to build a bloom filter in runtime filter."
         "Value:  True:turned on  False: turned off",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_max_px_workers_per_cpu, INT, OB_CLUSTER_PARAMETER, "1", "[1,30]",
        "The upper limit of PX workers that each CPU can carry. Range: [1,30]",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(default_table_organization, STR, OB_CLUSTER_PARAMETER, "INDEX",
        "The default_organization configuration option allows you to set the default"
        " table organization mode to either HEAP (unordered data storage) or INDEX (the data"
        " rows are held in an index defined on the primary key for the table) when creating new tables.",
        ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE),
        "INDEX, HEAP");

        


DEF_PARAM(_enable_topn_runtime_filter, BOOL, OB_CLUSTER_PARAMETER, "True",
         "Enable topn runtime filter.",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(_enable_async_load_sys_package, BOOL, OB_CLUSTER_PARAMETER, "True",
         "Controls the ability to enable/disable async load sys package",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
        
DEF_PARAM(_enable_px_task_rebalance, BOOL, OB_CLUSTER_PARAMETER, "False",
         "Enable or disable px task rebalance.",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_px_task_rebalance_trigger_time, TIME, OB_CLUSTER_PARAMETER, "10ms", "[1us, 1h]",
         "Control the trigger time of px task rebalance. Range: [1us, 1h]",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
// https://yuque.antfin.com/ob/product_functionality_review/vkv87bipgrf22tpi
DEF_PARAM(_ob_enable_truncate_partition_preserve_global_index, BOOL, OB_CLUSTER_PARAMETER, "False",
         "Specifies Whether to allow global indexes to be preserved when truncating/dropping the main table partition.",
         ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(_px_worker_share_plan_enabled, BOOL, OB_CLUSTER_PARAMETER, "True",
        "Enable parallel execution optimization by sharing plan and only serializing necessary expressions.",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(_enable_drop_column_instant, BOOL, OB_CLUSTER_PARAMETER, "True", "Whether to enable the capability for fast column deletion."
         "Value:  True: drop column instant;  False: drop column inplace",
         ObParameterAttr(Section::ROOT_SERVICE, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(_enable_routine_call_param_defend, BOOL, OB_CLUSTER_PARAMETER, "True",
         "Enable or disable routine call parameter defend.",
         ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_hnsw_max_scan_vectors, INT_WITH_CHECKER, OB_CLUSTER_PARAMETER, "20000", common::ObHNSWIterFilterScanNumChecker,
                    "The upper limit of hnsw iter-filter search nums. Range: [0,)",
                    ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
DEF_PARAM(_advance_checkpoint_interval, TIME, OB_CLUSTER_PARAMETER, "10m", "[0m,12h]",
         "The execution interval for the advance checkpoint task, 0m means disable this feature. "
         "Range: [0m, 12h]",
         ObParameterAttr(Section::RUNTIME, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

DEF_PARAM(server_create_time, INT, OB_CLUSTER_PARAMETER, "0", "[1,)",
        "the first time this server created, "
        "default: 0 (invalid timestamp), Range: [1,)",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::READONLY));

DEF_PARAM(log_restore_source, STR, OB_CLUSTER_PARAMETER, "",
        "standby log source in ip:rpc_port form",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));

// Persisted boot role and restart preparation state.
// format: "active_role:pending_role:transition_status:cutover_scn"
// example: "PRIMARY:INVALID:NORMAL:0"
DEF_PARAM(server_role_info, STR, OB_CLUSTER_PARAMETER, "",
        "server role state, format: active_role:pending_role:transition_status:cutover_scn",
        ObParameterAttr(Section::OBSERVER, Source::DEFAULT, EditLevel::DYNAMIC_EFFECTIVE));
