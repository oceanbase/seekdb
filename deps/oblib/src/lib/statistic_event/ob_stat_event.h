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

#ifdef STAT_EVENT_ADD_DEF
/**
 * STAT_EVENT_ADD_DEF(def, name, stat_class, stat_id, summary_in_session, can_visible, enable)
 * STAT_EVENT_SET_DEF(def, name, stat_class, stat_id, summary_in_session, can_visible, enable)
 * @param def name of this stat
 * @param name Name of this stat. Display on v$statname.
 * @param stat_class Every stats belongs to a class on deps/oblib/src/lib/statistic_event/ob_stat_class.h
 * @param stat_id Identifier of an stat ATTENTION: please add id placeholder on master.
 * @param summary_in_session Stat recorded for user session process mark this flag true.
 * @param can_visible Indicate whether this stat can be queried on gv$sysstat and gv$sesstat.
 * @param enable Indicate whether this stat is enabled. Marked it false it you merely need it as an placeholder.
 * NOTICE: do not reuse stat id or rename stat event!
*/

// NETWORK
STAT_EVENT_ADD_DEF(RPC_PACKET_IN, "rpc packet in", ObStatClassIds::NETWORK, 10000, true, true, true)
STAT_EVENT_ADD_DEF(RPC_PACKET_IN_BYTES, "rpc packet in bytes", ObStatClassIds::NETWORK, 10001, false, true, true)
STAT_EVENT_ADD_DEF(RPC_PACKET_OUT, "rpc packet out", ObStatClassIds::NETWORK, 10002, true, true, true)
STAT_EVENT_ADD_DEF(RPC_PACKET_OUT_BYTES, "rpc packet out bytes", ObStatClassIds::NETWORK, 10003, false, true, true)
STAT_EVENT_ADD_DEF(RPC_DELIVER_FAIL, "rpc deliver fail", ObStatClassIds::NETWORK, 10004, false, true, true)
STAT_EVENT_ADD_DEF(RPC_NET_DELAY, "rpc net delay", ObStatClassIds::NETWORK, 10005, false, true, true)
STAT_EVENT_ADD_DEF(RPC_NET_FRAME_DELAY, "rpc net frame delay", ObStatClassIds::NETWORK, 10006, false, true, true)
STAT_EVENT_ADD_DEF(MYSQL_PACKET_IN, "mysql packet in", ObStatClassIds::NETWORK, 10007, false, true, true)
STAT_EVENT_ADD_DEF(MYSQL_PACKET_IN_BYTES, "mysql packet in bytes", ObStatClassIds::NETWORK, 10008, false, true, true)
STAT_EVENT_ADD_DEF(MYSQL_PACKET_OUT, "mysql packet out", ObStatClassIds::NETWORK, 10009, false, true, true)
STAT_EVENT_ADD_DEF(MYSQL_PACKET_OUT_BYTES, "mysql packet out bytes", ObStatClassIds::NETWORK, 10010, false, true, true)
STAT_EVENT_ADD_DEF(MYSQL_DELIVER_FAIL, "mysql deliver fail", ObStatClassIds::NETWORK, 10011, false, true, true)
STAT_EVENT_ADD_DEF(RPC_COMPRESS_ORIGINAL_PACKET_CNT, "rpc compress original packet cnt", ObStatClassIds::NETWORK, 10012, false, true, true)
STAT_EVENT_ADD_DEF(RPC_COMPRESS_COMPRESSED_PACKET_CNT, "rpc compress compressed packet cnt", ObStatClassIds::NETWORK, 10013, false, true, true)
STAT_EVENT_ADD_DEF(RPC_COMPRESS_ORIGINAL_SIZE, "rpc compress original size", ObStatClassIds::NETWORK, 10014, false, true, true)
STAT_EVENT_ADD_DEF(RPC_COMPRESS_COMPRESSED_SIZE, "rpc compress compressed size", ObStatClassIds::NETWORK, 10015, false, true, true)

STAT_EVENT_ADD_DEF(RPC_STREAM_COMPRESS_ORIGINAL_PACKET_CNT, "rpc stream compress original packet cnt", ObStatClassIds::NETWORK, 10016, false, true, true)
STAT_EVENT_ADD_DEF(RPC_STREAM_COMPRESS_COMPRESSED_PACKET_CNT, "rpc stream compress compressed packet cnt", ObStatClassIds::NETWORK, 10017, false, true, true)
STAT_EVENT_ADD_DEF(RPC_STREAM_COMPRESS_ORIGINAL_SIZE, "rpc stream compress original size", ObStatClassIds::NETWORK, 10018, false, true, true)
STAT_EVENT_ADD_DEF(RPC_STREAM_COMPRESS_COMPRESSED_SIZE, "rpc stream compress compressed size", ObStatClassIds::NETWORK, 10019, false, true, true)

// QUEUE
STAT_EVENT_ADD_DEF(REQUEST_ENQUEUE_COUNT, "request enqueue count", ObStatClassIds::QUEUE, 20000, false, true, true)
STAT_EVENT_ADD_DEF(REQUEST_DEQUEUE_COUNT, "request dequeue count", ObStatClassIds::QUEUE, 20001, false, true, true)
STAT_EVENT_ADD_DEF(REQUEST_QUEUE_TIME, "request queue time", ObStatClassIds::QUEUE, 20002, false, true, true)

// TRANS
STAT_EVENT_ADD_DEF(TRANS_COMMIT_LOG_SYNC_TIME, "trans commit log sync time", ObStatClassIds::TRANS, 30000, false, true, true)
STAT_EVENT_ADD_DEF(TRANS_COMMIT_LOG_SYNC_COUNT, "trans commit log sync count", ObStatClassIds::TRANS, 30001, false, true, true)
STAT_EVENT_ADD_DEF(TRANS_COMMIT_LOG_SUBMIT_COUNT, "trans commit log submit count", ObStatClassIds::TRANS, 30002, false, true, true)
STAT_EVENT_ADD_DEF(TRANS_SYSTEM_TRANS_COUNT, "trans system trans count", ObStatClassIds::TRANS, 30003, false, true, true)
STAT_EVENT_ADD_DEF(TRANS_USER_TRANS_COUNT, "trans user trans count", ObStatClassIds::TRANS, 30004, false, true, true)
STAT_EVENT_ADD_DEF(TRANS_START_COUNT, "trans start count", ObStatClassIds::TRANS, 30005, false, true, true)
STAT_EVENT_ADD_DEF(TRANS_TOTAL_USED_TIME, "trans total used time", ObStatClassIds::TRANS, 30006, false, true, true)
STAT_EVENT_ADD_DEF(TRANS_COMMIT_COUNT, "trans commit count", ObStatClassIds::TRANS, 30007, false, true, true)
STAT_EVENT_ADD_DEF(TRANS_COMMIT_TIME, "trans commit time", ObStatClassIds::TRANS, 30008, false, true, true)
STAT_EVENT_ADD_DEF(TRANS_ROLLBACK_COUNT, "trans rollback count", ObStatClassIds::TRANS, 30009, false, true, true)
STAT_EVENT_ADD_DEF(TRANS_ROLLBACK_TIME, "trans rollback time", ObStatClassIds::TRANS, 30010, false, true, true)
STAT_EVENT_ADD_DEF(TRANS_TIMEOUT_COUNT, "trans timeout count", ObStatClassIds::TRANS, 30011, false, true, true)
STAT_EVENT_ADD_DEF(TRANS_LOCAL_COUNT, "trans local trans count", ObStatClassIds::TRANS, 30012, false, true, true)
STAT_EVENT_ADD_DEF(TRANS_DIST_COUNT, "trans distribute trans count", ObStatClassIds::TRANS, 30013, false, true, true)
STAT_EVENT_ADD_DEF(TRANS_READONLY_COUNT, "trans without write participant count", ObStatClassIds::TRANS, 30017, false, true, true)
STAT_EVENT_ADD_DEF(REDO_LOG_REPLAY_COUNT, "redo log replay count", ObStatClassIds::TRANS, 30018, false, true, true)
STAT_EVENT_ADD_DEF(REDO_LOG_REPLAY_TIME, "redo log replay time", ObStatClassIds::TRANS, 30019, false, true, true)
STAT_EVENT_ADD_DEF(PREPARE_LOG_REPLAY_COUNT, "prepare log replay count", ObStatClassIds::TRANS, 30020, false, true, true)
STAT_EVENT_ADD_DEF(PREPARE_LOG_REPLAY_TIME, "prepare log replay time", ObStatClassIds::TRANS, 30021, false, true, true)
STAT_EVENT_ADD_DEF(COMMIT_LOG_REPLAY_COUNT, "commit log replay count", ObStatClassIds::TRANS, 30022, false, true, true)
STAT_EVENT_ADD_DEF(COMMIT_LOG_REPLAY_TIME, "commit log replay time", ObStatClassIds::TRANS, 30023, false, true, true)
STAT_EVENT_ADD_DEF(ABORT_LOG_REPLAY_COUNT, "abort log replay count", ObStatClassIds::TRANS, 30024, false, true, true)
STAT_EVENT_ADD_DEF(ABORT_LOG_REPLAY_TIME, "abort log replay time", ObStatClassIds::TRANS, 30025, false, true, true)
STAT_EVENT_ADD_DEF(CLEAR_LOG_REPLAY_COUNT, "clear log replay count", ObStatClassIds::TRANS, 30026, false, true, true)
STAT_EVENT_ADD_DEF(CLEAR_LOG_REPLAY_TIME, "clear log replay time", ObStatClassIds::TRANS, 30027, false, true, true)
STAT_EVENT_ADD_DEF(GTS_ACQUIRE_TOTAL_TIME, "gts acquire total time", ObStatClassIds::TRANS, 30054, false, true, true)
STAT_EVENT_ADD_DEF(GTS_ACQUIRE_TOTAL_WAIT_COUNT, "gts acquire total wait count", ObStatClassIds::TRANS, 30056, false, true, true)
STAT_EVENT_ADD_DEF(GTS_WAIT_ELAPSE_TOTAL_TIME, "gts wait elapse total time", ObStatClassIds::TRANS, 30063, false, true, true)
STAT_EVENT_ADD_DEF(GTS_WAIT_ELAPSE_TOTAL_COUNT, "gts wait elapse total count", ObStatClassIds::TRANS, 30064, false, true, true)
STAT_EVENT_ADD_DEF(GTS_WAIT_ELAPSE_TOTAL_WAIT_COUNT, "gts wait elapse total wait count", ObStatClassIds::TRANS, 30065, false, true, true)
STAT_EVENT_ADD_DEF(GTS_TRY_ACQUIRE_TOTAL_COUNT, "gts try acquire total count", ObStatClassIds::TRANS, 30067, false, true, true)
STAT_EVENT_ADD_DEF(GTS_TRY_WAIT_ELAPSE_TOTAL_COUNT, "gts try wait elapse total count", ObStatClassIds::TRANS, 30068, false, true, true)
STAT_EVENT_ADD_DEF(TRANS_ELR_ENABLE_COUNT, "trans early lock release enable count", ObStatClassIds::TRANS, 30077, false, true, true)
STAT_EVENT_ADD_DEF(TRANS_ELR_UNABLE_COUNT, "trans early lock release unable count", ObStatClassIds::TRANS, 30078, false, true, true)
STAT_EVENT_ADD_DEF(READ_ELR_ROW_COUNT, "read elr row count", ObStatClassIds::TRANS, 30079, false, true, true)
STAT_EVENT_ADD_DEF(TRANS_LOCAL_TOTAL_USED_TIME, "local trans total used time", ObStatClassIds::TRANS, 30080, false, true, true)
STAT_EVENT_ADD_DEF(TRANS_DIST_TOTAL_USED_TIME, "distributed trans total used time", ObStatClassIds::TRANS, 30081, false, true, true)
STAT_EVENT_ADD_DEF(TX_DATA_HIT_MINI_CACHE_COUNT, "tx data hit mini cache count", ObStatClassIds::TRANS, 30082, false, true, true)
STAT_EVENT_ADD_DEF(TX_DATA_HIT_KV_CACHE_COUNT, "tx data hit kv cache count", ObStatClassIds::TRANS, 30083, false, true, true)
STAT_EVENT_ADD_DEF(TX_DATA_READ_TX_CTX_COUNT, "tx data read tx ctx count", ObStatClassIds::TRANS, 30084, false, true, true)
STAT_EVENT_ADD_DEF(TX_DATA_READ_TX_DATA_MEMTABLE_COUNT, "tx data read tx data memtable count", ObStatClassIds::TRANS, 30085, false, true, true)
STAT_EVENT_ADD_DEF(TX_DATA_READ_TX_DATA_SSTABLE_COUNT, "tx data read tx data sstable count", ObStatClassIds::TRANS, 30086, false, true, true)
// SQL
STAT_EVENT_ADD_DEF(SQL_SELECT_COUNT, "sql select count", ObStatClassIds::SQL, 40000, false, true, true)
STAT_EVENT_ADD_DEF(SQL_SELECT_TIME, "sql select time", ObStatClassIds::SQL, 40001, false, true, true)
STAT_EVENT_ADD_DEF(SQL_INSERT_COUNT, "sql insert count", ObStatClassIds::SQL, 40002, false, true, true)
STAT_EVENT_ADD_DEF(SQL_INSERT_TIME, "sql insert time", ObStatClassIds::SQL, 40003, false, true, true)
STAT_EVENT_ADD_DEF(SQL_REPLACE_COUNT, "sql replace count", ObStatClassIds::SQL, 40004, false, true, true)
STAT_EVENT_ADD_DEF(SQL_REPLACE_TIME, "sql replace time", ObStatClassIds::SQL, 40005, false, true, true)
STAT_EVENT_ADD_DEF(SQL_UPDATE_COUNT, "sql update count", ObStatClassIds::SQL, 40006, false, true, true)
STAT_EVENT_ADD_DEF(SQL_UPDATE_TIME, "sql update time", ObStatClassIds::SQL, 40007, false, true, true)
STAT_EVENT_ADD_DEF(SQL_DELETE_COUNT, "sql delete count", ObStatClassIds::SQL, 40008, false, true, true)
STAT_EVENT_ADD_DEF(SQL_DELETE_TIME, "sql delete time", ObStatClassIds::SQL, 40009, false, true, true)
STAT_EVENT_ADD_DEF(SQL_OTHER_COUNT, "sql other count", ObStatClassIds::SQL, 40018, false, true, true)
STAT_EVENT_ADD_DEF(SQL_OTHER_TIME, "sql other time", ObStatClassIds::SQL, 40019, false, true, true)
STAT_EVENT_ADD_DEF(SQL_PS_PREPARE_COUNT, "ps prepare count", ObStatClassIds::SQL, 40020, false, true, true)
STAT_EVENT_ADD_DEF(SQL_PS_PREPARE_TIME, "ps prepare time", ObStatClassIds::SQL, 40021, false, true, true)
STAT_EVENT_ADD_DEF(SQL_PS_EXECUTE_COUNT, "ps execute count", ObStatClassIds::SQL, 40022, false, true, true)
STAT_EVENT_ADD_DEF(SQL_PS_CLOSE_COUNT, "ps close count", ObStatClassIds::SQL, 40023, false, true, true)
STAT_EVENT_ADD_DEF(SQL_PS_CLOSE_TIME, "ps close time", ObStatClassIds::SQL, 40024, false, true, true)
STAT_EVENT_ADD_DEF(SQL_COMMIT_COUNT, "sql commit count", ObStatClassIds::SQL, 40025, false, true, true)
STAT_EVENT_ADD_DEF(SQL_COMMIT_TIME, "sql commit time", ObStatClassIds::SQL, 40026, false, true, true)
STAT_EVENT_ADD_DEF(SQL_ROLLBACK_COUNT, "sql rollback count", ObStatClassIds::SQL, 40027, false, true, true)
STAT_EVENT_ADD_DEF(SQL_ROLLBACK_TIME, "sql rollback time", ObStatClassIds::SQL, 40028, false, true, true)

STAT_EVENT_ADD_DEF(SQL_OPEN_CURSORS_CURRENT, "opened cursors current", ObStatClassIds::SQL, 40030, true, true, true)
STAT_EVENT_ADD_DEF(SQL_OPEN_CURSORS_CUMULATIVE, "opened cursors cumulative", ObStatClassIds::SQL, 40031, true, true, true)

STAT_EVENT_ADD_DEF(SQL_LOCAL_COUNT, "sql local count", ObStatClassIds::SQL, 40010, false, true, true)
STAT_EVENT_ADD_DEF(SQL_DISTRIBUTED_COUNT, "sql distributed count", ObStatClassIds::SQL, 40012, false, true, true)
STAT_EVENT_ADD_DEF(ACTIVE_SESSIONS, "active sessions", ObStatClassIds::SQL, 40013, false, true, true)
STAT_EVENT_ADD_DEF(SQL_SINGLE_QUERY_COUNT, "single query count", ObStatClassIds::SQL, 40014, false, true, true)
STAT_EVENT_ADD_DEF(SQL_MULTI_QUERY_COUNT, "multiple query count", ObStatClassIds::SQL, 40015, false, true, true)
STAT_EVENT_ADD_DEF(SQL_MULTI_ONE_QUERY_COUNT, "multiple query with one stmt count", ObStatClassIds::SQL, 40016, false, true, true)

STAT_EVENT_ADD_DEF(SQL_INNER_SELECT_COUNT, "sql inner select count", ObStatClassIds::SQL, 40100, false, true, true)
STAT_EVENT_ADD_DEF(SQL_INNER_SELECT_TIME, "sql inner select time", ObStatClassIds::SQL, 40101, false, true, true)
STAT_EVENT_ADD_DEF(SQL_INNER_INSERT_COUNT, "sql inner insert count", ObStatClassIds::SQL, 40102, false, true, true)
STAT_EVENT_ADD_DEF(SQL_INNER_INSERT_TIME, "sql inner insert time", ObStatClassIds::SQL, 40103, false, true, true)
STAT_EVENT_ADD_DEF(SQL_INNER_REPLACE_COUNT, "sql inner replace count", ObStatClassIds::SQL, 40104, false, true, true)
STAT_EVENT_ADD_DEF(SQL_INNER_REPLACE_TIME, "sql inner replace time", ObStatClassIds::SQL, 40105, false, true, true)
STAT_EVENT_ADD_DEF(SQL_INNER_UPDATE_COUNT, "sql inner update count", ObStatClassIds::SQL, 40106, false, true, true)
STAT_EVENT_ADD_DEF(SQL_INNER_UPDATE_TIME, "sql inner update time", ObStatClassIds::SQL, 40107, false, true, true)
STAT_EVENT_ADD_DEF(SQL_INNER_DELETE_COUNT, "sql inner delete count", ObStatClassIds::SQL, 40108, false, true, true)
STAT_EVENT_ADD_DEF(SQL_INNER_DELETE_TIME, "sql inner delete time", ObStatClassIds::SQL, 40109, false, true, true)
STAT_EVENT_ADD_DEF(SQL_INNER_OTHER_COUNT, "sql inner other count", ObStatClassIds::SQL, 40110, false, true, true)
STAT_EVENT_ADD_DEF(SQL_INNER_OTHER_TIME, "sql inner other time", ObStatClassIds::SQL, 40111, false, true, true)
STAT_EVENT_ADD_DEF(SQL_USER_LOGONS_CUMULATIVE, "user logons cumulative", ObStatClassIds::SQL, 40112, false, true, true)
STAT_EVENT_ADD_DEF(SQL_USER_LOGOUTS_CUMULATIVE, "user logouts cumulative", ObStatClassIds::SQL, 40113, false, true, true)
STAT_EVENT_ADD_DEF(SQL_USER_LOGONS_FAILED_CUMULATIVE, "user logons failed cumulative", ObStatClassIds::SQL, 40114, false, true, true)
STAT_EVENT_ADD_DEF(SQL_USER_LOGONS_COST_TIME_CUMULATIVE, "user logons time cumulative", ObStatClassIds::SQL, 40115, false, true, true)
STAT_EVENT_ADD_DEF(SQL_LOCAL_TIME, "sql local execute time", ObStatClassIds::SQL, 40116, false, true, true)
STAT_EVENT_ADD_DEF(SQL_DISTRIBUTED_TIME, "sql distributed execute time", ObStatClassIds::SQL, 40118, false, true, true)
STAT_EVENT_ADD_DEF(SQL_FAIL_COUNT, "sql fail count", ObStatClassIds::SQL, 40119, false, true, true)
STAT_EVENT_ADD_DEF(SQL_INNER_LOCAL_COUNT, "inner sql local count", ObStatClassIds::SQL, 40120, false, true, true)
STAT_EVENT_ADD_DEF(SQL_INNER_DISTRIBUTED_COUNT, "inner sql distributed count", ObStatClassIds::SQL, 40122, false, true, true)
STAT_EVENT_ADD_DEF(SQL_INSERT_DUPLICATE_COUNT, "try insert duplicate count", ObStatClassIds::SQL, 40126, true, true, true)

// CACHE
STAT_EVENT_ADD_DEF(ROW_CACHE_HIT, "row cache hit", ObStatClassIds::CACHE, 50000, true, true, true)
STAT_EVENT_ADD_DEF(ROW_CACHE_MISS, "row cache miss", ObStatClassIds::CACHE, 50001, true, true, true)
STAT_EVENT_ADD_DEF(BLOOM_FILTER_CACHE_HIT, "bloom filter cache hit", ObStatClassIds::CACHE, 50004, true, true, true)
STAT_EVENT_ADD_DEF(BLOOM_FILTER_CACHE_MISS, "bloom filter cache miss", ObStatClassIds::CACHE, 50005, true, true, true)
STAT_EVENT_ADD_DEF(BLOOM_FILTER_FILTS, "bloom filter filts", ObStatClassIds::CACHE, 50006, true, true, true)
STAT_EVENT_ADD_DEF(BLOOM_FILTER_PASSES, "bloom filter passes", ObStatClassIds::CACHE, 50007, true, true, true)
STAT_EVENT_ADD_DEF(BLOCK_CACHE_HIT, "block cache hit", ObStatClassIds::CACHE, 50008, true, true, true)
STAT_EVENT_ADD_DEF(BLOCK_CACHE_MISS, "block cache miss", ObStatClassIds::CACHE, 50009, true, true, true)
STAT_EVENT_ADD_DEF(LOCATION_CACHE_HIT, "location cache hit", ObStatClassIds::CACHE, 50010, false, true, true)
STAT_EVENT_ADD_DEF(LOCATION_CACHE_MISS, "location cache miss", ObStatClassIds::CACHE, 50011, false, true, true)
STAT_EVENT_ADD_DEF(LOCATION_CACHE_WAIT, "location cache wait", ObStatClassIds::CACHE, 50012, false, true, true)
STAT_EVENT_ADD_DEF(LOCATION_CACHE_PROXY_HIT, "location cache get hit from proxy virtual table", ObStatClassIds::CACHE, 50013, false, true, true)
STAT_EVENT_ADD_DEF(LOCATION_CACHE_PROXY_MISS, "location cache get miss from proxy virtual table", ObStatClassIds::CACHE, 50014, false, true, true)
STAT_EVENT_ADD_DEF(LOCATION_CACHE_CLEAR_LOCATION, "location cache nonblock renew", ObStatClassIds::CACHE, 50015, false, true, true)
STAT_EVENT_ADD_DEF(LOCATION_CACHE_CLEAR_LOCATION_IGNORED, "location cache nonblock renew ignored", ObStatClassIds::CACHE, 50016, false, true, true)
STAT_EVENT_ADD_DEF(LOCATION_CACHE_NONBLOCK_HIT, "location nonblock get hit", ObStatClassIds::CACHE, 50017, false, true, true)
STAT_EVENT_ADD_DEF(LOCATION_CACHE_NONBLOCK_MISS, "location nonblock get miss", ObStatClassIds::CACHE, 50018, false, true, true)
STAT_EVENT_ADD_DEF(LOCATION_CACHE_RPC_CHECK, "location cache rpc renew count", ObStatClassIds::CACHE, 50021, false, true, true)
STAT_EVENT_ADD_DEF(LOCATION_CACHE_RENEW, "location cache renew", ObStatClassIds::CACHE, 50022, false, true, true)
STAT_EVENT_ADD_DEF(LOCATION_CACHE_RENEW_IGNORED, "location cache renew ignored", ObStatClassIds::CACHE, 50023, false, true, true)
STAT_EVENT_ADD_DEF(KVCACHE_SYNC_WASH_TIME, "kvcache sync wash time", ObStatClassIds::CACHE, 50028, false, true, true)
STAT_EVENT_ADD_DEF(KVCACHE_SYNC_WASH_COUNT, "kvcache sync wash count", ObStatClassIds::CACHE, 50029, false, true, true)
STAT_EVENT_ADD_DEF(LOCATION_CACHE_RPC_RENEW_FAIL, "location cache rpc renew fail count", ObStatClassIds::CACHE, 50030, false, true, true)
STAT_EVENT_ADD_DEF(LOCATION_CACHE_SQL_RENEW, "location cache sql renew count", ObStatClassIds::CACHE, 50031, false, true, true)
STAT_EVENT_ADD_DEF(LOCATION_CACHE_IGNORE_RPC_RENEW, "location cache ignore rpc renew count", ObStatClassIds::CACHE, 50032, false, true, true)
STAT_EVENT_ADD_DEF(FUSE_ROW_CACHE_HIT, "fuse row cache hit", ObStatClassIds::CACHE, 50033, true, true, true)
STAT_EVENT_ADD_DEF(FUSE_ROW_CACHE_MISS, "fuse row cache miss", ObStatClassIds::CACHE, 50034, true, true, true)
STAT_EVENT_ADD_DEF(SCHEMA_CACHE_HIT, "schema cache hit", ObStatClassIds::CACHE, 50035, false, true, true)
STAT_EVENT_ADD_DEF(SCHEMA_CACHE_MISS, "schema cache miss", ObStatClassIds::CACHE, 50036, false, true, true)
// obsoleted part from INDEX_CLOG_CACHE_HIT to USER_TABLE_STAT_CACHE_MISS
STAT_EVENT_ADD_DEF(INDEX_CLOG_CACHE_HIT, "index clog cache hit", ObStatClassIds::CACHE, 50039, false, true, true)
STAT_EVENT_ADD_DEF(INDEX_CLOG_CACHE_MISS, "index clog cache miss", ObStatClassIds::CACHE, 50040, false, true, true)
STAT_EVENT_ADD_DEF(USER_TAB_COL_STAT_CACHE_HIT, "user tab col stat cache hit", ObStatClassIds::CACHE, 50041, false, true, true)
STAT_EVENT_ADD_DEF(USER_TAB_COL_STAT_CACHE_MISS, "user tab col stat cache miss", ObStatClassIds::CACHE, 50042, false, true, true)
STAT_EVENT_ADD_DEF(USER_TABLE_STAT_CACHE_HIT, "user table stat cache hit", ObStatClassIds::CACHE, 50043, false, true, true)
STAT_EVENT_ADD_DEF(USER_TABLE_STAT_CACHE_MISS, "user table stat cache miss", ObStatClassIds::CACHE, 50044, false, true, true)
// obsoleted part from CLOG_CACHE_HIT to USER_TABLE_STAT_CACHE_MISS
STAT_EVENT_ADD_DEF(OPT_TABLE_STAT_CACHE_HIT, "opt table stat cache hit", ObStatClassIds::CACHE, 50045, false, true, true)
STAT_EVENT_ADD_DEF(OPT_TABLE_STAT_CACHE_MISS, "opt table stat cache miss", ObStatClassIds::CACHE, 50046, false, true, true)
STAT_EVENT_ADD_DEF(OPT_COLUMN_STAT_CACHE_HIT, "opt column stat cache hit", ObStatClassIds::CACHE, 50047, false, true, true)
STAT_EVENT_ADD_DEF(OPT_COLUMN_STAT_CACHE_MISS, "opt column stat cache miss", ObStatClassIds::CACHE, 50048, false, true, true)
STAT_EVENT_ADD_DEF(TMP_PAGE_CACHE_HIT, "tmp page cache hit", ObStatClassIds::CACHE, 50049, false, true, true)
STAT_EVENT_ADD_DEF(TMP_PAGE_CACHE_MISS, "tmp page cache miss", ObStatClassIds::CACHE, 50050, false, true, true)
STAT_EVENT_ADD_DEF(TMP_BLOCK_CACHE_HIT, "tmp block cache hit", ObStatClassIds::CACHE, 50051, false, true, true)
STAT_EVENT_ADD_DEF(TMP_BLOCK_CACHE_MISS, "tmp block cache miss", ObStatClassIds::CACHE, 50052, false, true, true)
STAT_EVENT_ADD_DEF(SECONDARY_META_CACHE_HIT, "secondary meta cache hit", ObStatClassIds::CACHE, 50053, false, true, true)
STAT_EVENT_ADD_DEF(SECONDARY_META_CACHE_MISS, "secondary meta cache miss", ObStatClassIds::CACHE, 50054, false, true, true)
STAT_EVENT_ADD_DEF(OPT_DS_STAT_CACHE_HIT, "opt ds stat cache hit", ObStatClassIds::CACHE, 50055, false, true, true)
STAT_EVENT_ADD_DEF(OPT_DS_STAT_CACHE_MISS, "opt ds stat cache miss", ObStatClassIds::CACHE, 50056, false, true, true)
STAT_EVENT_ADD_DEF(STORAGE_META_CACHE_HIT, "storage meta cache hit", ObStatClassIds::CACHE, 50057, true, true, true)
STAT_EVENT_ADD_DEF(STORAGE_META_CACHE_MISS, "storage meta cache miss", ObStatClassIds::CACHE, 50058, true, true, true)
STAT_EVENT_ADD_DEF(TABLET_CACHE_HIT, "tablet cache hit", ObStatClassIds::CACHE, 50059, true, true, true)
STAT_EVENT_ADD_DEF(TABLET_CACHE_MISS, "tablet cache miss", ObStatClassIds::CACHE, 50060, true, true, true)

STAT_EVENT_ADD_DEF(SCHEMA_HISTORY_CACHE_HIT, "schema history cache hit", ObStatClassIds::CACHE, 50061, false, true, true)
STAT_EVENT_ADD_DEF(SCHEMA_HISTORY_CACHE_MISS, "schema history cache miss", ObStatClassIds::CACHE, 50062, false, true, true)
STAT_EVENT_ADD_DEF(OPT_SYSTEM_STAT_CACHE_HIT, "opt system stat cache hit", ObStatClassIds::CACHE, 50063, false, true, true)
STAT_EVENT_ADD_DEF(OPT_SYSTEM_STAT_CACHE_MISS, "opt system stat cache miss", ObStatClassIds::CACHE, 50064, false, true, true)

STAT_EVENT_ADD_DEF(DATA_BLOCK_CACHE_MISS, "data block cache miss", ObStatClassIds::CACHE, 50067, true, true, true)
STAT_EVENT_ADD_DEF(INDEX_BLOCK_CACHE_MISS, "index block cache miss", ObStatClassIds::CACHE, 50068, true, true, true)
STAT_EVENT_ADD_DEF(BACKUP_INDEX_CACHE_HIT, "backup index cache hit", ObStatClassIds::CACHE, 50071, true, true, true)
STAT_EVENT_ADD_DEF(BACKUP_INDEX_CACHE_MISS, "backup index cache miss", ObStatClassIds::CACHE, 50072, true, true, true)
STAT_EVENT_ADD_DEF(BACKUP_META_CACHE_HIT, "backup meta cache hit", ObStatClassIds::CACHE, 50073, true, true, true)
STAT_EVENT_ADD_DEF(BACKUP_META_CACHE_MISS, "backup meta cache miss", ObStatClassIds::CACHE, 50074, true, true, true)
STAT_EVENT_ADD_DEF(TRUNCATE_INFO_CACHE_HIT, "truncate info cache hit", ObStatClassIds::CACHE, 50075, true, true, true)
STAT_EVENT_ADD_DEF(TRUNCATE_INFO_CACHE_MISS, "truncate info cache miss", ObStatClassIds::CACHE, 50076, true, true, true)

// STORAGE
STAT_EVENT_ADD_DEF(IO_READ_COUNT, "io read count", ObStatClassIds::STORAGE, 60000, true, true, true)
STAT_EVENT_ADD_DEF(IO_READ_DELAY, "io read delay", ObStatClassIds::STORAGE, 60001, true, true, true)
STAT_EVENT_ADD_DEF(IO_READ_BYTES, "io read bytes", ObStatClassIds::STORAGE, 60002, true, true, true)
STAT_EVENT_ADD_DEF(IO_WRITE_COUNT, "io write count", ObStatClassIds::STORAGE, 60003, true, true, true)
STAT_EVENT_ADD_DEF(IO_WRITE_DELAY, "io write delay", ObStatClassIds::STORAGE, 60004, true, true, true)
STAT_EVENT_ADD_DEF(IO_WRITE_BYTES, "io write bytes", ObStatClassIds::STORAGE, 60005, true, true, true)
STAT_EVENT_ADD_DEF(MEMSTORE_SCAN_COUNT, "memstore scan count", ObStatClassIds::STORAGE, 60006, true, true, true)
STAT_EVENT_ADD_DEF(MEMSTORE_SCAN_SUCC_COUNT, "memstore scan succ count", ObStatClassIds::STORAGE, 60007, true, true, true)
STAT_EVENT_ADD_DEF(MEMSTORE_SCAN_FAIL_COUNT, "memstore scan fail count", ObStatClassIds::STORAGE, 60008, true, true, true)
STAT_EVENT_ADD_DEF(MEMSTORE_GET_COUNT, "memstore get count", ObStatClassIds::STORAGE, 60009, true, true, true)
STAT_EVENT_ADD_DEF(MEMSTORE_GET_SUCC_COUNT, "memstore get succ count", ObStatClassIds::STORAGE, 60010, true, true, true)
STAT_EVENT_ADD_DEF(MEMSTORE_GET_FAIL_COUNT, "memstore get fail count", ObStatClassIds::STORAGE, 60011, true, true, true)
STAT_EVENT_ADD_DEF(MEMSTORE_APPLY_COUNT, "memstore apply count", ObStatClassIds::STORAGE, 60012, true, true, true)
STAT_EVENT_ADD_DEF(MEMSTORE_APPLY_SUCC_COUNT, "memstore apply succ count", ObStatClassIds::STORAGE, 60013, true, true, true)
STAT_EVENT_ADD_DEF(MEMSTORE_APPLY_FAIL_COUNT, "memstore apply fail count", ObStatClassIds::STORAGE, 60014, true, true, true)
STAT_EVENT_ADD_DEF(MEMSTORE_GET_TIME, "memstore get time", ObStatClassIds::STORAGE, 60016, true, true, true)
STAT_EVENT_ADD_DEF(MEMSTORE_SCAN_TIME, "memstore scan time", ObStatClassIds::STORAGE, 60017, true, true, true)
STAT_EVENT_ADD_DEF(MEMSTORE_APPLY_TIME, "memstore apply time", ObStatClassIds::STORAGE, 60018, true, true, true)
STAT_EVENT_ADD_DEF(MEMSTORE_READ_LOCK_SUCC_COUNT, "memstore read lock succ count", ObStatClassIds::STORAGE, 60019, true, true, true)
STAT_EVENT_ADD_DEF(MEMSTORE_READ_LOCK_FAIL_COUNT, "memstore read lock fail count", ObStatClassIds::STORAGE, 60020, true, true, true)
STAT_EVENT_ADD_DEF(MEMSTORE_WRITE_LOCK_SUCC_COUNT, "memstore write lock succ count", ObStatClassIds::STORAGE, 60021, true, true, true)
STAT_EVENT_ADD_DEF(MEMSTORE_WRITE_LOCK_FAIL_COUNT, "memstore write lock fail count", ObStatClassIds::STORAGE, 60022, true, true, true)
STAT_EVENT_ADD_DEF(MEMSTORE_WAIT_WRITE_LOCK_TIME, "memstore wait write lock time", ObStatClassIds::STORAGE, 60023, true, true, true)
STAT_EVENT_ADD_DEF(MEMSTORE_WAIT_READ_LOCK_TIME, "memstore wait read lock time", ObStatClassIds::STORAGE, 60024, true, true, true)
STAT_EVENT_ADD_DEF(IO_READ_MICRO_INDEX_COUNT, "io read micro index count", ObStatClassIds::STORAGE, 60025, false, true, true)
STAT_EVENT_ADD_DEF(IO_READ_MICRO_INDEX_BYTES, "io read micro index bytes", ObStatClassIds::STORAGE, 60026, false, true, true)
STAT_EVENT_ADD_DEF(IO_READ_PREFETCH_MICRO_COUNT, "io prefetch micro block count", ObStatClassIds::STORAGE, 60027, false, true, true)
STAT_EVENT_ADD_DEF(IO_READ_PREFETCH_MICRO_BYTES, "io prefetch micro block bytes", ObStatClassIds::STORAGE, 60028, false, true, true)
STAT_EVENT_ADD_DEF(IO_READ_UNCOMP_MICRO_COUNT, "io read uncompress micro block count", ObStatClassIds::STORAGE, 60029, false, true, true)
STAT_EVENT_ADD_DEF(IO_READ_UNCOMP_MICRO_BYTES, "io read uncompress micro block bytes", ObStatClassIds::STORAGE, 60030, false, true, true)
STAT_EVENT_ADD_DEF(STORAGE_READ_ROW_COUNT, "storage read row count", ObStatClassIds::STORAGE, 60031, false, true, true)
STAT_EVENT_ADD_DEF(STORAGE_DELETE_ROW_COUNT, "storage delete row count", ObStatClassIds::STORAGE, 60032, false, true, true)
STAT_EVENT_ADD_DEF(STORAGE_INSERT_ROW_COUNT, "storage insert row count", ObStatClassIds::STORAGE, 60033, false, true, true)
STAT_EVENT_ADD_DEF(STORAGE_UPDATE_ROW_COUNT, "storage update row count", ObStatClassIds::STORAGE, 60034, false, true, true)
STAT_EVENT_ADD_DEF(MEMSTORE_ROW_PURGE_COUNT, "memstore row purge count", ObStatClassIds::STORAGE, 60037, false, true, true)
STAT_EVENT_ADD_DEF(MEMSTORE_ROW_COMPACTION_COUNT, "memstore row compaction count", ObStatClassIds::STORAGE, 60038, false, true, true)
STAT_EVENT_ADD_DEF(IO_READ_QUEUE_DELAY, "io read queue delay", ObStatClassIds::STORAGE, 60039, false, true, true)
STAT_EVENT_ADD_DEF(IO_WRITE_QUEUE_DELAY, "io write queue delay", ObStatClassIds::STORAGE, 60040, false, true, true)
STAT_EVENT_ADD_DEF(IO_READ_CB_QUEUE_DELAY, "io read callback queuing delay", ObStatClassIds::STORAGE, 60041, false, true, true)
STAT_EVENT_ADD_DEF(IO_READ_CB_PROCESS_DELAY, "io read callback process delay", ObStatClassIds::STORAGE, 60042, false, true, true)
STAT_EVENT_ADD_DEF(BANDWIDTH_IN_THROTTLE, "bandwidth in throttle size", ObStatClassIds::STORAGE, 60051, false, true, true)
STAT_EVENT_ADD_DEF(BANDWIDTH_OUT_THROTTLE, "bandwidth out throttle size", ObStatClassIds::STORAGE, 60052, false, true, true)
STAT_EVENT_ADD_DEF(MEMSTORE_READ_ROW_COUNT, "memstore read row count", ObStatClassIds::STORAGE, 60056, true, true, true)
STAT_EVENT_ADD_DEF(SSSTORE_READ_ROW_COUNT, "ssstore read row count", ObStatClassIds::STORAGE, 60057, true, true, true)
STAT_EVENT_ADD_DEF(MEMSTORE_WRITE_BYTES, "memstore write bytes", ObStatClassIds::STORAGE, 60058, true, true, true)

STAT_EVENT_ADD_DEF(MEMSTORE_WRITE_LOCK_WAKENUP_COUNT, "memstore write lock wakenup count in lock_wait_mgr", ObStatClassIds::STORAGE, 60068, false, true, true)


STAT_EVENT_ADD_DEF(EXIST_ROW_EFFECT_READ, "exist row effect read", ObStatClassIds::STORAGE, 60075, false, true, true)
STAT_EVENT_ADD_DEF(EXIST_ROW_EMPTY_READ, "exist row empty read", ObStatClassIds::STORAGE, 60076, false, true, true)
STAT_EVENT_ADD_DEF(GET_ROW_EFFECT_READ, "get row effect read", ObStatClassIds::STORAGE, 60077, false, true, true)
STAT_EVENT_ADD_DEF(GET_ROW_EMPTY_READ, "get row empty read", ObStatClassIds::STORAGE, 60078, false, true, true)
STAT_EVENT_ADD_DEF(SCAN_ROW_EFFECT_READ, "scan row effect read", ObStatClassIds::STORAGE, 60079, false, true, true)
STAT_EVENT_ADD_DEF(SCAN_ROW_EMPTY_READ, "scan row empty read", ObStatClassIds::STORAGE, 60080, false, true, true)
STAT_EVENT_ADD_DEF(BANDWIDTH_IN_SLEEP_US, "bandwidth in sleep us", ObStatClassIds::STORAGE, 60081, false, true, true)
STAT_EVENT_ADD_DEF(BANDWIDTH_OUT_SLEEP_US, "bandwidth out sleep us", ObStatClassIds::STORAGE, 60082, false, true, true)

STAT_EVENT_ADD_DEF(MEMSTORE_WRITE_LOCK_WAIT_TIMEOUT_COUNT, "memstore write lock wait timeout count", ObStatClassIds::STORAGE, 60083, false, true, true)

STAT_EVENT_ADD_DEF(DATA_BLOCK_READ_CNT, "accessed data micro block count", ObStatClassIds::STORAGE, 60084, true, true, true)
STAT_EVENT_ADD_DEF(DATA_BLOCK_CACHE_HIT, "data micro block cache hit", ObStatClassIds::STORAGE, 60085, true, true, true)
STAT_EVENT_ADD_DEF(INDEX_BLOCK_READ_CNT, "accessed index micro block count", ObStatClassIds::STORAGE, 60086, true, true, true)
STAT_EVENT_ADD_DEF(INDEX_BLOCK_CACHE_HIT, "index micro block cache hit", ObStatClassIds::STORAGE, 60087, true, true, true)
STAT_EVENT_ADD_DEF(BLOCKSCAN_BLOCK_CNT, "blockscaned data micro block count", ObStatClassIds::STORAGE, 60088, true, true, true)
STAT_EVENT_ADD_DEF(BLOCKSCAN_ROW_CNT, "blockscaned row count", ObStatClassIds::STORAGE, 60089, true, true, true)
STAT_EVENT_ADD_DEF(PUSHDOWN_STORAGE_FILTER_ROW_CNT, "storage filtered row count", ObStatClassIds::STORAGE, 60090, true, true, true)
STAT_EVENT_ADD_DEF(MINOR_SSSTORE_READ_ROW_COUNT, "minor ssstore read row count", ObStatClassIds::STORAGE, 60091, true, true, true)
STAT_EVENT_ADD_DEF(MAJOR_SSSTORE_READ_ROW_COUNT, "major ssstore read row count", ObStatClassIds::STORAGE, 60092, true, true, true)
STAT_EVENT_ADD_DEF(STORAGE_WRITING_THROTTLE_TIME, "storage waiting throttle time", ObStatClassIds::STORAGE, 60093, true, true, true)
STAT_EVENT_ADD_DEF(IO_READ_DEVICE_TIME, "io read execute time", ObStatClassIds::STORAGE, 60094, true, true, true)
STAT_EVENT_ADD_DEF(IO_WRITE_DEVICE_TIME, "io write execute time", ObStatClassIds::STORAGE, 60095, true, true, true)

// backup & restore
STAT_EVENT_ADD_DEF(BACKUP_IO_READ_COUNT, "backup io read count", ObStatClassIds::STORAGE, 69000, true, true, true)
STAT_EVENT_ADD_DEF(BACKUP_IO_READ_BYTES, "backup io read bytes", ObStatClassIds::STORAGE, 69001, true, true, true)
STAT_EVENT_ADD_DEF(BACKUP_IO_WRITE_COUNT, "backup io write count", ObStatClassIds::STORAGE, 69002, true, true, true)
STAT_EVENT_ADD_DEF(BACKUP_IO_WRITE_BYTES, "backup io write bytes", ObStatClassIds::STORAGE, 69003, true, true, true)
STAT_EVENT_ADD_DEF(BACKUP_DELETE_COUNT, "backup delete count", ObStatClassIds::STORAGE, 69010, true, true, true)

STAT_EVENT_ADD_DEF(BACKUP_IO_READ_DELAY, "backup io read delay", ObStatClassIds::STORAGE, 69012, true, true, true)
STAT_EVENT_ADD_DEF(BACKUP_IO_WRITE_DELAY, "backup io write delay", ObStatClassIds::STORAGE, 69013, true, true, true)
STAT_EVENT_ADD_DEF(BACKUP_DELETE_DELAY, "backup delete delay", ObStatClassIds::STORAGE, 69017, true, true, true)

STAT_EVENT_ADD_DEF(BACKUP_IO_LS_COUNT, "backup io list count", ObStatClassIds::STORAGE, 69019, true, true, true)
STAT_EVENT_ADD_DEF(BACKUP_IO_READ_FAIL_COUNT, "backup io read failed count", ObStatClassIds::STORAGE, 69020, true, true, true)
STAT_EVENT_ADD_DEF(BACKUP_IO_WRITE_FAIL_COUNT, "backup io write failed count", ObStatClassIds::STORAGE, 69021, true, true, true)
STAT_EVENT_ADD_DEF(BACKUP_IO_DEL_FAIL_COUNT, "backup io delete failed count", ObStatClassIds::STORAGE, 69022, true, true, true)
STAT_EVENT_ADD_DEF(BACKUP_IO_LS_FAIL_COUNT, "backup io list failed count", ObStatClassIds::STORAGE, 69023, true, true, true)

STAT_EVENT_ADD_DEF(BACKUP_TAGGING_COUNT, "backup io tagging count", ObStatClassIds::STORAGE, 69024, true, true, true)
STAT_EVENT_ADD_DEF(BACKUP_TAGGING_FAIL_COUNT, "backup io tagging failed count", ObStatClassIds::STORAGE, 69025, true, true, true)


// DEBUG
STAT_EVENT_ADD_DEF(REFRESH_SCHEMA_COUNT, "refresh schema count", ObStatClassIds::DEBUG, 70000, false, true, true)
STAT_EVENT_ADD_DEF(REFRESH_SCHEMA_TIME, "refresh schema time", ObStatClassIds::DEBUG, 70001, false, true, true)
STAT_EVENT_ADD_DEF(INNER_SQL_CONNECTION_EXECUTE_COUNT, "inner sql connection execute count", ObStatClassIds::DEBUG, 70002, false, true, true)
STAT_EVENT_ADD_DEF(INNER_SQL_CONNECTION_EXECUTE_TIME, "inner sql connection execute time", ObStatClassIds::DEBUG, 70003, false, true, true)
STAT_EVENT_ADD_DEF(LS_ALL_TABLE_OPERATOR_GET_COUNT, "log stream table operator get count", ObStatClassIds::DEBUG, 70006, false, true, true)
STAT_EVENT_ADD_DEF(LS_ALL_TABLE_OPERATOR_GET_TIME, "log stream table operator get time", ObStatClassIds::DEBUG, 70007, false, true, true)

//CLOG 80001 ~ 90000
STAT_EVENT_ADD_DEF(PALF_WRITE_IO_COUNT, "palf write io count to disk",  ObStatClassIds::CLOG, 80001, true, true, true)
STAT_EVENT_ADD_DEF(PALF_WRITE_SIZE, "palf write size to disk",  ObStatClassIds::CLOG, 80002, true, true, true)
STAT_EVENT_ADD_DEF(PALF_WRITE_TIME, "palf write total time to disk", ObStatClassIds::CLOG, 80003, true, true, true)
STAT_EVENT_ADD_DEF(PALF_READ_COUNT_FROM_HOT_CACHE, "palf read count from hot cache", ObStatClassIds::CLOG, 80004, true, true, true) // FARM COMPAT WHITELIST
STAT_EVENT_ADD_DEF(PALF_READ_SIZE_FROM_HOT_CACHE, "palf read size from hot cache", ObStatClassIds::CLOG, 80005, true, true, true) // FARM COMPAT WHITELIST
STAT_EVENT_ADD_DEF(PALF_READ_TIME_FROM_HOT_CACHE, "palf read total time from hot cache", ObStatClassIds::CLOG, 80006, true, true, true) // FARM COMPAT WHITELIST
STAT_EVENT_ADD_DEF(PALF_READ_IO_COUNT_FROM_DISK, "palf read io count from disk", ObStatClassIds::CLOG, 80007, true, true, true)
STAT_EVENT_ADD_DEF(PALF_READ_SIZE_FROM_DISK, "palf read size from disk", ObStatClassIds::CLOG, 80008, true, true, true)
STAT_EVENT_ADD_DEF(PALF_READ_TIME_FROM_DISK, "palf read total time from disk", ObStatClassIds::CLOG, 80009, true, true, true)
STAT_EVENT_ADD_DEF(PALF_HANDLE_RPC_REQUEST_COUNT, "palf handle rpc request count", ObStatClassIds::CLOG, 80010, true, true, true)
STAT_EVENT_ADD_DEF(RESTORE_READ_LOG_SIZE, "restore read log size", ObStatClassIds::CLOG, 80013, true, true, true)
STAT_EVENT_ADD_DEF(RESTORE_WRITE_LOG_SIZE, "restore write log size", ObStatClassIds::CLOG, 80014, true, true, true)
STAT_EVENT_ADD_DEF(CLOG_TRANS_LOG_TOTAL_SIZE, "clog trans log total size", ObStatClassIds::CLOG, 80057, false, true, true)

// 81001-81003 were used by removed external log-fetch statistics. Do not reuse.

// CLOG.REPLAY


// CLOG.PROXY

//OBSERVER

// rootservice
STAT_EVENT_ADD_DEF(RS_RPC_SUCC_COUNT, "success rpc process", ObStatClassIds::RS, 110015, false, true, true)
STAT_EVENT_ADD_DEF(RS_RPC_FAIL_COUNT, "failed rpc process", ObStatClassIds::RS, 110016, false, true, true)
STAT_EVENT_ADD_DEF(RS_BALANCER_SUCC_COUNT, "balancer succ execute count", ObStatClassIds::RS, 110017, false, true, true)
STAT_EVENT_ADD_DEF(RS_BALANCER_FAIL_COUNT, "balancer failed execute count", ObStatClassIds::RS, 110018, false, true, true)

// sys_time_model related (20xxxx)
STAT_EVENT_ADD_DEF(SYS_TIME_MODEL_DB_TIME, "DB time", ObStatClassIds::SYS, 200001, false, true, true)
STAT_EVENT_ADD_DEF(SYS_TIME_MODEL_DB_CPU, "DB CPU", ObStatClassIds::SYS, 200002, false, true, true)
STAT_EVENT_ADD_DEF(SYS_TIME_MODEL_BKGD_TIME, "background elapsed time", ObStatClassIds::SYS, 200005, false, true, true)
STAT_EVENT_ADD_DEF(SYS_TIME_MODEL_BKGD_CPU, "background cpu time", ObStatClassIds::SYS, 200006, false, true, true)
STAT_EVENT_ADD_DEF(SYS_TIME_MODEL_NON_IDLE_WAIT_TIME, "non idle wait time", ObStatClassIds::SYS, 200010, true, true, true)
STAT_EVENT_ADD_DEF(SYS_TIME_MODEL_IDLE_WAIT_TIME, "idle wait time", ObStatClassIds::SYS, 200011, true, true, true)
STAT_EVENT_ADD_DEF(SYS_TIME_MODEL_BKGD_DB_TIME, "background database time", ObStatClassIds::SYS, 200012, false, true, true)
STAT_EVENT_ADD_DEF(SYS_TIME_MODEL_BKGD_NON_IDLE_WAIT_TIME, "background database non-idle wait time", ObStatClassIds::SYS, 200013, false, true, true)
STAT_EVENT_ADD_DEF(SYS_TIME_MODEL_BKGD_IDLE_WAIT_TIME, "background database idle wait time", ObStatClassIds::SYS, 200014, false, true, true)
STAT_EVENT_ADD_DEF(DIAGNOSTIC_INFO_ALLOC_COUNT, "diagnostic info object allocated count", ObStatClassIds::SYS, 200015, false, true, true)
STAT_EVENT_ADD_DEF(DIAGNOSTIC_INFO_ALLOC_FAIL_COUNT, "diagnostic info object allocate failure count", ObStatClassIds::SYS, 200016, false, true, true)
STAT_EVENT_ADD_DEF(DIAGNOSTIC_INFO_RETURN_COUNT, "diagnostic info object returned count", ObStatClassIds::SYS, 200017, false, true, true)
STAT_EVENT_ADD_DEF(DIAGNOSTIC_INFO_RETURN_FAIL_COUNT, "diagnostic info object return failure count", ObStatClassIds::SYS, 200018, false, true, true)

// sqlstat relat (2200xx)
STAT_EVENT_ADD_DEF(CCWAIT_TIME, "concurrency wait total time", ObStatClassIds::SYS, 220001, true, true, true)
STAT_EVENT_ADD_DEF(USER_IO_WAIT_TIME, "user io wait total time", ObStatClassIds::SYS, 220002, true, true, true)
STAT_EVENT_ADD_DEF(APWAIT_TIME, "application wait total time", ObStatClassIds::SYS, 220003, true, true, true)
STAT_EVENT_ADD_DEF(SCHEDULE_WAIT_TIME, "schedule wait total time", ObStatClassIds::SYS, 220004, true, true, true)
STAT_EVENT_ADD_DEF(NETWORK_WAIT_TIME, "network wait total time", ObStatClassIds::SYS, 220005, true, true, true)

//end
STAT_EVENT_ADD_DEF(STAT_EVENT_ADD_END, "event add end", ObStatClassIds::DEBUG, 1, false, false, true)

#endif

#ifdef STAT_EVENT_SET_DEF
// QUEUE

// TRANS

// SQL

// CACHE
STAT_EVENT_SET_DEF(OPT_TAB_STAT_CACHE_SIZE, "table stat cache size", ObStatClassIds::CACHE, 120002, false, true, true)
STAT_EVENT_SET_DEF(OPT_TAB_COL_STAT_CACHE_SIZE, "table col stat cache size", ObStatClassIds::CACHE, 120003, false, true, true)
STAT_EVENT_SET_DEF(INDEX_BLOCK_CACHE_SIZE, "index block cache size", ObStatClassIds::CACHE, 120004, false, true, true)
STAT_EVENT_SET_DEF(USER_BLOCK_CACHE_SIZE, "user block cache size", ObStatClassIds::CACHE, 120006, false, true, true)
STAT_EVENT_SET_DEF(USER_ROW_CACHE_SIZE, "user row cache size", ObStatClassIds::CACHE, 120008, false, true, true)
STAT_EVENT_SET_DEF(BLOOM_FILTER_CACHE_SIZE, "bloom filter cache size", ObStatClassIds::CACHE, 120009, false, true, true)

// STORAGE
STAT_EVENT_SET_DEF(ACTIVE_MEMSTORE_USED, "active memstore used", ObStatClassIds::STORAGE, 130000, false, true, true)
STAT_EVENT_SET_DEF(TOTAL_MEMSTORE_USED, "total memstore used", ObStatClassIds::STORAGE, 130001, false, true, true)
STAT_EVENT_SET_DEF(MAJOR_FREEZE_TRIGGER, "major freeze trigger", ObStatClassIds::STORAGE, 130002, false, true, true)
STAT_EVENT_SET_DEF(MEMSTORE_LIMIT, "memstore limit", ObStatClassIds::STORAGE, 130004, false, true, true)

// RESOURCE
STAT_EVENT_SET_DEF(MIN_MEMORY_SIZE, "min memory size", ObStatClassIds::RESOURCE, 140001, false, true, true)
STAT_EVENT_SET_DEF(MAX_MEMORY_SIZE, "max memory size", ObStatClassIds::RESOURCE, 140002, false, true, true)
STAT_EVENT_SET_DEF(MEMORY_USAGE, "memory usage", ObStatClassIds::RESOURCE, 140003, false, true, true)
STAT_EVENT_SET_DEF(MIN_CPUS, "min cpus", ObStatClassIds::RESOURCE, 140004, false, true, true)
STAT_EVENT_SET_DEF(MAX_CPUS, "max cpus", ObStatClassIds::RESOURCE, 140005, false, true, true)
STAT_EVENT_SET_DEF(CPU_USAGE, "cpu usage", ObStatClassIds::RESOURCE, 140006, false, true, true)
STAT_EVENT_SET_DEF(DISK_USAGE, "disk usage", ObStatClassIds::RESOURCE, 140007, false, true, true)
STAT_EVENT_SET_DEF(MEMORY_USED_SIZE, "observer memory used size", ObStatClassIds::RESOURCE, 140008, false, true, true)
STAT_EVENT_SET_DEF(MEMORY_FREE_SIZE, "observer memory free size", ObStatClassIds::RESOURCE, 140009, false, true, true)
STAT_EVENT_SET_DEF(IS_MINI_MODE, "is mini mode", ObStatClassIds::RESOURCE, 140010, false, true, true)
STAT_EVENT_SET_DEF(MEMORY_HOLD_SIZE, "observer memory hold size", ObStatClassIds::RESOURCE, 140011, false, true, true)
STAT_EVENT_SET_DEF(WORKER_TIME, "worker time", ObStatClassIds::RESOURCE, 140012, false, true, true)
STAT_EVENT_SET_DEF(CPU_TIME, "cpu time", ObStatClassIds::RESOURCE, 140013, false, true, true)
STAT_EVENT_SET_DEF(MEMORY_LIMIT, "effective observer memory limit", ObStatClassIds::RESOURCE, 140014, false, true, true)
STAT_EVENT_SET_DEF(SYSTEM_MEMORY, "effective system memory", ObStatClassIds::RESOURCE, 140015, false, true, true)
STAT_EVENT_SET_DEF(MAX_SESSION_NUM, "max session num", ObStatClassIds::RESOURCE, 140017, false, true, true)
STAT_EVENT_SET_DEF(KV_CACHE_HOLD, "kvcache hold", ObStatClassIds::RESOURCE, 140018, false, true, true)
STAT_EVENT_SET_DEF(UNMANAGED_MEMORY_SIZE, "unmanaged memory size", ObStatClassIds::RESOURCE, 140019, false, true, true) // FARM COMPAT WHITELIST
//CLOG

// DEBUG
STAT_EVENT_SET_DEF(OBLOGGER_WRITE_SIZE, "oblogger log bytes", ObStatClassIds::DEBUG, 160001, false, true, true)
STAT_EVENT_SET_DEF(OBLOGGER_TOTAL_WRITE_COUNT, "oblogger total log count", ObStatClassIds::DEBUG, 160004, false, true, true)
STAT_EVENT_SET_DEF(ASYNC_ERROR_LOG_DROPPED_COUNT, "async error log dropped count", ObStatClassIds::DEBUG, 160019, false, true, true)
STAT_EVENT_SET_DEF(ASYNC_WARN_LOG_DROPPED_COUNT, "async warn log dropped count", ObStatClassIds::DEBUG, 160020, false, true, true)
STAT_EVENT_SET_DEF(ASYNC_INFO_LOG_DROPPED_COUNT, "async info log dropped count", ObStatClassIds::DEBUG, 160021, false, true, true)
STAT_EVENT_SET_DEF(ASYNC_TRACE_LOG_DROPPED_COUNT, "async trace log dropped count", ObStatClassIds::DEBUG, 160022, false, true, true)
STAT_EVENT_SET_DEF(ASYNC_DEBUG_LOG_DROPPED_COUNT, "async debug log dropped count", ObStatClassIds::DEBUG, 160023, false, true, true)
STAT_EVENT_SET_DEF(ASYNC_LOG_FLUSH_SPEED, "async log flush speed", ObStatClassIds::DEBUG, 160024, false, true, true)
STAT_EVENT_SET_DEF(ASYNC_GENERIC_LOG_WRITE_COUNT, "async generic log write count", ObStatClassIds::DEBUG, 160025, false, true, true)
STAT_EVENT_SET_DEF(ASYNC_USER_REQUEST_LOG_WRITE_COUNT, "async user request log write count", ObStatClassIds::DEBUG, 160026, false, true, true)
STAT_EVENT_SET_DEF(ASYNC_DATA_MAINTAIN_LOG_WRITE_COUNT, "async data maintain log write count", ObStatClassIds::DEBUG, 160027, false, true, true)
STAT_EVENT_SET_DEF(ASYNC_ROOT_SERVICE_LOG_WRITE_COUNT, "async root service log write count", ObStatClassIds::DEBUG, 160028, false, true, true)
STAT_EVENT_SET_DEF(ASYNC_SCHEMA_LOG_WRITE_COUNT, "async schema log write count", ObStatClassIds::DEBUG, 160029, false, true, true)
STAT_EVENT_SET_DEF(ASYNC_FORCE_ALLOW_LOG_WRITE_COUNT, "async force allow log write count", ObStatClassIds::DEBUG, 160030, false, true, true)
STAT_EVENT_SET_DEF(ASYNC_GENERIC_LOG_DROPPED_COUNT, "async generic log dropped count", ObStatClassIds::DEBUG, 160031, false, true, true)
STAT_EVENT_SET_DEF(ASYNC_USER_REQUEST_LOG_DROPPED_COUNT, "async user request log dropped count", ObStatClassIds::DEBUG, 160032, false, true, true)
STAT_EVENT_SET_DEF(ASYNC_DATA_MAINTAIN_LOG_DROPPED_COUNT, "async data maintain log dropped count", ObStatClassIds::DEBUG, 160033, false, true, true)
STAT_EVENT_SET_DEF(ASYNC_ROOT_SERVICE_LOG_DROPPED_COUNT, "async root service log dropped count", ObStatClassIds::DEBUG, 160034, false, true, true)
STAT_EVENT_SET_DEF(ASYNC_SCHEMA_LOG_DROPPED_COUNT, "async schema log dropped count", ObStatClassIds::DEBUG, 160035, false, true, true)
STAT_EVENT_SET_DEF(ASYNC_FORCE_ALLOW_LOG_DROPPED_COUNT, "async force allow log dropped count", ObStatClassIds::DEBUG, 160036, false, true, true)
STAT_EVENT_SET_DEF(RINGBUF_ALLOC_WAIT_COUNT, "ringbuf alloc wait count", ObStatClassIds::DEBUG, 160037, false, true, true)
STAT_EVENT_SET_DEF(RINGBUF_ALLOC_DROP_COUNT, "ringbuf alloc drop count", ObStatClassIds::DEBUG, 160038, false, true, true)
STAT_EVENT_SET_DEF(RINGBUF_ALLOC_SUCCESS_COUNT, "ringbuf alloc success count", ObStatClassIds::DEBUG, 160039, false, true, true)
STAT_EVENT_SET_DEF(RINGBUF_QUEUE_DEPTH, "ringbuf queue depth", ObStatClassIds::DEBUG, 160040, false, true, true)

// rootservice

// das
STAT_EVENT_SET_DEF(DAS_PARALLEL_MEMORY_USAGE, "the memory use of all DAS parallel tasks", ObStatClassIds::SQL, 230001, false, true, true)
STAT_EVENT_SET_DEF(DAS_PARALLEL_TASK_CNT, "the count of DAS parallel tasks", ObStatClassIds::SQL, 230002, false, true, true)

// END
STAT_EVENT_SET_DEF(STAT_EVENT_SET_END, "event set end", ObStatClassIds::DEBUG, 300000, false, false, true)
#endif

#ifndef OB_STAT_EVENT_DEFINE_H_
#define OB_STAT_EVENT_DEFINE_H_

#include "lib/time/ob_time_utility.h"
#include "lib/statistic_event/ob_stat_class.h"


namespace oceanbase
{
namespace common
{
static const int64_t MAX_STAT_EVENT_NAME_LENGTH = 64;
static const int64_t MAX_STAT_EVENT_PARAM_LENGTH = 64;

struct ObStatEventIds
{
  enum ObStatEventIdEnum
  {
#define STAT_DEF_true(def, name, stat_class, stat_id, summary_in_session, can_visible) def,
#define STAT_DEF_false(def, name, stat_class, stat_id, summary_in_session, can_visible)

#define STAT_EVENT_ADD_DEF(def, name, stat_class, stat_id, summary_in_session, can_visible, enable)\
STAT_DEF_##enable(def, name, stat_class, stat_id, summary_in_session, can_visible)
#include "lib/statistic_event/ob_stat_event.h"
#undef STAT_EVENT_ADD_DEF
#define STAT_EVENT_SET_DEF(def, name, stat_class, stat_id, summary_in_session, can_visible, enable)\
STAT_DEF_##enable(def, name, stat_class, stat_id, summary_in_session, can_visible)
#include "lib/statistic_event/ob_stat_event.h"
#undef STAT_EVENT_SET_DEF
#undef STAT_DEF_true
#undef STAT_DEF_false
  };
};

struct ObStatEventAddStat
{
  //value for a stat
  int64_t stat_value_;
  ObStatEventAddStat();
  int add(const ObStatEventAddStat &other);
  int add(int64_t value);
  int atomic_add(int64_t value);
  int64_t get_stat_value();
  inline bool is_valid() const { return true; }
};

inline int64_t ObStatEventAddStat::get_stat_value()
{
  return stat_value_;
}

struct ObStatEventSetStat
{
  //value for a stat
  int64_t stat_value_;
  ObStatEventSetStat();
  int add(const ObStatEventSetStat &other);
  void set(int64_t value);
  int64_t get_stat_value();
  inline bool is_valid() const { return stat_value_ > 0; }
};

inline int64_t ObStatEventSetStat::get_stat_value()
{
  return stat_value_;
}

inline void ObStatEventSetStat::set(int64_t value)
{
  stat_value_ = value;
}

struct ObStatEvent
{
  char name_[MAX_STAT_EVENT_NAME_LENGTH];
  int64_t stat_class_;
  int64_t stat_id_;
  bool summary_in_session_;
  bool can_visible_;
};


extern const ObStatEvent OB_STAT_EVENTS[];

}
}

#endif /* OB_WAIT_EVENT_DEFINE_H_ */
