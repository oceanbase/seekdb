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

#ifndef OCEANBASE_SHARE_OB_DDL_COMMON_H
#define OCEANBASE_SHARE_OB_DDL_COMMON_H

namespace oceanbase { namespace common { struct ObDatum; } }
#include "lib/allocator/page_arena.h"
#include "share/config/ob_server_config.h"
#include "share/schema/ob_table_schema.h"
#include "share/schema/ob_schema_service.h"
#include "share/location_cache/ob_location_struct.h"
#include "common/ob_role.h"
#include "share/tablet/ob_tablet_read_mode.h"
#include "share/ob_batch_selector.h"

namespace oceanbase
{
namespace obcall
{
struct ObAlterTableArg;
}
namespace share
{
class ObSQLiteConnectionPool;
namespace schema { class ObMultiVersionSchemaService; }
// the block_sstable_struct.h chain previously provided header-level using declarations for bare names; the chain is gone, so add explicit using declarations (all have fwd declarations)
using schema::ObTableSchema;
using schema::ObSchemaGetterGuard;
using schema::ObSchemaService;
using schema::ObColDesc;
using schema::ObIndexType;
enum ObDDLType
{
  DDL_INVALID = 0,

  ///< @note add new normal long running ddl type before this line
  DDL_CHECK_CONSTRAINT = 1,
  DDL_FOREIGN_KEY_CONSTRAINT = 2,
  DDL_ADD_NOT_NULL_COLUMN = 3,
  DDL_MODIFY_AUTO_INCREMENT = 4,
  DDL_CREATE_INDEX = 5,
  DDL_DROP_INDEX = 6,
  DDL_CREATE_FTS_INDEX = 7,
  DDL_CREATE_PARTITIONED_LOCAL_INDEX = 10,
  DDL_DROP_LOB = 11,
  DDL_DROP_FTS_INDEX = 12,
  DDL_DROP_MULVALUE_INDEX = 13,
  DDL_DROP_VEC_INDEX = 14,
  DDL_CREATE_VEC_INDEX = 15,
  DDL_CREATE_MULTIVALUE_INDEX = 16,
  DDL_REBUILD_INDEX = 17,
  DDL_CREATE_VEC_IVFFLAT_INDEX = 18,
  DDL_CREATE_VEC_IVFSQ8_INDEX = 19,
  DDL_CREATE_VEC_IVFPQ_INDEX = 20,
  DDL_DROP_VEC_IVFFLAT_INDEX = 21,
  DDL_DROP_VEC_IVFSQ8_INDEX = 22,
  DDL_DROP_VEC_IVFPQ_INDEX = 23,
  DDL_DROP_VEC_SPIV_INDEX = 24,
  DDL_CREATE_VEC_SPIV_INDEX = 26, // placeholder of spiv post build

  ///< @note Drop schema, and refuse concurrent trans.
  DDL_DROP_SCHEMA_AVOID_CONCURRENT_TRANS = 500,
  DDL_DROP_DATABASE = 501,
  DDL_DROP_TABLE = 502,
  DDL_TRUNCATE_TABLE = 503,
  DDL_DROP_PARTITION = 504,
  DDL_DROP_SUB_PARTITION = 505,
  DDL_TRUNCATE_PARTITION = 506,
  DDL_TRUNCATE_SUB_PARTITION = 507,
  DDL_RENAME_PARTITION = 508,
  DDL_RENAME_SUB_PARTITION = 509,
  ///< @note add new double table long running ddl type before this line
  DDL_DOUBLE_TABLE_OFFLINE = 1000,
  DDL_MODIFY_COLUMN = 1001, // only modify columns
  DDL_ADD_PRIMARY_KEY = 1002,
  DDL_DROP_PRIMARY_KEY = 1003,
  DDL_ALTER_PRIMARY_KEY = 1004,
  DDL_ALTER_PARTITION_BY = 1005,
  DDL_DROP_COLUMN = 1006, // only drop columns
  DDL_CONVERT_TO_CHARACTER = 1007,
  DDL_ADD_COLUMN_OFFLINE = 1008, // only add columns
  DDL_COLUMN_REDEFINITION = 1009, // only add/drop columns
  DDL_TABLE_REDEFINITION = 1010,
  // 1011-1015 were used by removed DDL types. Do not reuse.
  // 1016 is reserved. Do not reuse.
  DDL_MODIFY_AUTO_INCREMENT_WITH_REDEFINITION = 1017,

  // @note new normal ddl type to be defined here !!!
  DDL_NORMAL_TYPE = 10001,
  DDL_ADD_COLUMN_ONLINE = 10002, // only add trailing columns
  DDL_CHANGE_COLUMN_NAME = 10003,
  DDL_DROP_COLUMN_INSTANT = 10004,
  DDL_ADD_COLUMN_INSTANT = 10006, // add after/before column
  DDL_COMPOUND_INSTANT = 10007,
  // 10008 is reserved. Do not reuse.
  DDL_FORK_TABLE = 10009, // fork table
  ///< @note add new normal ddl type before this line
  DDL_MAX
};
const char *get_ddl_type(ObDDLType ddl_type);

enum ObDDLTaskType
{
  INVALID_TASK = 0,
  REBUILD_INDEX_TASK = 1,
  REBUILD_CONSTRAINT_TASK = 2,
  REBUILD_FOREIGN_KEY_TASK = 3,
  MAKE_DDL_TAKE_EFFECT_TASK = 4,
  CLEANUP_GARBAGE_TASK = 5,
  MODIFY_FOREIGN_KEY_STATE_TASK = 6,
  // used in rollback_failed_add_not_null_columns() in ob_constraint_task.cpp.
  DELETE_COLUMN_FROM_SCHEMA = 7,
  // remap all index tables to hidden table and take effect through one rpc, applied in drop column for 4.0.
  REMAP_INDEXES_AND_TAKE_EFFECT_TASK = 8,
  UPDATE_AUTOINC_SCHEMA = 9,
  CANCEL_DDL_TASK = 10,
  MODIFY_NOT_NULL_COLUMN_STATE_TASK = 11,
  SWITCH_VEC_INDEX_NAME_TASK = 15
};

enum ObDDLTaskStatus { // FARM COMPAT WHITELIST
  PREPARE = 0,
  OBTAIN_SNAPSHOT = 1,
  WAIT_TRANS_END = 2,
  REDEFINITION = 3,
  VALIDATE_CHECKSUM = 4,
  COPY_TABLE_DEPENDENT_OBJECTS = 5,
  TAKE_EFFECT = 6,
  CHECK_CONSTRAINT_VALID = 7,
  SET_CONSTRAINT_VALIDATE = 8,
  MODIFY_AUTOINC = 9,
  SET_WRITE_ONLY = 10, // disused, just for compatibility.
  WAIT_TRANS_END_FOR_WRITE_ONLY = 11,
  SET_UNUSABLE = 12,
  WAIT_TRANS_END_FOR_UNUSABLE = 13,
  DROP_SCHEMA = 14,
  CHECK_TABLE_EMPTY = 15,
  WAIT_CHILD_TASK_FINISH = 16,
  REPENDING = 17,
  WAIT_FROZE_END = 19,
  WAIT_COMPACTION_END = 20,
  GENERATE_ROWKEY_DOC_SCHEMA = 25,
  WAIT_ROWKEY_DOC_TABLE_COMPLEMENT = 26,
  GENERATE_DOC_AUX_SCHEMA = 27,
  WAIT_AUX_TABLE_COMPLEMENT = 28,
  GENERATE_ROWKEY_VID_SCHEMA = 29,
  WAIT_ROWKEY_VID_TABLE_COMPLEMENT = 30,
  GENERATE_VEC_AUX_SCHEMA = 31,
  WAIT_VEC_AUX_TABLE_COMPLEMENT = 32,
  GENERATE_VID_ROWKEY_SCHEMA = 33,
  WAIT_VID_ROWKEY_TABLE_COMPLEMENT = 34,
  REBUILD_SCHEMA = 35,
  SWITCH_INDEX_NAME = 36,
  DROP_AUX_INDEX_TABLE = 38,
  DROP_LOB_META_ROW = 39,
  GENERATE_SQ_META_TABLE_SCHEMA = 40,
  WAIT_SQ_META_TABLE_COMPLEMENT = 41,
  GENERATE_CENTROID_TABLE_SCHEMA = 42,
  WAIT_CENTROID_TABLE_COMPLEMENT = 43,
  GENERATE_PQ_CENTROID_TABLE_SCHEMA = 44,
  WAIT_PQ_CENTROID_TABLE_COMPLEMENT = 45,
  LOAD_DICTIONARY = 46,
  BUILD_DATA = 48,
  WAIT_DATA_COMPLEMENT = 49,

  FAIL = 99,
  SUCCESS = 100
};

struct ObDDLTaskDataInfo final
{
  ObDDLTaskDataInfo()
      : data_format_version_(0),
        snapshot_version_(0),
        task_status_(ObDDLTaskStatus::PREPARE),
        target_object_id_(0),
        schema_version_(0),
        is_no_logging_(false),
        is_offline_index_rebuild_(false)
  {}

  TO_STRING_KV(K_(data_format_version), K_(snapshot_version), K_(task_status),
               K_(target_object_id), K_(schema_version), K_(is_no_logging),
               K_(is_offline_index_rebuild));

  uint64_t data_format_version_;
  int64_t snapshot_version_;
  ObDDLTaskStatus task_status_;
  uint64_t target_object_id_;
  int64_t schema_version_;
  bool is_no_logging_;
  bool is_offline_index_rebuild_;
};

const char *const temp_store_format_options[] =
{
  "auto",
  "zstd",
  "none",
};

enum SortCompactLevel
{
  SORT_DEFAULT_LEVEL = 0,
  SORT_COMPACT_LEVEL = 1,
  SORT_ENCODE_LEVEL = 2,
  SORT_COMPRESSION_LEVEL = 3,
  SORT_COMPRESSION_COMPACT_LEVEL = 4,
  SORT_COMPRESSION_ENCODE_LEVEL = 5
};

inline const char *ddl_task_status_to_str(const ObDDLTaskStatus &task_status)
{
  const char *str = nullptr;
  switch(task_status) {
    case share::ObDDLTaskStatus::PREPARE:
      str = "PREPARE";
      break;
    case share::ObDDLTaskStatus::OBTAIN_SNAPSHOT:
      str = "OBTAIN_SNAPSHOT";
      break;
    case share::ObDDLTaskStatus::WAIT_TRANS_END:
      str = "WAIT_TRANS_END";
      break;
    case share::ObDDLTaskStatus::REDEFINITION:
      str = "REDEFINITION";
      break;
    case share::ObDDLTaskStatus::VALIDATE_CHECKSUM:
      str = "VALIDATE_CHECKSUM";
      break;
    case share::ObDDLTaskStatus::COPY_TABLE_DEPENDENT_OBJECTS:
      str = "COPY_TABLE_DEPENDENT_OBJECTS";
      break;
    case share::ObDDLTaskStatus::TAKE_EFFECT:
      str = "TAKE_EFFECT";
      break;
    case share::ObDDLTaskStatus::CHECK_CONSTRAINT_VALID:
      str = "CHECK_CONSTRAINT_VALID";
      break;
    case share::ObDDLTaskStatus::SET_CONSTRAINT_VALIDATE:
      str = "SET_CONSTRAINT_VALIDATE";
      break;
    case share::ObDDLTaskStatus::MODIFY_AUTOINC:
      str = "MODIFY_AUTOINC";
      break;
    case share::ObDDLTaskStatus::SET_WRITE_ONLY:
      str = "SET_WRITE_ONLY";
      break;
    case share::ObDDLTaskStatus::WAIT_TRANS_END_FOR_WRITE_ONLY:
      str = "WAIT_TRANS_END_FOR_WRITE_ONLY";
      break;
    case share::ObDDLTaskStatus::SET_UNUSABLE:
      str = "SET_UNUSABLE";
      break;
    case share::ObDDLTaskStatus::WAIT_TRANS_END_FOR_UNUSABLE:
      str = "WAIT_TRANS_END_FOR_UNUSABLE";
      break;
    case share::ObDDLTaskStatus::DROP_SCHEMA:
      str = "DROP_SCHEMA";
      break;
    case ObDDLTaskStatus::CHECK_TABLE_EMPTY:
      str = "CHECK_TABLE_EMPTY";
      break;
    case ObDDLTaskStatus::WAIT_CHILD_TASK_FINISH:
      str = "WAIT_CHILD_TASK_FINISH";
      break;
    case ObDDLTaskStatus::REPENDING:
      str = "REPENDING";
      break;
    case ObDDLTaskStatus::WAIT_FROZE_END:
      str = "WAIT_FROZE_END";
      break;
    case ObDDLTaskStatus::WAIT_COMPACTION_END:
      str = "WAIT_COMPACTION_END";
      break;
    case ObDDLTaskStatus::GENERATE_ROWKEY_DOC_SCHEMA:
      str = "GENERATE_ROWKEY_DOC_SCHEMA";
      break;
    case ObDDLTaskStatus::LOAD_DICTIONARY:
      str = "LOAD_DICTIONARY";
      break;
    case ObDDLTaskStatus::GENERATE_DOC_AUX_SCHEMA:
      str = "GENERATE_DOC_AUX_SCHEMA";
      break;
    case ObDDLTaskStatus::WAIT_ROWKEY_DOC_TABLE_COMPLEMENT:
      str = "WAIT_ROWKEY_DOC_TABLE_COMPLEMENT";
      break;
    case ObDDLTaskStatus::WAIT_AUX_TABLE_COMPLEMENT:
      str = "WAIT_AUX_TABLE_COMPLEMENT";
      break;
    case ObDDLTaskStatus::GENERATE_ROWKEY_VID_SCHEMA:
      str = "GENERATE_ROWKEY_VID_SCHEMA";
      break;
    case ObDDLTaskStatus::WAIT_ROWKEY_VID_TABLE_COMPLEMENT:
      str = "WAIT_ROWKEY_VID_TABLE_COMPLEMENT";
      break;
    case ObDDLTaskStatus::GENERATE_VEC_AUX_SCHEMA:
      str = "GENERATE_VEC_AUX_SCHEMA";
      break;
    case ObDDLTaskStatus::WAIT_VEC_AUX_TABLE_COMPLEMENT:
      str = "WAIT_VEC_AUX_TABLE_COMPLEMENT";
      break;
    case ObDDLTaskStatus::GENERATE_VID_ROWKEY_SCHEMA:
      str = "GENERATE_VID_ROWKEY_SCHEMA";
      break;
    case ObDDLTaskStatus::WAIT_VID_ROWKEY_TABLE_COMPLEMENT:
      str = "WAIT_VID_ROWKEY_TABLE_COMPLEMENT";
      break;
    case ObDDLTaskStatus::REBUILD_SCHEMA:
      str = "REBUILD_SCHEMA";
      break;
    case ObDDLTaskStatus::SWITCH_INDEX_NAME:
      str = "SWITCH_INDEX_NAME";
      break;
    case ObDDLTaskStatus::DROP_AUX_INDEX_TABLE:
      str = "DROP_AUX_INDEX_TABLE";
      break;
    case ObDDLTaskStatus::DROP_LOB_META_ROW:
      str = "DROP_LOB_META_ROW";
      break;
    case ObDDLTaskStatus::GENERATE_SQ_META_TABLE_SCHEMA:
      str = "GENERATE_SQ_META_TABLE_SCHEMA";
      break;
    case ObDDLTaskStatus::WAIT_SQ_META_TABLE_COMPLEMENT:
      str = "WAIT_SQ_META_TABLE_COMPLEMENT";
      break;
    case ObDDLTaskStatus::GENERATE_CENTROID_TABLE_SCHEMA:
      str = "GENERATE_CENTROID_TABLE_SCHEMA";
      break;
    case ObDDLTaskStatus::WAIT_CENTROID_TABLE_COMPLEMENT:
      str = "WAIT_CENTROID_TABLE_COMPLEMENT";
      break;
    case ObDDLTaskStatus::GENERATE_PQ_CENTROID_TABLE_SCHEMA:
      str = "GENERATE_PQ_CENTROID_TABLE_SCHEMA";
      break;
    case ObDDLTaskStatus::WAIT_PQ_CENTROID_TABLE_COMPLEMENT:
      str = "WAIT_PQ_CENTROID_TABLE_COMPLEMENT";
      break;
    case ObDDLTaskStatus::BUILD_DATA:
      str = "BUILD_DATA";
      break;
    case ObDDLTaskStatus::WAIT_DATA_COMPLEMENT:
      str = "WAIT_DATA_COMPLEMENT";
      break;
    case ObDDLTaskStatus::FAIL:
      str = "FAIL";
      break;
    case ObDDLTaskStatus::SUCCESS:
      str = "SUCCESS";
      break;
  }
  return str;
}

static inline bool is_simple_table_long_running_ddl(const ObDDLType type)
{
  return type > DDL_INVALID && type < DDL_DROP_SCHEMA_AVOID_CONCURRENT_TRANS;
}

static inline bool is_drop_schema_block_concurrent_trans(const ObDDLType type)
{
  return type > DDL_DROP_SCHEMA_AVOID_CONCURRENT_TRANS && type < DDL_DOUBLE_TABLE_OFFLINE;
}

static inline bool is_double_table_long_running_ddl(const ObDDLType type)
{
  return type > DDL_DOUBLE_TABLE_OFFLINE && type < DDL_NORMAL_TYPE;
}

static inline bool is_long_running_ddl(const ObDDLType type)
{
  return is_simple_table_long_running_ddl(type) || is_double_table_long_running_ddl(type);
}

static inline bool is_complement_data_relying_on_dag(const ObDDLType type)
{
  return DDL_DROP_COLUMN == type
      || DDL_ADD_COLUMN_OFFLINE == type
      || DDL_COLUMN_REDEFINITION == type;
}

static inline bool is_delete_lob_meta_row_relying_on_dag(const ObDDLType type)
{
  return DDL_DROP_VEC_INDEX == type;
}

static inline bool is_invalid_ddl_type(const ObDDLType type)
{
  return DDL_INVALID == type;
}

static inline bool is_create_index(const ObDDLType type)
{
 return ObDDLType::DDL_CREATE_INDEX == type || ObDDLType::DDL_CREATE_PARTITIONED_LOCAL_INDEX == type;
}
// ddl stmt or rs ddl trans has rollbacked and can retry
static inline bool is_ddl_stmt_packet_retry_err(const int ret)
{
  return OB_EAGAIN == ret || OB_SNAPSHOT_DISCARDED == ret || OB_ERR_PARALLEL_DDL_CONFLICT == ret
      || OB_TRANS_KILLED == ret || OB_TRANS_ROLLBACKED == ret // table lock doesn't support leader switch
      || OB_PARTITION_IS_BLOCKED == ret // when LS is blocking transaction writes
      || OB_TRANS_NEED_ROLLBACK == ret // transaction killed by leader switch
      || OB_DDL_RESOURCE_NOT_ENOUGH == ret // runtime DDL resource not enough
      ;
}

static inline bool is_column_redifinition_like_ddl_type(const ObDDLType type)
{
  return ObDDLType::DDL_DROP_COLUMN == type
          || ObDDLType::DDL_ADD_COLUMN_OFFLINE == type
          || ObDDLType::DDL_COLUMN_REDEFINITION == type;
}

static inline bool is_local_build_ddl_task_status(const ObDDLTaskStatus &task_status)
{
  return ObDDLTaskStatus::REPENDING == task_status || ObDDLTaskStatus::REDEFINITION == task_status;
}

static inline ObDDLType get_create_index_type(const int64_t data_format_version, const share::schema::ObTableSchema &index_schema)
{
  return index_schema.is_storage_local_index_table() && index_schema.is_partitioned_table() ? ObDDLType::DDL_CREATE_PARTITIONED_LOCAL_INDEX : ObDDLType::DDL_CREATE_INDEX;
}

enum ObCheckExistedDDLMode
{
  INVALID_DDL_MODE          = 0,
  ALL_LONG_RUNNING_DDL      = 1,
  SIMPLE_TABLE_RUNNING_DDL  = 2,
  DOUBLE_TABLE_RUNNING_DDL  = 3
};

struct ObColumnNameInfo final
{
public:
  ObColumnNameInfo()
    : column_name_(), is_shadow_column_(false), is_enum_set_need_cast_(false)
  {}
  ObColumnNameInfo(const ObString &column_name, const bool is_shadow_column, const bool is_enum_set_need_cast = false)
    : column_name_(column_name), is_shadow_column_(is_shadow_column), is_enum_set_need_cast_(is_enum_set_need_cast)
  {}
  ~ObColumnNameInfo() = default;
  TO_STRING_KV(K_(column_name), K_(is_shadow_column), K_(is_enum_set_need_cast));
public:
  ObString column_name_;
  bool is_shadow_column_;
  bool is_enum_set_need_cast_;
};

class ObColumnNameMap final {
public:
  ObColumnNameMap() {}
  ~ObColumnNameMap() {}
  int init(const schema::ObTableSchema &orig_table_schema,
           const schema::ObTableSchema &new_table_schema,
           const schema::AlterTableSchema &alter_table_arg);
  int assign(const ObColumnNameMap &other);
  int set(const ObString &orig_column_name, const ObString &new_column_name);
  int get(const ObString &orig_column_name, ObString &new_column_name) const;
  int get_orig_column_name(const ObString &new_column_name, ObString &orig_column_name) const;
  int get_changed_names(ObIArray<std::pair<ObString, ObString>> &changed_names) const;
  DECLARE_TO_STRING;

private:
  ObArenaAllocator allocator_;
  common::hash::ObHashMap<schema::ObColumnNameHashWrapper, ObString> col_name_map_;

  DISALLOW_COPY_AND_ASSIGN(ObColumnNameMap);
};

struct ObDDLTaskStatInfo final
{
public:
  ObDDLTaskStatInfo();
  ~ObDDLTaskStatInfo() = default;
  int init(const char *&ddl_type_str, const uint64_t table_id);
  TO_STRING_KV(K_(start_time), K_(finish_time), K_(time_remaining), K_(percentage),
               K_(op_name), K_(target), K_(message));
public:
  int64_t start_time_;
  int64_t finish_time_;
  int64_t time_remaining_;
  int64_t percentage_;
  char op_name_[common::MAX_LONG_OPS_NAME_LENGTH];
  char target_[common::MAX_LONG_OPS_TARGET_LENGTH];
  char message_[common::MAX_LONG_OPS_MESSAGE_LENGTH];
};

class ObDDLUtil
{
public:
  struct ObReplicaKey final
  {
  public:
    ObReplicaKey(): partition_id_(common::OB_INVALID_ID), addr_()
    {}
    ObReplicaKey(const int64_t partition_id, common::ObAddr addr): partition_id_(partition_id), addr_(addr)
    {}
    ~ObReplicaKey() = default;
    uint64_t hash() const
    {
      uint64_t hash_val = addr_.hash();
      hash_val = murmurhash(&partition_id_, sizeof(partition_id_), hash_val);
      return hash_val;
    }
    bool operator ==(const ObReplicaKey &other) const
    {
      return partition_id_ == other.partition_id_ && addr_ == other.addr_;
    }

    TO_STRING_KV(K_(partition_id), K_(addr));
  public:
    int64_t partition_id_;
    common::ObAddr addr_;
  };

  // get all tablets of a table by table_id
  static int get_tablets(
      schema::ObMultiVersionSchemaService &schema_service,
      const int64_t table_id,
      common::ObIArray<common::ObTabletID> &tablet_ids);

  static int get_tablet_count(schema::ObMultiVersionSchemaService &schema_service,
                              const int64_t table_id,
                              int64_t &tablet_count);
  static int get_all_indexes_tablets_count(
      schema::ObSchemaGetterGuard &schema_guard,
      const uint64_t data_table_id,
      int64_t &all_tablet_count);

  static int generate_spatial_index_column_names(const share::schema::ObTableSchema &dest_table_schema,
                                                 const share::schema::ObTableSchema &source_table_schema,
                                                 ObArray<ObColumnNameInfo> &insert_column_names,
                                                 ObArray<ObColumnNameInfo> &column_names,
                                                 ObArray<int64_t> &select_column_ids);
  static int append_multivalue_extra_column(const share::schema::ObTableSchema &dest_table_schema,
                                            const share::schema::ObTableSchema &source_table_schema,
                                            ObArray<ObColumnNameInfo> &column_names,
                                            ObArray<int64_t> &select_column_ids);
  static int refresh_alter_table_arg(schema::ObMultiVersionSchemaService &schema_service,
      const int64_t orig_table_id,
      const uint64_t foreign_key_id,
      obcall::ObAlterTableArg &alter_table_arg);

  static int generate_ddl_schema_hint_str(
      const ObString &table_name,
      const int64_t schema_version,
      ObSqlString &sql_string);

  static bool is_table_lock_retry_ret_code(int ret)
  {
    return OB_TRY_LOCK_ROW_CONFLICT == ret || OB_NOT_MASTER == ret || OB_TIMEOUT == ret
           || OB_EAGAIN == ret || OB_LS_LOCATION_LEADER_NOT_EXIST == ret || OB_TRANS_CTX_NOT_EXIST == ret;
  }

  static int check_can_convert_character(const ObObjMeta &obj_meta, const bool is_domain_index, const bool is_string_lob)
  {
    return (obj_meta.is_string_type() || obj_meta.is_enum_or_set()) &&
            (is_string_lob || (CS_TYPE_BINARY != obj_meta.get_collation_type() && !is_domain_index));
  }

  static int get_index_table_batch_partition_names(
    schema::ObMultiVersionSchemaService &schema_service,
    const int64_t &data_table_id,
    const int64_t &index_table_id,
    const ObIArray<ObTabletID> &tablets,
    common::ObIAllocator &allocator,
    ObIArray<ObString> &partition_names);
  static int get_tablet_data_size(
    ObSQLiteConnectionPool &meta_db_pool,
    const common::ObTabletID &tablet_id,
    int64_t &data_size);
  static int get_tablet_data_row_cnt(
    ObSQLiteConnectionPool &meta_db_pool,
    const common::ObTabletID &tablet_id,
    int64_t &data_row_cnt);
  static int get_ls_host_left_disk_space(
    common::ObISQLClient &sql_client,
    uint64_t &left_space_size);
  static int check_table_exist(
     const uint64_t table_id,
     share::schema::ObSchemaGetterGuard &schema_guard);
  static int get_ddl_rpc_timeout(const int64_t tablet_count, int64_t &ddl_rpc_timeout_us);
  static int get_ddl_rpc_timeout_by_table(schema::ObMultiVersionSchemaService &schema_service,
                                          const int64_t table_id,
                                          int64_t &ddl_rpc_timeout_us);
  static int get_ddl_tx_timeout(const int64_t tablet_count, int64_t &ddl_tx_timeout_us);
  static void get_ddl_rpc_timeout_for_database(schema::ObMultiVersionSchemaService &schema_service,
                                               const int64_t database_id,
                                               int64_t &ddl_rpc_timeout_us);
  static int64_t get_default_ddl_rpc_timeout();

  static int get_data_information(common::ObISQLClient &sql_client,
     const uint64_t task_id,
     uint64_t &data_format_version,
     int64_t &snapshot_version,
     share::ObDDLTaskStatus &task_status);
  static int get_data_information(
      common::ObISQLClient &sql_client,
      const uint64_t task_id,
      ObDDLTaskDataInfo &data_info);

  static int generate_column_name_str(
    const common::ObIArray<ObColumnNameInfo> &column_names,
    const bool with_origin_name,
    const bool with_alias_name,
    const bool use_heap_table_ddl_plan,
    ObSqlString &column_name_str);
  static int generate_column_name_str(
      const ObColumnNameInfo &column_name_info,
      const bool with_origin_name,
      const bool with_alias_name,
      const bool with_comma,
      ObSqlString &sql_string);
  static int reshape_ddl_column_obj(
      common::ObDatum &datum,
      const ObObjMeta &obj_meta);
  static int64_t calc_inner_sql_execute_timeout()
  {
    return max(OB_MAX_DDL_BUILD_TIMEOUT, GCONF._ob_ddl_timeout);
  }

  /**
   * NOTICE: The interface is designed for Offline DDL operation only.
   * The caller can not obtain the schema via the hold_buf_src_tenant_schema_guard.
   *
   * This interface provides the schema guard for the source and destination,
   * to avoid using two different versions of the guard caused by the parallel ddl.
   *
   * @param [in] hold_buf_src_tenant_schema_guard: hold buf.
   * @param [in] hold_buf_dst_tenant_schema_guard: hold buf.
   * @param [out] src_tenant_schema_guard:
   *    pointer to the hold_buf_src_tenant_schema_guard,
   *    is always not nullptr if the interface return OB_SUCC.
   * @param [out] dst_tenant_schema_guard:
   *    pointer to the hold_buf_dst_tenant_schema_guard,
   *    is always not nullptr if the interface return OB_SUCC.
  */
  static int check_schema_version_refreshed(schema::ObMultiVersionSchemaService &schema_service,
                                            const int64_t target_schema_version);
  static bool reach_time_interval(const int64_t i, volatile int64_t &last_time);
  static int check_table_compaction_checksum_error(schema::ObMultiVersionSchemaService &schema_service,
                                                   common::ObISQLClient &sql_client,
                                                   ObSQLiteConnectionPool &meta_db_pool,
                                                   const uint64_t table_id);
  static int get_temp_store_compress_type(const ObCompressorType schema_compr_type,
                                          const int64_t parallel,
                                          ObCompressorType &compr_type);
  static int get_temp_store_compress_type(const share::schema::ObTableSchema *table_schema,
                                          const int64_t parallel,
                                          ObCompressorType &compr_type);
  static inline bool is_verifying_checksum_error_needed(share::ObDDLType type)
  {
    bool res = false;
    switch (type) {
      case DDL_MODIFY_COLUMN:
      case DDL_ADD_PRIMARY_KEY:
      case DDL_DROP_PRIMARY_KEY:
      case DDL_ALTER_PRIMARY_KEY:
      case DDL_ALTER_PARTITION_BY:
      case DDL_DROP_COLUMN:
      case DDL_CONVERT_TO_CHARACTER:
      case DDL_ADD_COLUMN_OFFLINE:
      case DDL_COLUMN_REDEFINITION:
      case DDL_TABLE_REDEFINITION:
      case DDL_CREATE_INDEX:
      case DDL_CREATE_FTS_INDEX:
      case DDL_CREATE_VEC_INDEX:
      case DDL_CREATE_PARTITIONED_LOCAL_INDEX:
      case DDL_CHECK_CONSTRAINT:
      case DDL_FOREIGN_KEY_CONSTRAINT:
      case DDL_ADD_NOT_NULL_COLUMN:
        res = true;
        break;
      default:
        res = false;
    }
    return res;
  }
  static int get_global_index_table_ids(const schema::ObTableSchema &table_schema, ObIArray<uint64_t> &global_index_table_ids, share::schema::ObSchemaGetterGuard &schema_guard);
  static bool use_idempotent_mode();
  static int get_tablet_ids(
      schema::ObMultiVersionSchemaService &schema_service,
      const int64_t table_id,
      const int64_t target_table_id,
      common::ObIArray<common::ObTabletID> &tablet_ids);
  static int get_no_logging_param(bool &is_no_logging);
  static int check_need_acquire_lob_snapshot(
      const ObTableSchema *data_table_schema,
      const ObTableSchema *index_table_schema,
      bool &need_acquire);

  static int get_table_lob_col_idx(const ObTableSchema &table_schema, ObIArray<uint64_t> &lob_col_idxs);

  static bool need_reshape(const ObObjMeta &col_type);
  static bool is_vector_index_complement(const ObIndexType index_type);
  static int64_t generate_idempotent_value(
      const int64_t slice_count,
      const int64_t slice_idx,
      const int64_t range_interval,
      const int64_t slice_row_idx);

private:
  static int check_table_column_checksum_error(common::ObISQLClient &sql_client,
                                               const int64_t table_id);

public:
  const static int64_t MAX_BATCH_COUNT = 128;
};

class ObCheckTabletDataComplementOp
{
public:

  static int check_and_wait_old_complement_task(schema::ObMultiVersionSchemaService &schema_service,
      common::ObMySQLProxy &sql_proxy,
      const uint64_t index_table_id,
      const int64_t ddl_task_id,
      const int64_t execution_id,
      const common::ObCurTraceId::TraceId &trace_id,
      const int64_t schema_version,
      const int64_t scn,
      bool &need_exec_new_inner_sql);
  static int check_finish_report_checksum(schema::ObMultiVersionSchemaService &schema_service,
      common::ObMySQLProxy &sql_proxy,
      const uint64_t index_table_id,
      const int64_t execution_id,
      const uint64_t ddl_task_id);
  static int check_tablet_checksum_update_status(common::ObMySQLProxy &sql_proxy,
      const uint64_t index_table_id,
      const uint64_t ddl_task_id,
      const int64_t execution_id,
      const ObIArray<ObTabletID> &tablet_ids,
      bool &tablet_checksum_status);

private:
  static int check_task_inner_sql_session_status(
      common::ObMySQLProxy &sql_proxy,
      const common::ObCurTraceId::TraceId &trace_id,
      const int64_t task_id,
      const int64_t scn,
      bool &is_old_task_session_exist);

};

typedef common::ObCurTraceId::TraceId DDLTraceId;
class ObDDLEventInfo final
{
public:
  explicit ObDDLEventInfo(const common::ObAddr &addr);
  ObDDLEventInfo(const common::ObAddr &addr, const int32_t sub_id);
  ~ObDDLEventInfo() = default;
  void record_in_guard();
  void init_sub_trace_id(const int32_t sub_id);
  void set_inner_sql_id(const int64_t inner_sql_id);
  const DDLTraceId &get_trace_id() const { return trace_id_; }
  const DDLTraceId &get_parent_trace_id() const { return parent_trace_id_; }
  int set_trace_id(const DDLTraceId &trace_id) { return trace_id_.set(trace_id.get()); }
  void reset();
  TO_STRING_KV(K(addr_), K(event_ts_), K(sub_id_), K(trace_id_), K(parent_trace_id_));

public:
  ObAddr addr_;
  int32_t sub_id_;
  int64_t event_ts_;
  DDLTraceId parent_trace_id_;
  DDLTraceId trace_id_;
};


}  // end namespace share
}  // end namespace oceanbase

#endif  // OCEANBASE_SHARE_OB_DDL_COMMON_H
