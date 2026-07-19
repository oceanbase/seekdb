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

#ifndef OCEANBASE_LOGSERVICE_OB_LOG_BASE_TYPE_
#define OCEANBASE_LOGSERVICE_OB_LOG_BASE_TYPE_

#include "logservice/palf/lsn.h"

namespace oceanbase
{
namespace share
{
class SCN;
}
namespace logservice
{
enum ObLogBaseType
{
  INVALID_LOG_BASE_TYPE = 0,

  TRANS_SERVICE_LOG_BASE_TYPE = 1,

  TABLET_OP_LOG_BASE_TYPE = 2,

  STORAGE_SCHEMA_LOG_BASE_TYPE = 3,

  TABLET_SEQ_SYNC_LOG_BASE_TYPE = 4,

  DDL_LOG_BASE_TYPE = 5,

  KEEP_ALIVE_LOG_BASE_TYPE = 6,

  TIMESTAMP_LOG_BASE_TYPE = 7,

  TRANS_ID_LOG_BASE_TYPE = 8,

  GC_LS_LOG_BASE_TYPE = 9,

  MAJOR_FREEZE_LOG_BASE_TYPE = 10,

  //for primary_ls_service
  PRIMARY_LS_SERVICE_LOG_BASE_TYPE = 11,

  //for recovery_ls_service
  RECOVERY_LS_SERVICE_LOG_BASE_TYPE = 12,

  //for standby timestamp service
  STANDBY_TIMESTAMP_LOG_BASE_TYPE = 13,

  //for recovery_ls_service
  RESTORE_SERVICE_LOG_BASE_TYPE = 16,

  RESERVED_SNAPSHOT_LOG_BASE_TYPE = 17,

  MEDIUM_COMPACTION_LOG_BASE_TYPE = 18,

  // for arb garbage collect service, has not been used for now
  ARB_GARBAGE_COLLECT_SERVICE_LOG_BASE_TYPE = 19,
  // for data_dictionary_service
  DATA_DICT_LOG_BASE_TYPE = 20,

  // for arbitration service
  ARBITRATION_SERVICE_LOG_BASE_TYPE = 21,

  // for NET_STANDBY_TNT_SERVICE
  NET_STANDBY_TNT_SERVICE_LOG_BASE_TYPE = 22,

  // 23 was used by the removed endpoint ingress bandwidth service. Do not reuse.

  HEARTBEAT_SERVICE_LOG_BASE_TYPE = 24,

  // for padding log entry
  PADDING_LOG_BASE_TYPE = 25,

  // for dup table trans
  DUP_TABLE_LOG_BASE_TYPE = 26,

  // for obj lock garbage collect service
  OBJ_LOCK_GARBAGE_COLLECT_SERVICE_LOG_BASE_TYPE = 27,

  // 31 - 34 were used by removed backup/archive services. Do not reuse.

  COMMON_LS_SERVICE_LOG_BASE_TYPE = 36,

  // local lifecycle callback only; does not write clog
  LS_BLOCK_TX_SERVICE_LOG_BASE_TYPE = 37,


  TTL_LOG_BASE_TYPE = 39,

  // for tenant snapshot
  SNAPSHOT_SCHEDULER_LOG_BASE_TYPE = 41,
  //for tenant clone
  CLONE_SCHEDULER_LOG_BASE_TYPE = 42,

  // for DBMS_SCHEDULER
  DBMS_SCHEDULER_LOG_BASE_TYPE = 45,

  // for shared storage pre warm
  SHARED_STORAGE_PRE_WARM_LOG_BASE_TYPE = 46,

  // for vector index
  VEC_INDEX_LOG_BASE_TYPE = 48,

  // for DBMS_SCHEDULER GC
  DBMS_SCHEDULER_GC_LOG_BASE_TYPE = 50,

  // for new DDL scheduler
  SYS_DDL_SCHEDULER_LOG_BASE_TYPE = 52,

  // for tenant disaster recovery (abandoned)
  // DISASTER_RECOVERY_SERVICE_LOG_BASE_TYPE = 53,

  // for new DDL service
  DDL_SERVICE_LAUNCHER_LOG_BASE_TYPE = 54,
  // for vector index scheduler
  VEC_INDEX_SERVICE_LOG_BASE_TYPE  = 60,
  // for the local sys tenant package service
  SYS_TENANT_LOAD_SYS_PACKAGE_SERVICE_LOG_BASE_TYPE = 61,
  // for internal table change notifier (timezone, SRS, etc.)
  INTERNAL_TABLE_NOTIFIER_LOG_BASE_TYPE = 62,
  // pay attention!!!
  // add log type in log_base_type_to_string
  // max value
  MAX_LOG_BASE_TYPE,
};

// Define the maximum length of ObLogBaseType string
static constexpr int64_t OB_LOG_BASE_TYPE_STR_MAX_LEN = 128;
static inline
int log_base_type_to_string(const ObLogBaseType log_type,
                            char *str,
                            const int64_t str_len)
{
  int ret = OB_SUCCESS;
  if (log_type == INVALID_LOG_BASE_TYPE) {
    strncpy(str ,"INVALID_TYPE", str_len);
  } else if (log_type == TRANS_SERVICE_LOG_BASE_TYPE) {
    strncpy(str ,"TRANS_SERVICE", str_len);
  } else if (log_type == TABLET_OP_LOG_BASE_TYPE) {
    strncpy(str ,"TABLET_OP", str_len);
  } else if (log_type == STORAGE_SCHEMA_LOG_BASE_TYPE) {
    strncpy(str ,"STORAGE_SCHEMA", str_len);
  } else if (log_type == TABLET_SEQ_SYNC_LOG_BASE_TYPE) {
    strncpy(str ,"TABLET_SEQ_SYNC", str_len);
  } else if (log_type == DDL_LOG_BASE_TYPE) {
    strncpy(str ,"DDL", str_len);
  } else if (log_type == KEEP_ALIVE_LOG_BASE_TYPE) {
    strncpy(str ,"KEEP_ALIVE", str_len);
  } else if (log_type == TIMESTAMP_LOG_BASE_TYPE) {
    strncpy(str ,"TIMESTAMP", str_len);
  } else if (log_type == TRANS_ID_LOG_BASE_TYPE) {
    strncpy(str ,"TRANS_ID", str_len);
  } else if (log_type == GC_LS_LOG_BASE_TYPE) {
    strncpy(str ,"GC_LS", str_len);
  } else if (log_type == MAJOR_FREEZE_LOG_BASE_TYPE) {
    strncpy(str ,"MAJOR_FREEZE", str_len);
  } else if (log_type == PRIMARY_LS_SERVICE_LOG_BASE_TYPE) {
    strncpy(str ,"PRIMARY_LS_SERVICE", str_len);
  } else if (log_type == RECOVERY_LS_SERVICE_LOG_BASE_TYPE) {
    strncpy(str ,"RECOVERY_LS_SERVICE", str_len);
  } else if (log_type == STANDBY_TIMESTAMP_LOG_BASE_TYPE) {
    strncpy(str ,"STANDBY_TIMESTAMP", str_len);
  } else if (log_type == RESTORE_SERVICE_LOG_BASE_TYPE) {
    strncpy(str ,"RESTORE_SERVICE", str_len);
  } else if (log_type == RESERVED_SNAPSHOT_LOG_BASE_TYPE) {
    strncpy(str ,"RESERVED_SNAPSHOT", str_len);
  } else if (log_type == MEDIUM_COMPACTION_LOG_BASE_TYPE) {
    strncpy(str ,"MEDIUM_COMPACTION", str_len);
  } else if (log_type == ARB_GARBAGE_COLLECT_SERVICE_LOG_BASE_TYPE) {
    strncpy(str ,"ARB_GARBAGE_COLLECTE_SERVICE", str_len);
  } else if (log_type == DATA_DICT_LOG_BASE_TYPE) {
    strncpy(str ,"DATA_DICTIONARY_SERVICE", str_len);
  } else if (log_type == ARBITRATION_SERVICE_LOG_BASE_TYPE) {
    strncpy(str ,"ARBITRATION_SERVICE", str_len);
  } else if (log_type == NET_STANDBY_TNT_SERVICE_LOG_BASE_TYPE) {
    strncpy(str ,"NET_STANDBY_TNT_SERVICE", str_len);
  } else if (log_type == HEARTBEAT_SERVICE_LOG_BASE_TYPE) {
    strncpy(str ,"HEARTBEAT_SERVICE", str_len);
  } else if (log_type == PADDING_LOG_BASE_TYPE) {
    strncpy(str ,"PADDING_LOG_ENTRY", str_len);
  } else if (log_type == DUP_TABLE_LOG_BASE_TYPE) {
    strncpy(str ,"DUP_TABLE", str_len);
  } else if (log_type == OBJ_LOCK_GARBAGE_COLLECT_SERVICE_LOG_BASE_TYPE) {
    strncpy(str ,"OBJ_LOCK_GARBAGE_COLLECT_SERVICE", str_len);
  } else if (log_type == COMMON_LS_SERVICE_LOG_BASE_TYPE) {
    strncpy(str ,"COMMON_LS_SERVICE", str_len);
  } else if (log_type == LS_BLOCK_TX_SERVICE_LOG_BASE_TYPE) {
    strncpy(str ,"BLOCK_TX_SERVICE", str_len);
  } else if (log_type == DBMS_SCHEDULER_LOG_BASE_TYPE) {
    strncpy(str ,"DBMS_SCHEDULER", str_len);
  } else if (log_type == TTL_LOG_BASE_TYPE) {
    strncpy(str ,"TTL_SERVICE", str_len);
  } else if (log_type == SNAPSHOT_SCHEDULER_LOG_BASE_TYPE) {
    strncpy(str ,"SNAPSHOT_SCHEDULER", str_len);
  } else if (log_type == CLONE_SCHEDULER_LOG_BASE_TYPE) {
    strncpy(str ,"CLONE_SCHEDULER", str_len);
  } else if (log_type == SHARED_STORAGE_PRE_WARM_LOG_BASE_TYPE) {
    strncpy(str ,"SHARED_STORAGE_PRE_WARM_LOG_BASE_TYPE", str_len);
  } else if (log_type == VEC_INDEX_LOG_BASE_TYPE) {
    strncpy(str ,"VEC_INDEX_SERVICE", str_len);
  } else if (log_type == DBMS_SCHEDULER_LOG_BASE_TYPE) {
    strncpy(str ,"DBMS_SCHEDULER", str_len);
  } else if (log_type == DBMS_SCHEDULER_GC_LOG_BASE_TYPE) {
    strncpy(str ,"DBMS_SCHEDULER_GC", str_len);
  } else if (log_type == SYS_DDL_SCHEDULER_LOG_BASE_TYPE) {
    strncpy(str ,"SYS_DDL_SCHEDULER", str_len);
  } else if (log_type == DDL_SERVICE_LAUNCHER_LOG_BASE_TYPE) {
    strncpy(str ,"DDL_SERVICE_LAUNCHER", str_len);
  } else if (log_type == SYS_TENANT_LOAD_SYS_PACKAGE_SERVICE_LOG_BASE_TYPE) {
    strncpy(str ,"SYS_TENANT_LOAD_SYS_PACKAGE_SERVICE", str_len);
  } else if (log_type == VEC_INDEX_SERVICE_LOG_BASE_TYPE ) {
    strncpy(str, "VEC_INDEX_SERVICE", str_len);
  } else if (log_type == INTERNAL_TABLE_NOTIFIER_LOG_BASE_TYPE) {
    strncpy(str, "INTERNAL_TABLE_NOTIFIER", str_len);
  } else {
    ret = OB_INVALID_ARGUMENT;
  }

  if (str_len > 0) {
    str[str_len - 1] = '\0';
  }
  return ret;
}

inline bool is_valid_log_base_type(const ObLogBaseType &type)
{
  return type > INVALID_LOG_BASE_TYPE && type < MAX_LOG_BASE_TYPE;
}

class ObIReplaySubHandler
{
public:
  virtual int replay(const void *buffer,
                     const int64_t nbytes,
                     const palf::LSN &lsn,
                     const share::SCN &scn) = 0;
};

class ObILocalLogHandler
{
public:
  virtual void deactivate() = 0;
  virtual int activate() = 0;
  VIRTUAL_TO_STRING_KV("ObILocalLogHandler", "Dummy");
};

// services inherit from ObICheckpointSubHandler
// register in ObCheckpointExecutor
class ObICheckpointSubHandler
{
public:
  virtual share::SCN get_rec_scn() = 0;
  virtual int flush(share::SCN &scn) = 0;
};

#define REGISTER_TO_LOGSERVICE(type, subhandler)                                            \
  if (OB_SUCC(ret)) {                                                                       \
    if (OB_FAIL(replay_handler_.register_handler(type, subhandler))) {                      \
      LOG_WARN("replay_handler_ register failed", K(ret), K(type));                        \
    } else if (OB_FAIL(local_log_handler_set_.register_handler(type, subhandler))) {          \
      LOG_WARN("local_log_handler_set_ register failed", K(ret), K(type));                 \
    } else if (OB_FAIL(checkpoint_executor_.register_handler(type, subhandler))) {          \
      LOG_WARN("checkpoint_executor_ register failed", K(ret), K(type));                   \
    } else {                                                                                \
      LOG_INFO("register to logservice success", K(type));                                 \
    }                                                                                       \
  }

#define UNREGISTER_FROM_LOGSERVICE(type, subhandler)                                        \
  (void)replay_handler_.unregister_handler(type);                                           \
  (void)local_log_handler_set_.unregister_handler(type);                                      \
  (void)checkpoint_executor_.unregister_handler(type);                                      \

#define REGISTER_REPLAY_CHECKPOINT_HANDLER(type, subhandler)                                \
  if (OB_SUCC(ret)) {                                                                        \
    if (OB_FAIL(replay_handler_.register_handler(type, subhandler))) {                       \
      LOG_WARN("replay_handler_ register failed", K(ret), K(type));                         \
    } else if (OB_FAIL(checkpoint_executor_.register_handler(type, subhandler))) {           \
      LOG_WARN("checkpoint_executor_ register failed", K(ret), K(type));                    \
    } else {                                                                                 \
      LOG_INFO("register replay and checkpoint handler success", K(type));                  \
    }                                                                                        \
  }

#define UNREGISTER_REPLAY_CHECKPOINT_HANDLER(type)                                          \
  (void)replay_handler_.unregister_handler(type);                                            \
  (void)checkpoint_executor_.unregister_handler(type);                                       \

#define REGISTER_TO_RESTORESERVICE(type, subhandler)                                        \
  if (OB_SUCC(ret)) {                                                                       \
    if (OB_FAIL(restore_local_log_handler_set_.register_handler(type, subhandler))) {         \
      LOG_WARN("restore_local_log_handler_set_ register failed",                              \
          K(ret), K(type));                                                                 \
    } else {                                                                                \
      LOG_INFO("register to restoreservice success", K(type));                             \
    }                                                                                       \
  }

#define UNREGISTER_FROM_RESTORESERVICE(type, subhandler)                                     \
  (void)restore_local_log_handler_set_.unregister_handler(type);
} // namespace logservice
} // namespace oceanbase

#endif // OCEANBASE_LOGSERVICE_OB_LOG_BASE_TYPE_
