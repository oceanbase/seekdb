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

#ifndef OB_DEFINE_H
#define OB_DEFINE_H

#include "lib/ob_define.h"
#include "lib/compress/ob_compress_util.h"
#include "lib/container/ob_se_array.h"
#include "lib/profile/ob_trace_id.h"
#include "common/ob_tablet_id.h"
#include "share/ob_errno.h"
#include "lib/worker.h"
#include "cmath"
#ifdef __linux__
#include <features.h>
#if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ > 17)
using std::isinf;
using std::isnan;
#endif
#elif defined(__APPLE__)
// macOS doesn't have features.h, but std::isinf and std::isnan are available in <cmath>
using std::isinf;
using std::isnan;
#endif

/****** UTILS FOR PROGRAMMING *****/
#include "lib/ob_check_macros.h"

namespace oceanbase {
namespace common {

// iternal recyclebin object prefix
const char *const OB_MYSQL_RECYCLE_PREFIX = "__recycle_$_";
const char *const OB_RECYCLE_PREFIX = "RECYCLE_$_";

OB_INLINE bool is_valid_log_compressor_type(common::ObCompressorType compressor_type)
{
   bool b_ret = false;
   if (common::ObCompressorType::ZSTD_1_3_8_COMPRESSOR == compressor_type) {
    b_ret = true;
   }
   return b_ret;
}
//check whether transaction version is valid
OB_INLINE bool is_valid_trans_version(const int64_t trans_version)
{
  // When the observer has not performed any transactions, publish_version is 0
  return trans_version >= 0;
}

OB_INLINE bool is_valid_membership_version(const int64_t membership_version)
{
  // When the observer does not perform any member changes, membership_version is 0
  return membership_version >= 0;
}

OB_INLINE bool is_valid_read_snapshot_version(const int64_t read_snapshot_version)
{
  // read snapshot version should be greater than 0 and should not be INT64_MAX
  return read_snapshot_version > 0 && INT64_MAX != read_snapshot_version;
}

inline bool is_need_retry_interval_part_error(int code)
{
  bool ret = false;
  if (OB_ERR_INTERVAL_PARTITION_EXIST == code
     || OB_ERR_INTERVAL_PARTITION_ERROR == code) {
    ret = true;
  }
  return ret;
}

inline bool is_schema_error(int err)
{
  bool ret = false;
  switch(err) {
    case OB_TENANT_EXIST:
    case OB_TENANT_NOT_EXIST:
    case OB_ERR_BAD_DATABASE:
    case OB_DATABASE_EXIST:
    case OB_TABLEGROUP_NOT_EXIST:
    case OB_TABLEGROUP_EXIST:
    case OB_TABLE_NOT_EXIST:
    case OB_ERR_TABLE_EXIST:
    case OB_ERR_BAD_FIELD_ERROR:
    case OB_ERR_COLUMN_DUPLICATE:
    case OB_ERR_USER_EXIST:
    case OB_ERR_USER_NOT_EXIST:
    case OB_ERR_NO_PRIVILEGE:
    case OB_ERR_NO_DB_PRIVILEGE:
    case OB_ERR_NO_TABLE_PRIVILEGE:
    case OB_SCHEMA_ERROR:
    case OB_ERR_WAIT_REMOTE_SCHEMA_REFRESH:
    case OB_ERR_REMOTE_SCHEMA_NOT_FULL:
    case OB_ERR_SP_ALREADY_EXISTS:
    case OB_ERR_SP_DOES_NOT_EXIST:
    case OB_OBJECT_NAME_NOT_EXIST:
    case OB_OBJECT_NAME_EXIST:
    case OB_SCHEMA_EAGAIN:
    case OB_SCHEMA_NOT_UPTODATE:
    case OB_ERR_PARALLEL_DDL_CONFLICT:
    case OB_NO_PARTITION_FOR_GIVEN_VALUE_SCHEMA_ERROR:
    case OB_ERR_DDL_RESOURCE_NOT_ENOUGH:
      ret = true;
      break;
    default:
      break;
  }
  return ret;
}

// this function only used for error logging
// expr eval error range (-5000, -6000]
inline bool should_catch_err(int err)
{
  bool ret = false;
  // think that expr_eval err only in (-5000, -6000] should catch
  if (err > -6000 && err < -5000) {
    ret = true;
  } else {
    switch (err) {
    case OB_ERR_DIVISOR_IS_ZERO:
    case OB_INVALID_DATE_VALUE:
    case OB_INVALID_DATE_FORMAT:
    case OB_BAD_NULL_ERROR:
    case OB_ERR_VALUE_LARGER_THAN_ALLOWED:
      ret = true;
      break;
    default:
      break;
    }
  }
  return ret;
}

inline bool is_duplicate_key_err(int err)
{
  bool ret = false;
  if (OB_ERR_PRIMARY_KEY_DUPLICATE == err) {
    ret = true;
  }
  return ret;
}

inline bool is_get_location_timeout_error(int err)
{
  return OB_GET_LOCATION_TIME_OUT == err;
}

inline bool is_partition_change_error(int err)
{
  bool ret = false;
  switch (err) {
    case OB_PARTITION_NOT_EXIST:
    case OB_LOCATION_NOT_EXIST:
    case OB_PARTITION_IS_STOPPED:
    case OB_PARTITION_IS_BLOCKED:
    case OB_LS_LOCATION_NOT_EXIST:
    case OB_MAPPING_BETWEEN_TABLET_AND_LS_NOT_EXIST:
    case OB_LS_NOT_EXIST:
    case OB_TABLET_NOT_EXIST:
      ret = true;
      break;
    default:
      break;
  }
  return ret;
}

inline bool is_server_down_error(int err)
{
  bool ret = false;
  ret = (OB_RPC_CONNECT_ERROR == err || OB_RPC_SEND_ERROR == err || OB_RPC_POST_ERROR == err);
  return ret;
}

inline bool is_trans_stmt_need_retry_error(int err)
{
  bool ret = false;
  ret = (OB_TRANS_STMT_NEED_RETRY == err);
  return ret;
}

inline bool is_server_status_error(int err)
{
  bool ret = false;
  ret = (OB_SERVER_IS_INIT == err || OB_SERVER_IS_STOPPING == err);
  return ret;
}

inline bool is_unit_migrate(int err)
{
  return OB_TENANT_NOT_IN_SERVER == err;
}

inline bool is_process_timeout_error(int err)
{
  bool ret = false;
  ret = (OB_TIMEOUT == err);
  return ret;
}

inline bool is_location_leader_not_exist_error(int err)
{
  return OB_LOCATION_LEADER_NOT_EXIST == err
      || OB_LS_LOCATION_LEADER_NOT_EXIST == err;
}

inline bool is_master_changed_error(int err)
{
  bool ret = false;
  switch (err) {
    case OB_LOCATION_LEADER_NOT_EXIST:
    case OB_LS_LOCATION_LEADER_NOT_EXIST:
    case OB_NOT_MASTER:
    case OB_RS_NOT_MASTER:
    case OB_RS_SHUTDOWN:
      ret = true;
      break;
    default:
      ret = false;
      break;
  }
  return ret;
}

inline bool is_timeout_err(int err)
{
  return OB_TIMEOUT == err
      || OB_TRANS_TIMEOUT == err
      || OB_TRANS_STMT_TIMEOUT == err
      || OB_TRANS_RPC_TIMEOUT == err;
}

inline bool is_not_supported_err(int err)
{
  return OB_NOT_SUPPORTED == err;
}

inline bool is_try_lock_row_err(int err)
{
  return OB_TRY_LOCK_ROW_CONFLICT == err;
}

inline bool is_transaction_set_violation_err(int err)
{
  return OB_TRANSACTION_SET_VIOLATION == err;
}

inline bool is_transaction_cannot_serialize_err(int err)
{
  return OB_TRANS_CANNOT_SERIALIZE == err;
}

inline bool is_snapshot_discarded_err(const int err)
{
  return OB_SNAPSHOT_DISCARDED == err;
}

inline bool is_transaction_rpc_timeout_err(int err)
{
  return OB_TRANS_RPC_TIMEOUT == err;
}

inline bool is_data_not_readable_err(int err)
{
  return OB_DATA_NOT_UPTODATE == err
         || OB_REPLICA_NOT_READABLE == err
         || OB_SNAPSHOT_DISCARDED == err;
}

inline bool is_has_no_readable_replica_err(int err)
{
  return OB_NO_READABLE_REPLICA == err;
}

inline bool is_id_not_ready_err(const int err)
{
  return OB_GTS_NOT_READY == err || OB_GTI_NOT_READY == err;
}

inline bool is_weak_read_service_ready_err(const int err)
{
  return OB_TRANS_WEAK_READ_VERSION_NOT_READY == err;
}

inline bool is_static_engine_retry(const int err)
{
  return STATIC_ENG_NOT_IMPLEMENT == err;
}

inline void set_interval_partition_insert_error(int &ret)
{
  ret = OB_NO_PARTITION_FOR_INTERVAL_PART;
}
inline bool is_interval_partition_insert_error(const int err)
{
  return OB_NO_PARTITION_FOR_INTERVAL_PART == err;
}

inline bool is_query_killed_return(const int ret)
{
  // TODO(handora.qc): check the mode for OB_DEAD_LOCK
  return OB_ERR_QUERY_INTERRUPTED == ret
    || OB_DEAD_LOCK == ret;
}

//@TODO shanyan.g Temporary settings for elr
static const bool CAN_ELR = false;

#define LOG_WARN_IGNORE_ITER_END(ret, fmt, args...) \
  do {\
    if (OB_UNLIKELY(common::OB_ITER_END != ret)) {\
      LOG_WARN(fmt, ##args);\
    }\
  } while(0);

// Weakly consistent read related macros
const int64_t OB_WRS_LEVEL_VALUE_LENGTH = 128; // Maximum length of the level_value field of the __all_weak_read_service internal table
const int64_t OB_WRS_LEVEL_NAME_LENGTH = 128; // Maximum length of the level_name field of the __all_weak_read_service internal table

//Encryption related macros
const int64_t OB_MAX_ENCRYPTION_NAME_LENGTH = 128;
const int64_t OB_MAX_ENCRYPTION_KEY_NAME_LENGTH = 256;
const char *const OB_MYSQL_ENCRYPTION_DEFAULT_MODE = "aes-128";
const char *const OB_MYSQL_ENCRYPTION_NONE_MODE = "none";
//--end---Encryption related macros
const int64_t OB_MAX_ENCRYPTION_MODE_LENGTH = 64;

/**
 * Review found that in the definitions of internal tables and internal views, OB_MAX_TABLE_NAME_LENGTH is used in many places to limit the field length to 128 bytes,
 * but the semantics of the corresponding fields are not table_name.
 * Since it is necessary to adjust the length of OB_MAX_TABLE_NAME_LENGTH to 256, to ensure that the definitions of internal tables and views using OB_MAX_TABLE_NAME_LENGTH remain unchanged,
 * the following definitions are added to replace the original OB_MAX_TABLE_NAME_LENGTH
 */
const int64_t OB_MAX_CORE_TALBE_NAME_LENGTH = 128;
const int64_t OB_MAX_OUTLINE_NAME_LENGTH = 128;
const int64_t OB_MAX_ROUTINE_NAME_LENGTH = 128;
const int64_t OB_MAX_ROUTINE_NAME_BINARY_LENGTH = 2048; // Should be OB_MAX_ROUTINE_NAME_LENGTH * 4(max char bytes), 
                                                         // reserve some bytes thus OB_MAX_ROUTINE_NAME_LENGTH changes will probably not influence it
                                                         // it is defined in primary key, and can not change randomly.
const int64_t OB_MAX_PACKAGE_NAME_LENGTH = 128;
const int64_t OB_MAX_KVCACHE_NAME_LENGTH = 128;
const int64_t OB_MAX_SYNONYM_NAME_LENGTH = 128;
const int64_t OB_MAX_PARAMETERS_NAME_LENGTH = 128;
const int64_t OB_MAX_RESOURCE_PLAN_NAME_LENGTH = 128;
// end for const define replace OB_MAX_TABLE_NAME_LENGTH

///////////////////////////////////////////////////////
//          Schema defination                        //

// internal aux-vertical partition table name prefix
const char *const OB_AUX_VP_PREFIX = "__AUX_VP_";

//          End of Schema defination                 //
///////////////////////////////////////////////////////

const int64_t OB_STATUS_LENGTH = 64;


///////////////////////////
//// used for replay
const int64_t REPLAY_TASK_QUEUE_SIZE = 32;
const int64_t APPLY_TASK_QUEUE_SIZE = 64;
inline int64_t &get_replay_queue_index()
{
  struct DEFAULT_WRAPPER {
    DEFAULT_WRAPPER() : v_(-1) {}
    int64_t v_;
  };
  RLOCAL_INLINE(DEFAULT_WRAPPER, replay_queue_index);
  return (&replay_queue_index)->v_;
}

inline bool &get_replay_is_writing_throttling()
{
  struct DEFAULT_WRAPPER {
    DEFAULT_WRAPPER() : v_(false) {}
    bool v_;
  };
  RLOCAL_INLINE(DEFAULT_WRAPPER, is_writing_throttling);
  return (&is_writing_throttling)->v_;
}
///////////////////////////////////
//max concurrency for log external upload
const int64_t OB_MAX_LOG_UPLOAD_CONCURRENCY = 16;

enum ObDmlEventType
{
  DE_INVALID = 0,
  DE_INSERTING = (1 << 0),
  DE_UPDATING = (1 << 1),
  DE_DELETING = (1 << 2)
};

const char *const NORMAL_MODE_STR = "normal";
const char *const ARBITRATION_MODE_STR = "arbitration";
const char *const DISABLED_CLUSTER_MODE_STR = "disabled_cluster";
const char *const DISABLED_WITH_READONLY_CLUSTER_MODE_STR = "disabled_with_readonly_cluster";
static const int64_t MODIFY_GC_SNAPSHOT_INTERVAL = 2 * 1000 * 1000; //2s

//reserved table id for information schema
const uint64_t OB_ALL_VIRTUAL_PARAMETERS_OLD_TID = 12037; // "PARAMETERS_OLD"
const uint64_t OB_ALL_VIRTUAL_TABLE_CONSTRAINTS_OLD_TID = 12005; // "TABLE_CONSTRAINTS_OLD"
const uint64_t OB_ALL_VIRTUAL_REFERENTIAL_CONSTRAINTS_OLD_TID = 12177; // "REFERENTIAL_CONSTRAINTS_OLD"
const uint64_t OB_ALL_VIRTUAL_CHECK_CONSTRAINTS_OLD_TID = 12235; // "CHECK_CONSTRAINTS_OLD"
const uint64_t OB_ALL_VIRTUAL_TRIGGERS_OLD_TID = 12221; // "TRIGGERS_OLD"
const uint64_t OB_TABLE_PRIVILEGES_OLD_TID = 12002;  // not used anymore for "TABLE_PRIVILEGES" has a new table id
const uint64_t OB_USER_PRIVILEGES_OLD_TID = 12003;   // not used anymore for "USER_PRIVILEGES" has a new table id
const uint64_t OB_SCHEMA_PRIVILEGES_OLD_TID = 12004; // not used anymore for "SCHEMA_PRIVILEGES" has a new table id
const uint64_t OB_PARTITIONS_OLD_TID = 12007;        // not used anymore for "PARTITIONS" has a new table id
const uint64_t OB_ALL_VIRTUAL_PROC_OLD_TID = 12030;              // not used anymore for "PROC" has a new table id
//end of reserved table id for information schema

////////////////typedef
typedef common::ObSEArray<int64_t, 8> PartitionIdArray;
///////////////
}  // common
namespace share
{
// alias
using ObTaskId = ::oceanbase::common::ObCurTraceId::TraceId;
}  // share
}  // oceanbase

#endif /* OB_DEFINE_H */
