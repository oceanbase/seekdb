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

#define USING_LOG_PREFIX SHARE

#include "ob_rpc_struct.h"
#include "share/rc/ob_module_provider.h"
#include "share/ob_server_struct.h"

namespace oceanbase
{
using namespace common;
using namespace sql;
using namespace share::schema;
using namespace share;
using namespace storage;
using namespace transaction;
using namespace transaction::tablelock;
namespace obcall
{
OB_SERIALIZE_MEMBER(Bool, v_);
OB_SERIALIZE_MEMBER(Int64, v_);
OB_SERIALIZE_MEMBER(UInt64, v_);

DEF_TO_STRING(ObPartitionId)
{
  int64_t pos = 0;
  J_KV(KT_(table_id),
       K_(partition_id));
  return pos;
}

OB_SERIALIZE_MEMBER(ObPartitionId,
                    table_id_,
                    partition_id_);

bool ObStartRedefTableArg::is_valid() const
{
  return (OB_INVALID_ID != orig_table_id_
          && OB_INVALID_ID != target_table_id_
          && share::DDL_INVALID != ddl_type_);
}
OB_DEF_SERIALIZE(ObStartRedefTableArg)
{
  int ret = OB_SUCCESS;
  if (!is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  } else {
    LST_DO_CODE(OB_UNIS_ENCODE,

          orig_table_id_,

          target_table_id_,
          session_id_,
          parallelism_,
          ddl_type_,
          ddl_stmt_str_,
          trace_id_,
          sql_mode_,
          tz_info_wrap_);
    if (OB_SUCC(ret)) {
      LST_DO_CODE(OB_UNIS_ENCODE, foreign_key_checks_);
    }
  }
  return ret;
}

OB_DEF_DESERIALIZE(ObStartRedefTableArg)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE,

          orig_table_id_,

          target_table_id_,
          session_id_,
          parallelism_,
          ddl_type_,
          ddl_stmt_str_,
          trace_id_,
          sql_mode_,
          tz_info_wrap_);
  if (OB_SUCC(ret)) {
    LST_DO_CODE(OB_UNIS_DECODE, foreign_key_checks_);
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObStartRedefTableArg)
{
  int64_t len = 0;
  int ret = OB_SUCCESS;
  if (!is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  } else {
    LST_DO_CODE(OB_UNIS_ADD_LEN,

          orig_table_id_,

          target_table_id_,
          session_id_,
          parallelism_,
          ddl_type_,
          ddl_stmt_str_,
          trace_id_,
          sql_mode_,
          tz_info_wrap_);
  }
  if (OB_SUCC(ret)) {
    LST_DO_CODE(OB_UNIS_ADD_LEN, foreign_key_checks_);
  }
  if (OB_FAIL(ret)) {
    len = -1;
  }
  return len;
}

bool ObCopyTableDependentsArg::is_valid() const
{
  return 0 != task_id_;
}


bool ObFinishRedefTableArg::is_valid() const
{
  return (0 != task_id_);
}


bool ObAbortRedefTableArg::is_valid() const
{
  return 0 != task_id_;
}


bool ObUpdateDDLTaskActiveTimeArg::is_valid() const
{
  return 0 != task_id_;
}








OB_SERIALIZE_MEMBER(ObStartRedefTableRes,
                    task_id_,

                    schema_version_);

OB_SERIALIZE_MEMBER(ObCopyTableDependentsArg,
                    task_id_,

                    copy_indexes_,
                    copy_triggers_,
                    copy_constraints_,
                    copy_foreign_keys_,
                    ignore_errors_);

OB_SERIALIZE_MEMBER(ObFinishRedefTableArg,
                    task_id_);

OB_SERIALIZE_MEMBER(ObAbortRedefTableArg,
                    task_id_);

OB_SERIALIZE_MEMBER(ObUpdateDDLTaskActiveTimeArg,
                    task_id_);







//////////////////////////////////////////////
//
//  Runtime schema
//
//////////////////////////////////////////////


int ObLoadRuntimeTableSchemaArg::init(const uint64_t table_id,
    const ObIArray<share::ObLoadInnerTableSchemaInfo> *schema_infos,
    const ObIArray<int64_t> &insert_idx, const uint64_t data_version)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(insert_idx_.assign(insert_idx))) {
    LOG_WARN("failed to assign insert_idx_", KR(ret), K(insert_idx));
  } else {

    table_id_ = table_id;
    data_version_ = data_version;
    schema_infos_ = reinterpret_cast<uint64_t>(schema_infos);
  }
  return ret;
}

int ObLoadRuntimeTableSchemaArg::assign(const ObLoadRuntimeTableSchemaArg &arg)
{
  int ret = OB_SUCCESS;
  if (this == &arg) {
  } else if (OB_FAIL(insert_idx_.assign(arg.insert_idx_))) {
    LOG_WARN("failed to assign insert_idx_", KR(ret), K(arg.insert_idx_));
  } else {

    table_id_ = arg.table_id_;
    data_version_ = arg.data_version_;
    schema_infos_ = arg.schema_infos_;
  }
  return ret;
}

bool ObLoadRuntimeTableSchemaArg::is_valid() const
{
  bool valid = true;
  if (table_id_ > OB_MAX_INNER_TABLE_ID) {
    valid = false;
  } else if (insert_idx_.count() <= 0) {
    valid = false;
  } else if (data_version_ != DATA_CURRENT_VERSION) {
    valid = false;
  }
  return valid;
}

OB_SERIALIZE_MEMBER(ObLoadRuntimeTableSchemaArg, table_id_, data_version_, schema_infos_, insert_idx_);

bool ObModifySysVarArg::is_valid() const
{
  return !sys_var_list_.empty();
}


OB_SERIALIZE_MEMBER((ObModifySysVarArg, ObDDLArg), sys_var_list_, is_inner_);

DEF_TO_STRING(ObModifySysVarArg)
{
  int64_t pos = 0;
  J_KV(K_(sys_var_list), K_(is_inner));
  return pos;
}

bool ObCreateDatabaseArg::is_valid() const
{
  return !database_schema_.get_database_name_str().empty();
}

DEF_TO_STRING(ObCreateDatabaseArg)
{
  int64_t pos = 0;
  J_KV(K_(database_schema),
       K_(if_not_exist));
  return pos;
}

OB_SERIALIZE_MEMBER((ObCreateDatabaseArg, ObDDLArg),
                    database_schema_,
                    if_not_exist_);

bool ObAlterDatabaseArg::is_valid() const
{
  return !database_schema_.get_database_name_str().empty();
}

DEF_TO_STRING(ObAlterDatabaseArg)
{
  int64_t pos = 0;
  J_KV(K_(database_schema));
  return pos;
}

OB_SERIALIZE_MEMBER((ObAlterDatabaseArg, ObDDLArg),
                    database_schema_,
                    alter_option_bitset_);

bool ObDropDatabaseArg::is_valid() const
{
  return !database_name_.empty();
}

DEF_TO_STRING(ObDropDatabaseArg)
{
  int64_t pos = 0;
  J_KV(
       K_(database_name),
       K_(if_exist),
       K_(to_recyclebin),
       K_(is_add_to_scheduler));
  return pos;
}

OB_SERIALIZE_MEMBER((ObDropDatabaseArg, ObDDLArg),

                    database_name_,
                    if_exist_,
                    to_recyclebin_,
                    is_add_to_scheduler_);

bool ObCreateTableArg::is_valid() const
{
  // index_arg_list can be empty
  return !schema_.get_table_name_str().empty();
}

int ObCreateTableArg::assign(const ObCreateTableArg &other)
{
  int ret = OB_SUCCESS;
  OZ(ObDDLArg::assign(other));
  OX(if_not_exist_ = other.if_not_exist_);
  OZ(schema_.assign(other.schema_));
  OZ(index_arg_list_.assign(other.index_arg_list_));
  OZ(foreign_key_arg_list_.assign(other.foreign_key_arg_list_));
  OZ(constraint_list_.assign(other.constraint_list_));
  OX(db_name_ = other.db_name_);
  OX(last_replay_log_id_ = other.last_replay_log_id_);
  OX(is_inner_ = other.is_inner_);
  OZ(error_info_.assign(other.error_info_));
  OX(is_alter_view_ = other.is_alter_view_);
  OZ(dep_infos_.assign(other.dep_infos_));

  return ret;
}

DEF_TO_STRING(ObCreateTableArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(if_not_exist),
       K_(schema),
       K_(index_arg_list),
       K_(constraint_list),
       K_(db_name),
       K_(last_replay_log_id),
       K_(foreign_key_arg_list),
       K_(is_inner),
       K_(error_info),
       K_(is_alter_view),
       K_(dep_infos));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER((ObCreateTableArg, ObDDLArg),
                    if_not_exist_,
                    schema_,
                    index_arg_list_,
                    db_name_,
                    foreign_key_arg_list_,
                    constraint_list_,
                    last_replay_log_id_,
                    is_inner_,
                    error_info_,
                    is_alter_view_,
                    dep_infos_);

OB_SERIALIZE_MEMBER(ObCreateTableRes, table_id_, schema_version_, task_id_, do_nothing_);

OB_SERIALIZE_MEMBER(ObDropTableRes, schema_version_, task_id_, do_nothing_);

bool ObCreateTableLikeArg::is_valid() const
{
  return !origin_db_name_.empty()
      && !origin_table_name_.empty() && !new_db_name_.empty()
      && !new_table_name_.empty()
      && (table_type_ == USER_TABLE || table_type_ == TMP_TABLE);
}


DEF_TO_STRING(ObCreateTableLikeArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(if_not_exist),

       K_(origin_db_name),
       K_(origin_table_name),
       K_(new_db_name),
       K_(new_table_name),
       K_(table_type),
       K_(session_id),
       K_(define_user_id));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER((ObCreateTableLikeArg, ObDDLArg),
                    if_not_exist_,

                    origin_db_name_,
                    origin_table_name_,
                    new_db_name_,
                    new_table_name_,
                    table_type_,
                    session_id_,
                    define_user_id_);



bool ObSetCommentArg::is_valid() const
{
  return !database_name_.empty()
         && !table_name_.empty()
         && op_type_ > MIN_OP_TYPE
         && op_type_ < MAX_OP_TYPE;
}

OB_SERIALIZE_MEMBER((ObSetCommentArg, ObDDLArg),
                     session_id_,
                     database_name_,
                     table_name_,
                     column_name_list_,
                     column_comment_list_,
                     table_comment_,
                     op_type_);

bool ObAlterTableArg::is_valid() const
{
  // TODO(shaohang.lsh): add more check if needed
  return !alter_table_schema_.origin_database_name_.empty()
      && !alter_table_schema_.origin_table_name_.empty();
}

bool ObAlterTableArg::is_allow_when_disable_ddl() const
{
  return false;
}

int ObAlterTableArg::is_alter_comment(bool &is_alter_comment) const
{
  int ret = OB_SUCCESS;
  is_alter_comment = alter_table_schema_.alter_option_bitset_.has_member(COMMENT);
  if (!is_alter_comment && is_alter_columns_) {
    ObTableSchema::const_column_iterator it_begin = alter_table_schema_.column_begin();
    ObTableSchema::const_column_iterator it_end = alter_table_schema_.column_end();
    AlterColumnSchema *alter_column_schema = NULL;
    for (; OB_SUCC(ret) && !is_alter_comment && it_begin != it_end; it_begin++) {
      if (OB_ISNULL(alter_column_schema = static_cast<AlterColumnSchema *>(*it_begin))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("alter_column_schema is NULL", K(ret));
      } else {
        is_alter_comment |= alter_column_schema->is_set_comment_;
      }
    }
  }
  return ret;
}

int ObAlterTableArg::serialize_index_args(char *buf, const int64_t data_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  if (!is_valid() || NULL == buf || data_len <= 0 || pos >= data_len) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), "self", *this, KP(buf), K(data_len), K(pos));
  } else if (OB_FAIL(serialization::encode_vi64(buf, data_len, pos, index_arg_list_.size()))) {
    SHARE_LOG(WARN, "Fail to serialize index arg count", K(ret));
  }
  for (int i = 0; OB_SUCC(ret) && i < index_arg_list_.size(); ++i) {
    ObIndexArg *index_arg = index_arg_list_.at(i);
    if (index_arg->index_action_type_ == ObIndexArg::ALTER_PRIMARY_KEY
      || index_arg->index_action_type_ == ObIndexArg::DROP_PRIMARY_KEY) {
      ObAlterPrimaryArg *alter_pk_arg = static_cast<ObAlterPrimaryArg *>(index_arg);
      if (NULL == alter_pk_arg) {
        ret = OB_INVALID_ARGUMENT;
        SHARE_LOG(WARN, "index arg is null", K(ret));
      } else if (OB_FAIL(serialization::encode_vi32(buf, data_len, pos,
          alter_pk_arg->index_action_type_))) {
        SHARE_LOG(WARN, "failed to serialize index type", K(ret));
      } else if (OB_FAIL(alter_pk_arg->serialize(buf, data_len, pos))) {
        SHARE_LOG(WARN, "failed to serialize create index arg!", K(data_len), K(pos), K(ret));
      }
    } else if (index_arg->index_action_type_ == ObIndexArg::ADD_INDEX
              || index_arg->index_action_type_ == ObIndexArg::ADD_PRIMARY_KEY) {
      ObCreateIndexArg *create_index_arg = static_cast<ObCreateIndexArg *>(index_arg);
      if (NULL == create_index_arg) {
        ret = OB_INVALID_ARGUMENT;
        SHARE_LOG(WARN, "index arg is null", K(ret));
      } else if (OB_FAIL(serialization::encode_vi32(buf, data_len, pos,
          create_index_arg->index_action_type_))) {
        SHARE_LOG(WARN, "failed to serialize index type", K(ret));
      } else if (OB_FAIL(create_index_arg->serialize(buf, data_len, pos))) {
        SHARE_LOG(WARN, "failed to serialize create index arg!", K(data_len), K(pos), K(ret));
      }
    } else if (index_arg->index_action_type_ == ObIndexArg::DROP_INDEX) {
      ObDropIndexArg *drop_index_arg = static_cast<ObDropIndexArg *>(index_arg);
      if (NULL == drop_index_arg) {
        ret = OB_INVALID_ARGUMENT;
        SHARE_LOG(WARN, "index arg is null", K(ret));
      } else if (OB_FAIL(serialization::encode_vi32(buf, data_len, pos,
                                                    drop_index_arg->index_action_type_))) {
        SHARE_LOG(WARN, "failed to serialize index type", K(ret));
      } else if (OB_FAIL(drop_index_arg->serialize(buf, data_len, pos))) {
        SHARE_LOG(WARN, "failed to serialize drop index arg!", K(data_len), K(pos), K(ret));
      }
    } else if (index_arg->index_action_type_ == ObIndexArg::ALTER_INDEX) {
      ObAlterIndexArg *alter_index_arg = static_cast<ObAlterIndexArg *>(index_arg);
      if (OB_UNLIKELY(NULL == alter_index_arg)) {
        ret = OB_INVALID_ARGUMENT;
        SHARE_LOG(WARN, "index arg is null", K(ret));
      } else if (OB_FAIL(serialization::encode_vi32(buf, data_len, pos,
                                                    alter_index_arg->index_action_type_))) {
        SHARE_LOG(WARN, "failed to serialize index type", K(ret));
      } else if (OB_FAIL(alter_index_arg->serialize(buf, data_len, pos))) {
        SHARE_LOG(WARN, "failed to serialize alter index arg!", K(data_len), K(pos), K(ret));
      }
    } else if (index_arg->index_action_type_ == ObIndexArg::ALTER_INDEX_PARALLEL) {
      ObAlterIndexParallelArg *alter_index_parallel_arg = static_cast<ObAlterIndexParallelArg *>(index_arg);
      if (OB_UNLIKELY(NULL == alter_index_parallel_arg)) {
        ret = OB_INVALID_ARGUMENT;
        SHARE_LOG(WARN, "index arg is null", K(ret));
      } else if (OB_FAIL(serialization::encode_vi32(buf, data_len, pos,
                                                    alter_index_parallel_arg->index_action_type_))) {
        SHARE_LOG(WARN, "failed to serialize index type", K(ret));
      } else if (OB_FAIL(alter_index_parallel_arg->serialize(buf, data_len, pos))) {
        SHARE_LOG(WARN, "failed to serialize alter index parallel arg!",
          K(data_len), K(pos), K(ret));
      }
    } else if (index_arg->index_action_type_ == ObIndexArg::RENAME_INDEX) {
      ObRenameIndexArg *rename_index_arg = static_cast<ObRenameIndexArg *>(index_arg);
      SHARE_LOG(WARN, "serialize rename index arg!", K(rename_index_arg->origin_index_name_), K(rename_index_arg->new_index_name_));

      if (OB_UNLIKELY(NULL == rename_index_arg)) {
        ret = OB_INVALID_ARGUMENT;
        SHARE_LOG(WARN, "index arg is null", K(ret));
      } else if (OB_FAIL(serialization::encode_vi32(buf, data_len, pos,
                                                    rename_index_arg->index_action_type_))) {
        SHARE_LOG(WARN, "failed to serialize index type", K(ret));
      } else if (OB_FAIL(rename_index_arg->serialize(buf, data_len, pos))) {
        SHARE_LOG(WARN, "failed to serialize alter index arg!", K(data_len), K(pos), K(ret));
      }
    } else if (index_arg->index_action_type_ == ObIndexArg::DROP_FOREIGN_KEY) {
      ObDropForeignKeyArg *foreign_key_arg = static_cast<ObDropForeignKeyArg *>(index_arg);
      if (OB_UNLIKELY(NULL == foreign_key_arg)) {
        ret = OB_INVALID_ARGUMENT;
        SHARE_LOG(WARN, "index arg is null", K(ret));
      } else if (OB_FAIL(serialization::encode_vi32(buf, data_len, pos,
                                                    foreign_key_arg->index_action_type_))) {
        SHARE_LOG(WARN, "failed to serialize index type", K(ret));
      } else if (OB_FAIL(foreign_key_arg->serialize(buf, data_len, pos))) {
        SHARE_LOG(WARN, "failed to serialize drop foreign key arg!", K(data_len), K(pos), K(ret));
      }
    } else {
      ret = OB_INVALID_ARGUMENT;
      SHARE_LOG(WARN, "unknown index action type", K_(index_arg->index_action_type), K(ret));
    }
  }
  return ret;
}

int ObAlterTableArg::alloc_index_arg(const ObIndexArg::IndexActionType index_action_type, ObIndexArg *&index_arg)
{
  int ret = OB_SUCCESS;
  void *tmp_ptr = nullptr;
  if (index_action_type == ObIndexArg::ALTER_PRIMARY_KEY
    || index_action_type == ObIndexArg::DROP_PRIMARY_KEY) {
    ObAlterPrimaryArg *alter_pk_arg = NULL;
    if (NULL == (tmp_ptr = allocator_.alloc(sizeof(ObAlterPrimaryArg)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      SHARE_LOG(ERROR, "failed to alloc memory!", K(ret));
    } else {
      index_arg = new (tmp_ptr) ObAlterPrimaryArg();
    }
  } else if (index_action_type == ObIndexArg::ADD_INDEX
            || index_action_type == ObIndexArg::ADD_PRIMARY_KEY) {
    ObCreateIndexArg *create_index_arg = NULL;
    if (NULL == (tmp_ptr = allocator_.alloc(sizeof(ObCreateIndexArg)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      SHARE_LOG(ERROR, "failed to alloc memory!", K(ret));
    } else {
      index_arg = new (tmp_ptr) ObCreateIndexArg();
    }
  } else if (index_action_type == ObIndexArg::DROP_INDEX) {
    ObDropIndexArg *drop_index_arg = NULL;
    if (NULL == (tmp_ptr = (ObDropIndexArg *)allocator_.alloc(sizeof(ObDropIndexArg)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      SHARE_LOG(ERROR, "failed to allocate memory!", K(ret));
    } else {
      index_arg = new (tmp_ptr) ObDropIndexArg();
    }
  } else if (index_action_type == ObIndexArg::ALTER_INDEX) {
    ObAlterIndexArg *alter_index_arg = NULL;
    if (OB_UNLIKELY(NULL == (tmp_ptr = (ObAlterIndexArg *)allocator_.alloc(sizeof(ObAlterIndexArg))))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      SHARE_LOG(ERROR, "failed to allocate memory!", K(ret));
    } else {
      index_arg = new (tmp_ptr) ObAlterIndexArg();
    }
  } else if (index_action_type == ObIndexArg::ALTER_INDEX_PARALLEL) {
    ObAlterIndexParallelArg *alter_index_parallel_arg = NULL;
    if (OB_UNLIKELY(NULL == (tmp_ptr = (ObAlterIndexParallelArg *)allocator_.alloc(sizeof(ObAlterIndexParallelArg))))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      SHARE_LOG(ERROR, "failed to allocate memory!", K(ret));
    } else {
      index_arg = new (tmp_ptr) ObAlterIndexParallelArg();
    }
  } else if (index_action_type == ObIndexArg::RENAME_INDEX) {
    ObRenameIndexArg *rename_index_arg = NULL;
    if (OB_UNLIKELY(NULL == (tmp_ptr = (ObRenameIndexArg *)allocator_.alloc(sizeof(ObRenameIndexArg))))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      SHARE_LOG(ERROR, "failed to allocate memory!", K(ret));
    } else {
      index_arg = new (tmp_ptr) ObRenameIndexArg();
    }
  } else if (index_action_type == ObIndexArg::DROP_FOREIGN_KEY) {
    ObDropForeignKeyArg *drop_foreign_key_arg = NULL;
    if (OB_UNLIKELY(NULL == (tmp_ptr = (ObDropForeignKeyArg *)allocator_.alloc(sizeof(ObDropForeignKeyArg))))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      SHARE_LOG(ERROR, "failed to allocate memory!", K(ret));
    } else {
      index_arg = new (tmp_ptr) ObDropForeignKeyArg();
    }
  } else {
    ret = OB_INVALID_ARGUMENT;
    SHARE_LOG(WARN, "unknown index action type", K(index_action_type), K(ret));
  }
  return ret;
}

int ObAlterTableArg::deserialize_index_args(const char *buf, const int64_t data_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  int64_t count = 0;
  if (OB_ISNULL(buf) || OB_UNLIKELY(data_len <= 0) || OB_UNLIKELY(pos > data_len)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("buf should not be null", K(buf), K(data_len), K(pos), K(ret));
  } else if (pos == data_len) {
    //do nothing
  } else if (OB_FAIL(serialization::decode_vi64(buf, data_len, pos, &count))) {
    SHARE_LOG(WARN, "Fail to decode column count", K(ret));
  }
  for (int i = 0; OB_SUCC(ret) && i < count; ++i) {
    ObIndexArg::IndexActionType index_action_type = ObIndexArg::INVALID_ACTION;
    ObIndexArg *index_arg = nullptr;
    if (OB_FAIL(serialization::decode_vi32(buf, data_len, pos, ((int32_t *)(&index_action_type))))) {
      SHARE_LOG(WARN, "failed to decode index action type", K(ret));
      break;
    } else if (OB_FAIL(alloc_index_arg(index_action_type, index_arg))) {
      SHARE_LOG(WARN, "alloc index arg failed", K(ret));
    } else if (OB_ISNULL(index_arg)) {
      ret = OB_ERR_UNEXPECTED;
      SHARE_LOG(WARN, "error unexpected, index arg must not be nullptr", K(ret));
    } else if (OB_FAIL(index_arg->deserialize(buf, data_len, pos))) {
      SHARE_LOG(WARN, "deserialize index arg failed", K(ret));
    } else if (OB_FAIL(index_arg_list_.push_back(index_arg))) {
      SHARE_LOG(WARN, "push back index arg failed", K(ret));
    }
    if (OB_FAIL(ret) && nullptr != index_arg) {
      index_arg->~ObIndexArg();
      allocator_.free(index_arg);
      index_arg = nullptr;
    }
  }
  return ret;
}

int64_t ObAlterTableArg::get_index_args_serialize_size() const
{
  int ret = OB_SUCCESS;
  int64_t len = 0;
  if (!is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), "self", *this);
  } else {
    len += serialization::encoded_length_vi64(index_arg_list_.size());
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < index_arg_list_.size(); ++i) {
    ObIndexArg *index_arg = index_arg_list_.at(i);
    if (NULL == index_arg) {
      ret = OB_INVALID_ARGUMENT;
      SHARE_LOG(WARN, "index arg should not be null", K(ret));
    } else {
      len += serialization::encoded_length(index_arg->index_action_type_);
      if (ObIndexArg::DROP_PRIMARY_KEY == index_arg->index_action_type_
        || ObIndexArg::ALTER_PRIMARY_KEY == index_arg->index_action_type_) {
        ObAlterPrimaryArg *alter_pk_arg = static_cast<ObAlterPrimaryArg *>(index_arg);
        if (NULL == alter_pk_arg) {
          ret = OB_INVALID_ARGUMENT;
          SHARE_LOG(WARN, "index arg is null", K(ret));
        } else {
          len += alter_pk_arg->get_serialize_size();
        }
      } else if (ObIndexArg::ADD_INDEX == index_arg->index_action_type_
                || ObIndexArg::ADD_PRIMARY_KEY == index_arg->index_action_type_) {
        ObCreateIndexArg *create_index_arg = static_cast<ObCreateIndexArg *>(index_arg);
        if (NULL == create_index_arg) {
          ret = OB_INVALID_ARGUMENT;
          SHARE_LOG(WARN, "index arg is null", K(ret));
        } else {
          len += create_index_arg->get_serialize_size();
        }
      } else if (ObIndexArg::DROP_INDEX == index_arg->index_action_type_) {
        ObDropIndexArg *drop_index_arg = static_cast<ObDropIndexArg *>(index_arg);
        if (NULL == drop_index_arg) {
          ret = OB_INVALID_ARGUMENT;
          SHARE_LOG(WARN, "index arg is null", K(ret));
        } else {
          len += drop_index_arg->get_serialize_size();
        }
      } else if (ObIndexArg::ALTER_INDEX == index_arg->index_action_type_) {
        ObAlterIndexArg *alter_index_arg = static_cast<ObAlterIndexArg *>(index_arg);
        if (OB_UNLIKELY(NULL == alter_index_arg)) {
          ret = OB_INVALID_ARGUMENT;
          SHARE_LOG(WARN, "index arg is null", K(ret));
        } else {
          len += alter_index_arg->get_serialize_size();
        }
      } else if (ObIndexArg::DROP_FOREIGN_KEY == index_arg->index_action_type_) {
        ObDropForeignKeyArg *drop_foreign_key_arg = static_cast<ObDropForeignKeyArg *>(index_arg);
        if (OB_UNLIKELY(NULL == drop_foreign_key_arg)) {
          ret = OB_INVALID_ARGUMENT;
          SHARE_LOG(WARN, "foreign key arg is null", K(ret));
        } else {
          len += drop_foreign_key_arg->get_serialize_size();
        }
      } else if (ObIndexArg::ALTER_INDEX_PARALLEL == index_arg->index_action_type_) {
        ObAlterIndexParallelArg *alter_index_parallel_arg =
          static_cast<ObAlterIndexParallelArg *>(index_arg);
        if (OB_UNLIKELY(NULL == alter_index_parallel_arg)) {
          ret = OB_INVALID_ARGUMENT;
          SHARE_LOG(WARN, "index arg is null", K(ret));
        } else {
          len += alter_index_parallel_arg->get_serialize_size();
        }
      } else if (ObIndexArg::RENAME_INDEX == index_arg->index_action_type_) {
        ObRenameIndexArg *rename_index_arg = static_cast<ObRenameIndexArg *>(index_arg);
        if (OB_UNLIKELY(NULL == rename_index_arg)) {
          ret = OB_INVALID_ARGUMENT;
          SHARE_LOG(WARN, "index arg is null", K(ret));
        } else {
          len += rename_index_arg->get_serialize_size();
        }
      } else {
        ret = OB_INVALID_ARGUMENT;
        SHARE_LOG(WARN, "Invalid index action type!", K(ret));
      }
    }
  }
  if (OB_FAIL(ret)) {
    len = -1;
  }
  return len;
}

OB_DEF_SERIALIZE(ObAlterTableArg)
{
  int ret = OB_SUCCESS;
  if (!is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), "self", *this);
  } else if (OB_FAIL(ObDDLArg::serialize(buf, buf_len, pos))) {
    SHARE_SCHEMA_LOG(WARN, "fail to serialize DDLArg", K(buf_len), K(pos), K(ret));
  } else if (OB_FAIL(serialize_index_args(buf, buf_len, pos))) {
    SHARE_SCHEMA_LOG(WARN, "fail to serialize index args", K(buf_len), K(pos), K(ret));
  } else if (OB_FAIL(alter_table_schema_.serialize(buf, buf_len, pos))) {
    SHARE_SCHEMA_LOG(WARN, "fail to serialize alter table schema", K(ret));
  } else if (OB_FAIL(serialization::encode_vi32(buf, buf_len, pos, alter_part_type_))) {
    SHARE_SCHEMA_LOG(WARN, "fail to serialize alter_part_type", K(ret));
  } else if (OB_FAIL(serialization::encode_vi32(buf, buf_len, pos, alter_constraint_type_))) {
    SHARE_SCHEMA_LOG(WARN, "fail to serialize alter_constraint_type", K(ret));
  } else if (OB_FAIL(serialization::encode_vi64(buf, buf_len, pos, session_id_))) {
    SHARE_SCHEMA_LOG(WARN, "fail to serialize session_id", K(ret));
  } else if (OB_FAIL(tz_info_wrap_.serialize(buf, buf_len, pos))) {
    SHARE_SCHEMA_LOG(WARN, "fail to serialize timezone info wrap", K(ret));
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(foreign_key_arg_list_.serialize(buf, buf_len, pos))) {
      SHARE_SCHEMA_LOG(WARN, "fail to serialize foreign_key_arg_list_", K(ret));
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(serialization::encode_i64(buf, buf_len, pos, sql_mode_))) {
      SHARE_SCHEMA_LOG(WARN, "fail to serialize sql mode", K(ret));
    }
  }
  LST_DO_CODE(OB_UNIS_ENCODE,
              ddl_task_type_,
              table_id_,
              hidden_table_id_,
              is_alter_columns_,
              is_alter_indexs_,
              is_alter_options_,
              is_alter_partitions_,
              is_inner_,
              is_update_global_indexes_,
              is_convert_to_character_,
              skip_sys_table_check_,
              need_rebuild_trigger_,
              foreign_key_checks_,
              is_add_to_scheduler_,
              local_session_var_,
              alter_algorithm_,
              rebuild_index_arg_list_,
              lock_session_id_,
              lock_session_create_ts_,
              lock_priority_);

  LST_DO_CODE(OB_UNIS_ENCODE,
              data_version_);

  return ret;
}

OB_DEF_DESERIALIZE(ObAlterTableArg)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObDDLArg::deserialize(buf, data_len, pos))) {
    SHARE_SCHEMA_LOG(WARN, "fail to deserialize DDLArg", K(data_len), K(pos), K(ret));
  } else if (OB_FAIL(deserialize_index_args(buf, data_len, pos))) {
    SHARE_SCHEMA_LOG(WARN, "fail to deserialize index args, ", K(ret));
  } else if (OB_FAIL(alter_table_schema_.deserialize(buf, data_len, pos))) {
    SHARE_SCHEMA_LOG(WARN, "fail to deserialize alter table schema, ", K(ret));
  } else if (OB_FAIL(serialization::decode_vi32(buf, data_len, pos, ((int32_t *)(&alter_part_type_))))) {
    SHARE_SCHEMA_LOG(WARN, "fail to deserialize alter_part_type_, ", K(ret));
  } else if (OB_FAIL(serialization::decode_vi32(buf, data_len, pos, ((int32_t *)(&alter_constraint_type_))))) {
    SHARE_SCHEMA_LOG(WARN, "fail to deserialize alter_constraint_type_, ", K(ret));
  } else if (OB_FAIL(serialization::decode_vi64(buf, data_len, pos, ((int64_t *)(&session_id_))))) {
    SHARE_SCHEMA_LOG(WARN, "fail to deserialize session_id_, ", K(ret));
  } else if (OB_FAIL(tz_info_wrap_.deserialize(buf, data_len, pos))) {
    SHARE_SCHEMA_LOG(WARN, "fail to deserialize timezone info", K(ret));
  }

  if (OB_SUCC(ret) && pos < data_len) {
    if (OB_FAIL(foreign_key_arg_list_.deserialize(buf, data_len, pos))) {
      SHARE_SCHEMA_LOG(WARN, "fail to deserialize foreign_key_arg_list_", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(serialization::decode_i64(buf, data_len, pos, reinterpret_cast<int64_t *>(&sql_mode_)))) {
      SHARE_SCHEMA_LOG(WARN, "fail to decode sql mode", K(ret));
    }
  }
  LST_DO_CODE(OB_UNIS_DECODE,
              ddl_task_type_,
              table_id_,
              hidden_table_id_,
              is_alter_columns_,
              is_alter_indexs_,
              is_alter_options_,
              is_alter_partitions_,
              is_inner_,
              is_update_global_indexes_,
              is_convert_to_character_,
              skip_sys_table_check_,
              need_rebuild_trigger_,
              foreign_key_checks_,
              is_add_to_scheduler_,
              local_session_var_,
              alter_algorithm_,
              rebuild_index_arg_list_,
              lock_session_id_,
              lock_session_create_ts_,
              lock_priority_);

  LST_DO_CODE(OB_UNIS_DECODE,
              data_version_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObAlterTableArg)
{
  int64_t len = 0;
  int ret = OB_SUCCESS;
  if (!is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), "self", *this);
  } else  {
    len += ObDDLArg::get_serialize_size();
    len += get_index_args_serialize_size();
    len += alter_table_schema_.get_serialize_size();
    len += serialization::encoded_length_vi32(alter_part_type_);
    len += serialization::encoded_length_vi32(alter_constraint_type_);
    len += serialization::encoded_length_vi64(session_id_);
    len += tz_info_wrap_.get_serialize_size();
    len += foreign_key_arg_list_.get_serialize_size();
    len += serialization::encoded_length_i64(sql_mode_);
    LST_DO_CODE(OB_UNIS_ADD_LEN,
                ddl_task_type_,
                table_id_,
                hidden_table_id_,
                is_alter_columns_,
                is_alter_indexs_,
                is_alter_options_,
                is_alter_partitions_,
                is_inner_,
                is_update_global_indexes_,
                is_convert_to_character_,
                skip_sys_table_check_,
                need_rebuild_trigger_,
                foreign_key_checks_,
                is_add_to_scheduler_,
                local_session_var_,
                alter_algorithm_,
                rebuild_index_arg_list_,
                lock_session_id_,
                lock_session_create_ts_,
                lock_priority_,
                data_version_);
  }

  if (OB_FAIL(ret)) {
    len = -1;
  }
  return len;
}

bool ObExchangePartitionArg::is_valid() const
{
  return OB_INVALID_ID != session_id_ && PARTITION_LEVEL_ZERO != exchange_partition_level_ && PARTITION_LEVEL_MAX != exchange_partition_level_ && OB_INVALID_ID != base_table_id_
         && !base_table_part_name_.empty() && OB_INVALID_ID != inc_table_id_;
}

int ObExchangePartitionArg::assign(const ObExchangePartitionArg &other)
{
  int ret = OB_SUCCESS;
  if (this == &other) {
    //do nothing
  } else if (OB_FAIL(ObDDLArg::assign(other))) {
    LOG_WARN("assign failed", K(ret));
  } else {
    session_id_ = other.session_id_;

    exchange_partition_level_ = other.exchange_partition_level_;
    base_table_id_ = other.base_table_id_;
    base_table_part_name_ = other.base_table_part_name_;
    inc_table_id_ = other.inc_table_id_;
    including_indexes_ = other.including_indexes_;
    without_validation_ = other.without_validation_;
    update_global_indexes_ = other.update_global_indexes_;
  }
  return ret;
}

OB_DEF_SERIALIZE(ObExchangePartitionArg)
{
  int ret = OB_SUCCESS;
  BASE_SER((, ObDDLArg));
  LST_DO_CODE(OB_UNIS_ENCODE,
              session_id_,

              exchange_partition_level_,
              base_table_id_,
              base_table_part_name_,
              inc_table_id_,
              including_indexes_,
              without_validation_,
              update_global_indexes_);
  return ret;
}

OB_DEF_DESERIALIZE(ObExchangePartitionArg)
{
  int ret = OB_SUCCESS;
  BASE_DESER((, ObDDLArg));
  LST_DO_CODE(OB_UNIS_DECODE,
              session_id_,

              exchange_partition_level_,
              base_table_id_,
              base_table_part_name_,
              inc_table_id_,
              including_indexes_,
              without_validation_,
              update_global_indexes_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObExchangePartitionArg)
{
  int64_t len = ObDDLArg::get_serialize_size();
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              session_id_,

              exchange_partition_level_,
              base_table_id_,
              base_table_part_name_,
              inc_table_id_,
              including_indexes_,
              without_validation_,
              update_global_indexes_);
  return len;
}

DEF_TO_STRING(ObExchangePartitionArg)
{
  int64_t pos = 0;
  pos += ObDDLArg::to_string(buf + pos, buf_len - pos);
  J_OBJ_START();
  J_KV(K_(session_id),

       K_(exchange_partition_level),
       K_(base_table_id),
       K_(base_table_part_name),
       K_(inc_table_id),
       K_(including_indexes),
       K_(without_validation),
       K_(update_global_indexes));
  J_OBJ_END();
  return pos;
}

bool ObTruncateTableArg::is_valid() const
{
  return !database_name_.empty()
      && !table_name_.empty();
}

OB_SERIALIZE_MEMBER((ObTruncateTableArg, ObDDLArg),

                    database_name_,
                    table_name_,
                    session_id_,
                    is_add_to_scheduler_,
                    foreign_key_checks_);

DEF_TO_STRING(ObTruncateTableArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(
       K_(database_name),
       K_(table_name),
       K_(session_id),
       K_(is_add_to_scheduler),
       K_(foreign_key_checks));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER(ObRenameTableItem, origin_db_name_,
                                       new_db_name_,
                                       origin_table_name_,
                                       new_table_name_);

DEF_TO_STRING(ObRenameTableItem)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(origin_db_name),
       K_(new_db_name),
       K_(origin_table_name),
       K_(new_table_name),
       K_(origin_table_id));
  J_OBJ_END();
  return pos;
}

bool ObRenameTableItem::is_valid() const
{
  return !origin_db_name_.empty() && !new_db_name_.empty()
      && !origin_table_name_.empty() && !new_table_name_.empty();
}

bool ObRenameTableArg::is_valid() const
{
  return rename_table_items_.count() > 0;
}

DEF_TO_STRING(ObRenameTableArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(
       K_(rename_table_items),
       K_(lock_session_id),
       K_(lock_session_create_ts),
       K_(lock_priority));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER((ObRenameTableArg, ObDDLArg),

                    rename_table_items_,
                    lock_session_id_,
                    lock_session_create_ts_,
                    lock_priority_);

DEF_TO_STRING(ObTableItem)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(database_name),
       K_(table_name),
       K_(is_hidden),
       K_(table_id));
  J_OBJ_END();
  return pos;
}

bool ObTableItem::operator==(const ObTableItem &r) const
{
  bool ret = false;
  if (OB_NAME_CASE_INVALID != mode_ && mode_ == r.mode_) {
    if (!table_name_.empty() && !r.table_name_.empty() &&
        !database_name_.empty() && !r.database_name_.empty()) {
      //todo compare using case mode @hualong
      //ret = ObCharset::case_mode_equal(mode_, table_name_, r.table_name_) &&
      //    ObCharset::case_mode_equal(mode_, database_name_, r.database_name_);
      ret = table_name_ == r.table_name_ && database_name_ == r.database_name_ && is_hidden_ == r.is_hidden_
        && table_id_ == r.table_id_;
    }
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObTableItem,
                    database_name_,
                    table_name_,
                    is_hidden_,
                    table_id_);

bool ObDropTableArg::is_valid() const
{
  bool ret = (table_type_ < MAX_TABLE_TYPE
              && tables_.count() > 0);
  if (false == ret && (TMP_TABLE == table_type_ || TMP_TABLE_ALL == table_type_)) {
    LOG_WARN("drop table valid check for temp table");
    ret = (session_id_ != OB_INVALID_ID && true == if_exist_ && false == to_recyclebin_);
  }
  return ret;
}


DEF_TO_STRING(ObDropTableArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(
       K_(table_type),
       K_(tables),
       K_(if_exist),
       K_(to_recyclebin),
       K_(session_id),
       K_(sess_create_time),
       K_(foreign_key_checks),
       K_(is_add_to_scheduler),
       K_(force_drop));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER((ObDropTableArg, ObDDLArg),

                    table_type_,
                    tables_,
                    if_exist_,
                    to_recyclebin_,
                    session_id_,
                    sess_create_time_,
                    foreign_key_checks_,
                    is_add_to_scheduler_,
                    force_drop_);

int ObForkTableArg::assign(const ObForkTableArg &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObDDLArg::assign(other))) {
    LOG_WARN("assign ddl arg failed", K(ret));
  } else {

    src_database_name_ = other.src_database_name_;
    src_table_name_ = other.src_table_name_;
    dst_database_name_ = other.dst_database_name_;
    dst_table_name_ = other.dst_table_name_;
    if_not_exist_ = other.if_not_exist_;
    session_id_ = other.session_id_;
  }
  return ret;
}

bool ObForkTableArg::is_valid() const
{
  return (!src_database_name_.empty()
          && !src_table_name_.empty()
          && !dst_database_name_.empty()
          && !dst_table_name_.empty());
}

DEF_TO_STRING(ObForkTableArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(
       K_(src_database_name),
       K_(src_table_name),
       K_(dst_database_name),
       K_(dst_table_name),
       K_(if_not_exist),
       K_(session_id));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER((ObForkTableArg, ObDDLArg),

                    src_database_name_,
                    src_table_name_,
                    dst_database_name_,
                    dst_table_name_,
                    if_not_exist_,
                    session_id_);

int ObForkDatabaseArg::assign(const ObForkDatabaseArg &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObDDLArg::assign(other))) {
    LOG_WARN("assign ddl arg failed", K(ret));
  } else {

    src_database_name_ = other.src_database_name_;
    dst_database_name_ = other.dst_database_name_;
    if_not_exist_ = other.if_not_exist_;
    session_id_ = other.session_id_;
  }
  return ret;
}

bool ObForkDatabaseArg::is_valid() const
{
  return (!src_database_name_.empty()
          && !dst_database_name_.empty());
}

DEF_TO_STRING(ObForkDatabaseArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(
       K_(src_database_name),
       K_(dst_database_name),
       K_(if_not_exist),
       K_(session_id));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER((ObForkDatabaseArg, ObDDLArg),

                    src_database_name_,
                    dst_database_name_,
                    if_not_exist_,
                    session_id_);

bool ObOptimizeTableArg::is_valid() const
{
  return (tables_.count() > 0);
}

OB_SERIALIZE_MEMBER((ObOptimizeTableArg, ObDDLArg),
    tables_);

DEF_TO_STRING(ObOptimizeTableArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(tables));
  J_OBJ_END();
  return pos;
}

DEF_TO_STRING(ObColumnSortItem)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(column_name),
       K_(prefix_len),
       K_(order_type),
       K_(column_id),
       K_(is_func_index));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER(ObColumnSortItem,
                                 column_name_,
                                 prefix_len_,
                                 order_type_,
                                 column_id_,
                                 is_func_index_);


bool ObIndexOption::is_valid() const
{
  return block_size_ > 0
      && index_status_ > INDEX_STATUS_NOT_FOUND
      && index_status_ < INDEX_STATUS_MAX
      && progressive_merge_num_ >= 0;
}

DEF_TO_STRING(ObTableOption)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(block_size),
       K_(index_status),
       K_(compress_method),
       K_(comment),
       K_(progressive_merge_num),
       K_(row_store_type),
       K_(store_format));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER(ObTableOption,
                    block_size_,
                    index_status_,
                    compress_method_,
                    comment_,
                    progressive_merge_num_,
                    row_store_type_,
                    store_format_);

DEF_TO_STRING(ObIndexOption)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(block_size),
       K_(index_status),
       K_(compress_method),
       K_(comment),
       K_(progressive_merge_num),
       K_(parser_name),
       K_(parser_properties),
       K_(index_attributes_set),
       K_(row_store_type),
       K_(store_format));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER((ObIndexOption, ObTableOption), parser_name_, index_attributes_set_, parser_properties_);

bool ObIndexArg::is_valid() const
{
  return !index_name_.empty() && !table_name_.empty()
      && !database_name_.empty() && INVALID_ACTION != index_action_type_;
}

DEF_TO_STRING(ObIndexArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(
       K_(session_id),
       K_(index_name),
       K_(table_name),
       K_(database_name),
       K_(index_action_type),
       K_(compact_level));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER((ObIndexArg, ObDDLArg),

                    index_name_,
                    table_name_,
                    database_name_,
                    index_action_type_,
                    session_id_,
                    compact_level_);

bool ObCreateIndexArg::is_valid() const
{
  // store_columns_ can be empty
  return ObIndexArg::is_valid() && index_type_ > INDEX_TYPE_IS_NOT
         && index_type_ < INDEX_TYPE_MAX
         && index_columns_.count() > 0
         && index_option_.is_valid()
         && index_using_type_ >= USING_BTREE
         && index_using_type_ < USING_TYPE_MAX;
}
DEF_TO_STRING(ObCreateIndexArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_NAME(N_INDEX_ARG);
  J_COLON();
  pos += ObIndexArg::to_string(buf + pos, buf_len - pos);
  J_COMMA();
  J_KV(K_(index_type),
       K_(index_columns),
       K_(store_columns),
       K_(index_option),
       K_(index_using_type),
       K_(data_table_id),
       K_(index_table_id),
       K_(if_not_exist),
       K_(index_schema),
       K_(is_inner),
       K_(sql_mode),
       K_(local_session_var),
       K_(vidx_refresh_info),
       K_(is_rebuild_index),
       K_(is_index_scope_specified),
       K_(is_offline_rebuild),
       K_(index_key),
       K_(data_version));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER((ObCreateIndexArg, ObIndexArg),
                    index_type_,
                    index_columns_,
                    store_columns_,
                    index_option_,
                    index_using_type_,
                    data_table_id_,
                    index_table_id_,
                    if_not_exist_,
                    with_rowid_,
                    index_schema_,
                    is_inner_,
                    hidden_store_columns_,
                    sql_mode_,
                    local_session_var_,
                    vidx_refresh_info_,
                    is_rebuild_index_,
                    is_index_scope_specified_,
                    is_offline_rebuild_,
                    index_key_,
                    data_version_);



OB_SERIALIZE_MEMBER((ObCreateAuxIndexArg, ObDDLArg),

                    data_table_id_,
                    create_index_arg_,
                    snapshot_version_);
OB_SERIALIZE_MEMBER(ObCreateAuxIndexRes,
                    aux_table_id_,
                    ddl_task_id_,
                    schema_generated_);


DEF_TO_STRING(ObAlterIndexArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_NAME(N_INDEX_ARG);
  J_COLON();
  pos += ObIndexArg::to_string(buf + pos, buf_len - pos);
  J_COMMA();
  J_KV(K_(index_visibility));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER((ObAlterIndexArg, ObIndexArg), index_visibility_);
OB_SERIALIZE_MEMBER((ObDropLobArg, ObDDLArg), session_id_, data_table_id_, aux_lob_meta_table_id_);

DEF_TO_STRING(ObDropIndexArg) {
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(
       K_(index_name),
       K_(table_name),
       K_(database_name),
       K_(index_action_type),
       K_(index_table_id),
       K_(is_add_to_scheduler),
       K_(is_in_recyclebin),
       K_(is_hidden),
       K_(is_inner),
       K_(is_vec_inner_drop),
       K_(only_set_status),
       K_(index_ids),
       K_(table_id),
       K_(is_drop_in_rebuild_task));
  J_OBJ_END();
  return pos;
}
OB_SERIALIZE_MEMBER((ObDropIndexArg, ObIndexArg),

                    index_name_,
                    table_name_,
                    database_name_,
                    index_action_type_,
                    index_table_id_,
                    is_add_to_scheduler_,
                    is_in_recyclebin_,
                    is_hidden_,
                    is_inner_,
                    is_vec_inner_drop_,
                    only_set_status_,
                    index_ids_,
                    is_parent_task_dropping_fts_index_,
                    is_parent_task_dropping_multivalue_index_,
                    table_id_,
                    is_drop_in_rebuild_task_,
                    is_parent_task_dropping_spiv_index_);

OB_SERIALIZE_MEMBER(ObDropIndexRes, index_table_id_, schema_version_, task_id_);

int ObDropIndexArg::assign(const ObDropIndexArg &other)
{
  int ret = common::OB_SUCCESS;
  if (OB_FAIL(ObIndexArg::assign(other))) {
    LOG_WARN("fail to assign base", K(ret));
  } else if (OB_FAIL(index_ids_.assign(other.index_ids_))) {
    LOG_WARN("fail to assign index columns", K(ret));
  } else {
    index_table_id_ = other.index_table_id_;
    is_add_to_scheduler_ = other.is_add_to_scheduler_;
    is_hidden_ = other.is_hidden_;
    is_in_recyclebin_ = other.is_in_recyclebin_;
    is_inner_ = other.is_inner_;
    is_vec_inner_drop_ = other.is_vec_inner_drop_;
    is_parent_task_dropping_fts_index_ = other.is_parent_task_dropping_fts_index_;
    is_parent_task_dropping_multivalue_index_ = other.is_parent_task_dropping_multivalue_index_;
    only_set_status_ = other.only_set_status_;
    table_id_ = other.table_id_;
    is_drop_in_rebuild_task_ = other.is_drop_in_rebuild_task_;
    is_parent_task_dropping_spiv_index_ = other.is_parent_task_dropping_spiv_index_;
  }
  return ret;
}


DEF_TO_STRING(ObRebuildIndexArg) {
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(
       K_(index_name),
       K_(table_name),
       K_(database_name),
       K_(index_action_type),
       K_(index_table_id),
       K_(vidx_refresh_info));
  J_OBJ_END();
  return pos;
}
OB_SERIALIZE_MEMBER((ObRebuildIndexArg, ObIndexArg),
                    index_table_id_,
                    vidx_refresh_info_);


DEF_TO_STRING(ObAlterIndexParallelArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_NAME(N_INDEX_ARG);
  J_COLON();
  pos += ObIndexArg::to_string(buf + pos, buf_len - pos);
  J_COMMA();
  J_KV(K_(new_parallel));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER((ObAlterIndexParallelArg, ObIndexArg), new_parallel_);

DEF_TO_STRING(ObRenameIndexArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_NAME(N_INDEX_ARG);
  J_COLON();
  pos += ObIndexArg::to_string(buf + pos, buf_len - pos);
  J_COMMA();
  J_KV(K_(origin_index_name),
       K_(new_index_name));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER((ObRenameIndexArg, ObIndexArg), origin_index_name_, new_index_name_);


DEF_TO_STRING(ObCreateForeignKeyArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_NAME(N_FOREIGN_KEY_ARG);
  J_COLON();
  pos += ObIndexArg::to_string(buf + pos, buf_len - pos);
  J_COMMA();
  J_KV(K_(parent_database),
       K_(parent_table),
       K_(child_columns),
       K_(parent_columns),
       K_(update_action),
       K_(delete_action),
       K_(foreign_key_name),
       K_(enable_flag),
       K_(is_modify_enable_flag),
       K_(fk_ref_type),
       K_(ref_cst_id),
       K_(validate_flag),
       K_(is_modify_validate_flag),
       K_(rely_flag),
       K_(is_modify_rely_flag),
       K_(is_modify_fk_state),
       K_(parent_database_id),
       K_(parent_table_id),
       K_(name_generated_type));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER((ObCreateForeignKeyArg, ObIndexArg),
                    parent_database_,
                    parent_table_,
                    child_columns_,
                    parent_columns_,
                    update_action_,
                    delete_action_,
                    foreign_key_name_,
                    enable_flag_,
                    is_modify_enable_flag_,
                    fk_ref_type_, // FARM COMPAT WHITELIST for ref_cst_type_
                    ref_cst_id_,
                    validate_flag_,
                    is_modify_validate_flag_,
                    rely_flag_,
                    is_modify_rely_flag_,
                    is_modify_fk_state_,
                    need_validate_data_,
                    is_parent_table_mock_,
                    parent_database_id_,
                    parent_table_id_,
                    name_generated_type_);


DEF_TO_STRING(ObDropForeignKeyArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_NAME(N_FOREIGN_KEY_ARG);
  J_COLON();
  pos += ObIndexArg::to_string(buf + pos, buf_len - pos);
  J_COMMA();
  J_KV(K_(foreign_key_name));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER((ObDropForeignKeyArg, ObIndexArg),
                    foreign_key_name_);

bool ObRecyclebinRestoreTableArg::is_valid() const
{
  int bret = true;
  if (origin_table_name_.empty()) {
    bret = false;
    LOG_WARN_RET(OB_INVALID_ERROR, "origin_table_name is empty", K_(origin_table_name));
  } else if ((new_db_name_.empty() && !new_table_name_.empty()) ||
      (!new_db_name_.empty() && new_table_name_.empty())) {
    bret = false;
    LOG_WARN_RET(OB_INVALID_ERROR, "new_db_name or new_table_name is invalid",
             K_(new_db_name), K_(new_table_name));
  }
  return bret;
}

OB_SERIALIZE_MEMBER((ObRecyclebinRestoreTableArg, ObDDLArg),

                    origin_table_name_,
                    new_db_name_,
                    new_table_name_,
                    origin_db_name_);

bool ObPurgeIndexArg::is_valid() const
{
  return OB_INVALID_ID != database_id_ && !table_name_.empty();
}



OB_SERIALIZE_MEMBER((ObPurgeIndexArg, ObDDLArg),

                    database_id_,
                    table_name_);

bool ObRecyclebinRestoreDatabaseArg::is_valid() const
{
  return !origin_db_name_.empty();
}

OB_SERIALIZE_MEMBER((ObRecyclebinRestoreDatabaseArg, ObDDLArg),

                    origin_db_name_,
                    new_db_name_);

bool ObPurgeTableArg::is_valid() const
{
  return OB_INVALID_ID != database_id_ && !table_name_.empty();
}

OB_SERIALIZE_MEMBER((ObPurgeTableArg, ObDDLArg),

                    database_id_,
                    table_name_);

bool ObPurgeDatabaseArg::is_valid() const
{
  return !db_name_.empty();
}

OB_SERIALIZE_MEMBER((ObPurgeDatabaseArg, ObDDLArg),

                    db_name_);





OB_SERIALIZE_MEMBER((ObPurgeRecycleBinArg, ObDDLArg),

                    purge_num_,
                    expire_time_,
                    auto_purge_);


OB_SERIALIZE_MEMBER((ObDependencyObjDDLArg, ObDDLArg),

                    insert_dep_objs_,
                    update_dep_objs_,
                    delete_dep_objs_,
                    schema_,
                    reset_view_column_infos_);


OB_SERIALIZE_MEMBER(ObCheckFrozenScnArg, frozen_scn_);
ObCheckFrozenScnArg::ObCheckFrozenScnArg()
{
  frozen_scn_.set_min();
}

bool ObCheckFrozenScnArg::is_valid() const
{
  return frozen_scn_.is_valid() && frozen_scn_ > SCN::min_scn();
}










OB_SERIALIZE_MEMBER(ObCalcColumnChecksumRequestArg::SingleItem, tablet_id_, calc_table_id_);

bool ObCalcColumnChecksumRequestArg::SingleItem::is_valid() const
{
  return tablet_id_.is_valid() && OB_INVALID_ID != calc_table_id_;
}

void ObCalcColumnChecksumRequestArg::SingleItem::reset()
{
  tablet_id_.reset();
  calc_table_id_ = OB_INVALID_ID;
}

int ObCalcColumnChecksumRequestArg::SingleItem::assign(const SingleItem &other)
{
  int ret = OB_SUCCESS;
  tablet_id_ = other.tablet_id_;
  calc_table_id_ = other.calc_table_id_;
  return ret;
}

OB_SERIALIZE_MEMBER(
    ObCalcColumnChecksumRequestArg,

    target_table_id_,
    schema_version_,
    execution_id_,
    snapshot_version_,
    source_table_id_,
    task_id_,
    calc_items_,
    user_parallelism_);

bool ObCalcColumnChecksumRequestArg::is_valid() const
{
  bool bret = OB_INVALID_ID != target_table_id_
      && OB_INVALID_VERSION != schema_version_ && execution_id_ >= 0
      && OB_INVALID_VERSION != snapshot_version_ && OB_INVALID_ID != source_table_id_
      && task_id_ > 0;
  for (int64_t i = 0; bret && i < calc_items_.count(); ++i) {
    bret = calc_items_.at(i).is_valid();
  }
  return bret;
}

void ObCalcColumnChecksumRequestArg::reset()
{

  target_table_id_ = OB_INVALID_ID;
  schema_version_ = OB_INVALID_VERSION;
  snapshot_version_ = OB_INVALID_VERSION;
  source_table_id_ = OB_INVALID_ID;
  execution_id_ = -1;
  task_id_ = 0;
  user_parallelism_ = 0;
}

OB_SERIALIZE_MEMBER(ObCalcColumnChecksumRequestRes, ret_codes_);

OB_SERIALIZE_MEMBER(
    ObCalcColumnChecksumResponseArg,
    tablet_id_,
    target_table_id_,
    ret_code_,
    source_table_id_,
    schema_version_,
    task_id_);

bool ObCalcColumnChecksumResponseArg::is_valid() const
{
  return tablet_id_.is_valid()
      && OB_INVALID_ID != target_table_id_
      && OB_INVALID_ID != source_table_id_
      && schema_version_ > 0
      && task_id_ > 0;
}

void ObCalcColumnChecksumResponseArg::reset()
{
  tablet_id_.reset();
  target_table_id_ = OB_INVALID_ID;
  ret_code_ = OB_SUCCESS;
  source_table_id_ = OB_INVALID_ID;
  schema_version_ = OB_INVALID_VERSION;
  task_id_ = 0;

}

//----End structs for partition online/offline----


DEF_TO_STRING(ObSwitchSchemaArg)
{
  int64_t pos = 0;
  J_KV(K_(schema_info),
       K_(force_refresh),
       K_(is_async));
  return pos;
}

OB_SERIALIZE_MEMBER(ObSwitchSchemaArg, schema_info_, force_refresh_, is_async_);



OB_SERIALIZE_MEMBER(ObTabletPair, tablet_id_);
OB_SERIALIZE_MEMBER(ObCheckSchemaVersionElapsedArg, schema_version_, need_wait_trans_end_, tablets_, ddl_task_id_);

bool ObCheckSchemaVersionElapsedArg::is_valid() const
{
  bool bret = schema_version_ > 0 && !tablets_.empty() && ddl_task_id_ >= 0;
  for (int64_t i = 0; bret && i < tablets_.count(); ++i) {
    bret = tablets_.at(i).is_valid();
  }
  return bret;
}


bool ObCheckModifyTimeElapsedArg::is_valid() const
{
  bool bret = sstable_exist_ts_ > 0 && ddl_task_id_ >= 0;
  for (int64_t i = 0; bret && i < tablets_.count(); ++i) {
    bret = tablets_.at(i).is_valid();
  }
  return bret;
}

int ObDDLCheckTabletMergeStatusArg::assign(const ObDDLCheckTabletMergeStatusArg &other) {
  int ret = OB_SUCCESS;
  if (OB_FAIL(tablet_ids_.assign(other.tablet_ids_))) {
    LOG_WARN("assign tablet_ids_ failed", K(ret), K(other.tablet_ids_));
  } else {
    snapshot_version_ = other.snapshot_version_;
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObDDLCheckTabletMergeStatusArg, tablet_ids_, snapshot_version_);


OB_SERIALIZE_MEMBER(ObCheckModifyTimeElapsedArg, sstable_exist_ts_, tablets_, ddl_task_id_);


OB_SERIALIZE_MEMBER(ObCheckTransElapsedResult, ret_code_, snapshot_, pending_tx_id_);
OB_SERIALIZE_MEMBER(ObCheckSchemaVersionElapsedResult, results_);


OB_SERIALIZE_MEMBER(CandidateStatus, candidate_status_);

OB_SERIALIZE_MEMBER(ObDDLCheckTabletMergeStatusResult, merge_status_);

//----Structs for managing privileges----
OB_SERIALIZE_MEMBER(ObAccountArg,
                    user_name_,
                    host_name_,
                    is_role_);

bool ObSchemaReviseArg::is_valid() const
{
  bool bret = false;
  if (REVISE_CONSTRAINT_COLUMN_INFO == type_
      || REVISE_NOT_NULL_CONSTRAINT == type_) {
    bret = (OB_INVALID_ID != table_id_)
           && !(REVISE_CONSTRAINT_COLUMN_INFO == type_ && 0 == csts_array_.count());
  }
  return bret;
}


OB_SERIALIZE_MEMBER((ObSchemaReviseArg, ObDDLArg),
                    type_,

                    table_id_,
                    csts_array_);

bool ObCreateUserArg::is_valid() const
{
  return user_infos_.count() > 0;
}


OB_SERIALIZE_MEMBER((ObCreateUserArg, ObDDLArg),

                    user_infos_,
                    if_not_exist_,
                    creator_id_,
                    is_create_role_);

bool ObDropUserArg::is_valid() const
{
  return users_.count() > 0 && hosts_.count() == users_.count();
}

OB_DEF_SERIALIZE(ObDropUserArg)
{
  int ret = OB_SUCCESS;
  BASE_SER((, ObDDLArg));
  LST_DO_CODE(OB_UNIS_ENCODE,

              users_,
              hosts_,
              is_role_);
  return ret;
}

OB_DEF_DESERIALIZE(ObDropUserArg)
{
  int ret = OB_SUCCESS;
  BASE_DESER((, ObDDLArg));
  LST_DO_CODE(OB_UNIS_DECODE,

              users_,
              hosts_);

  if (OB_SUCC(ret)) {
    LST_DO_CODE(OB_UNIS_DECODE, is_role_);
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObDropUserArg)
{
  int64_t len = ObDDLArg::get_serialize_size();
  LST_DO_CODE(OB_UNIS_ADD_LEN,

              users_,
              hosts_,
              is_role_);
  return len;
}

bool ObAlterRoleArg::is_valid() const
{
  return role_name_.length() > 0;
}


OB_DEF_SERIALIZE(ObAlterRoleArg)
{
  int ret = OB_SUCCESS;
  BASE_SER((, ObDDLArg));
  LST_DO_CODE(OB_UNIS_ENCODE,

              role_name_,
              host_name_,
              pwd_enc_);
  return ret;
}

OB_DEF_DESERIALIZE(ObAlterRoleArg)
{
  int ret = OB_SUCCESS;
  BASE_DESER((, ObDDLArg));
  LST_DO_CODE(OB_UNIS_DECODE,

              role_name_,
              host_name_,
              pwd_enc_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObAlterRoleArg)
{
  int64_t len = ObDDLArg::get_serialize_size();
  LST_DO_CODE(OB_UNIS_ADD_LEN,

              role_name_,
              host_name_,
              pwd_enc_);
  return len;
}

bool ObRenameUserArg::is_valid() const
{
  return (old_users_.count() > 0
          && old_users_.count() == new_users_.count()
          && old_hosts_.count() == new_hosts_.count()
          && old_users_.count() == old_hosts_.count());
}

OB_DEF_SERIALIZE(ObRenameUserArg)
{
  int ret = OB_SUCCESS;
  BASE_SER((, ObDDLArg));
  LST_DO_CODE(OB_UNIS_ENCODE,

              old_users_,
              new_users_,
              old_hosts_,
              new_hosts_);
  return ret;
}

OB_DEF_DESERIALIZE(ObRenameUserArg)
{
  int ret = OB_SUCCESS;
  BASE_DESER((, ObDDLArg));
  LST_DO_CODE(OB_UNIS_DECODE,

              old_users_,
              new_users_,
              old_hosts_,
              new_hosts_);

  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObRenameUserArg)
{
  int64_t len = ObDDLArg::get_serialize_size();
  LST_DO_CODE(OB_UNIS_ADD_LEN,

              old_users_,
              new_users_,
              old_hosts_,
              new_hosts_);
  return len;
}


bool ObSetPasswdArg::is_valid() const
{
  // user_name_ and passwd_ can be empty
  return true;
}

OB_DEF_SERIALIZE(ObSetPasswdArg)
{
  int ret = OB_SUCCESS;
  BASE_SER((, ObDDLArg));
  LST_DO_CODE(OB_UNIS_ENCODE,

              user_,
              passwd_,
              host_,
              ssl_type_,
              ssl_cipher_,
              x509_issuer_,
              x509_subject_,
              modify_max_connections_,
              max_connections_per_hour_,
              max_user_connections_);
  return ret;
}

OB_DEF_DESERIALIZE(ObSetPasswdArg)
{
  int ret = OB_SUCCESS;
  host_.assign_ptr(OB_DEFAULT_HOST_NAME, static_cast<int32_t>(STRLEN(OB_DEFAULT_HOST_NAME)));
  ssl_type_ = schema::ObSSLType::SSL_TYPE_NOT_SPECIFIED;
  ssl_cipher_.reset();
  x509_issuer_.reset();
  x509_subject_.reset();

  BASE_DESER((, ObDDLArg));
  LST_DO_CODE(OB_UNIS_DECODE,

              user_,
              passwd_,
              host_,
              ssl_type_,
              ssl_cipher_,
              x509_issuer_,
              x509_subject_,
              modify_max_connections_,
              max_connections_per_hour_,
              max_user_connections_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObSetPasswdArg)
{
  int64_t len = ObDDLArg::get_serialize_size();
  LST_DO_CODE(OB_UNIS_ADD_LEN,

              user_,
              passwd_,
              host_,
              ssl_type_,
              ssl_cipher_,
              x509_issuer_,
              x509_subject_,
              modify_max_connections_,
              max_connections_per_hour_,
              max_user_connections_);
  return len;
}

bool ObLockUserArg::is_valid() const
{
  return users_.count() > 0 && users_.count() == hosts_.count();
}

OB_DEF_SERIALIZE(ObLockUserArg)
{
  int ret = OB_SUCCESS;
  BASE_SER((, ObDDLArg));
  LST_DO_CODE(OB_UNIS_ENCODE,

              users_,
              locked_,
              hosts_);
  return ret;
}

OB_DEF_DESERIALIZE(ObLockUserArg)
{
  int ret = OB_SUCCESS;
  BASE_DESER((, ObDDLArg));
  LST_DO_CODE(OB_UNIS_DECODE,

              users_,
              locked_,
              hosts_);

  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObLockUserArg)
{
  int64_t len = ObDDLArg::get_serialize_size();
  LST_DO_CODE(OB_UNIS_ADD_LEN,

              users_,
              locked_,
              hosts_);
  return len;
}



OB_SERIALIZE_MEMBER((ObAlterUserProfileArg, ObDDLArg),

                    user_name_,
                    host_name_,
                    user_id_,
                    default_role_flag_,
                    role_id_array_,
                    user_ids_);

bool ObGrantArg::is_valid() const
{
  return true;
}

bool ObGrantArg::is_allow_when_disable_ddl() const
{
  return is_inner_;
}


OB_DEF_SERIALIZE(ObGrantArg)
{
  int ret = OB_SUCCESS;
  BASE_SER((, ObDDLArg));
  LST_DO_CODE(OB_UNIS_ENCODE,

              priv_level_,
              db_,
              table_,
              priv_set_,
              users_passwd_,
              need_create_user_,
              has_create_user_priv_,
              hosts_,
              roles_,
              option_,
              sys_priv_array_,
              obj_priv_array_,
              object_type_,
              object_id_,
              ins_col_ids_,
              upd_col_ids_,
              ref_col_ids_,
              grantor_id_,
              remain_roles_,
              is_inner_,
              sel_col_ids_,
              column_names_priv_,
              grantor_,
              grantor_host_);
return ret;
}

OB_DEF_DESERIALIZE(ObGrantArg)
{
  int ret = OB_SUCCESS;
  BASE_DESER((, ObDDLArg));
  LST_DO_CODE(OB_UNIS_DECODE,

              priv_level_,
              db_,
              table_,
              priv_set_,
              users_passwd_,
              need_create_user_,
              has_create_user_priv_,
              hosts_,
              roles_,
              option_,
              sys_priv_array_,
              obj_priv_array_,
              object_type_,
              object_id_,
              ins_col_ids_,
              upd_col_ids_,
              ref_col_ids_,
              grantor_id_,
              remain_roles_,
              is_inner_,
              sel_col_ids_,
              column_names_priv_,
              grantor_,
              grantor_host_);

  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObGrantArg)
{
  int64_t len = ObDDLArg::get_serialize_size();
  LST_DO_CODE(OB_UNIS_ADD_LEN,

              priv_level_,
              db_,
              table_,
              priv_set_,
              users_passwd_,
              need_create_user_,
              has_create_user_priv_,
              hosts_,
              roles_,
              option_,
              sys_priv_array_,
              obj_priv_array_,
              object_type_,
              object_id_,
              ins_col_ids_,
              upd_col_ids_,
              ref_col_ids_,
              grantor_id_,
              remain_roles_,
              is_inner_,
              sel_col_ids_,
              column_names_priv_,
              grantor_,
              grantor_host_);
  return len;
}

bool ObRevokeUserArg::is_valid() const
{
  // FIXME: Currently the role only supports revoke from user
  return OB_INVALID_ID != user_id_;
}

OB_SERIALIZE_MEMBER((ObRevokeUserArg, ObDDLArg),

                    user_id_,
                    priv_set_,
                    revoke_all_,
                    role_ids_);

bool ObRevokeDBArg::is_valid() const
{
  return OB_INVALID_ID != user_id_
      && !db_.empty();
}

OB_SERIALIZE_MEMBER((ObRevokeDBArg, ObDDLArg),

                    user_id_,
                    db_,
                    priv_set_);


bool ObRevokeTableArg::is_valid() const
{
  return OB_INVALID_ID != user_id_
      && !db_.empty() && !table_.empty();
}

OB_SERIALIZE_MEMBER((ObRevokeTableArg, ObDDLArg),

                    user_id_,
                    db_,
                    table_,
                    priv_set_,
                    grant_,
                    obj_id_,
                    obj_type_,
                    grantor_id_,
                    obj_priv_array_,
                    revoke_all_ora_,
                    sel_col_ids_,
                    ins_col_ids_,
                    upd_col_ids_,
                    ref_col_ids_,
                    column_names_priv_,
                    grantor_,
                    grantor_host_);

bool ObRevokeRoutineArg::is_valid() const
{
  return OB_INVALID_ID != user_id_
      && !db_.empty() && !routine_.empty();
}


OB_SERIALIZE_MEMBER((ObRevokeRoutineArg, ObDDLArg),

                    user_id_,
                    db_,
                    routine_,
                    priv_set_,
                    grant_,
                    obj_id_,
                    obj_type_,
                    grantor_id_,
                    obj_priv_array_,
                    revoke_all_ora_,
                    grantor_,
                    grantor_host_);

bool ObRevokeSysPrivArg::is_valid() const
{
  return OB_INVALID_ID != grantee_id_;
}


OB_SERIALIZE_MEMBER((ObRevokeSysPrivArg, ObDDLArg),

                    grantee_id_,
                    sys_priv_array_,
                    role_ids_);

OB_SERIALIZE_MEMBER((ObCreateRoleArg, ObDDLArg),

                    user_infos_);

//----End of structs for managing privileges----








OB_SERIALIZE_MEMBER(ObAdminSetConfigItem, name_, value_, comment_);
OB_SERIALIZE_MEMBER(ObAdminSetConfigArg, items_, is_inner_);

bool ObUpdateIndexStatusArg::is_allow_when_disable_ddl() const
{
  bool bret = false;
  if (is_error_index_status(status_)) {
    bret = true;
  }
  return bret;
}

bool ObUpdateIndexStatusArg::is_valid() const
{
  return OB_INVALID_ID != index_table_id_ && status_ > INDEX_STATUS_NOT_FOUND
      && status_ < INDEX_STATUS_MAX;
}



OB_SERIALIZE_MEMBER((ObUpdateIndexStatusArg, ObDDLArg),
                    index_table_id_,
                    status_,
                    convert_status_,
                    in_offline_ddl_white_list_,
                    data_table_id_,
                    database_name_,
                    task_id_,
                    error_code_);

OB_SERIALIZE_MEMBER(ObMergeFinishArg, server_, frozen_version_);

OB_SERIALIZE_MEMBER(ObDebugSyncActionArg, reset_, clear_, action_);







OB_SERIALIZE_MEMBER(ObMinorFreezeArg,
                    tablet_id_);

int ObMinorFreezeArg::assign(const ObMinorFreezeArg &other)
{
  int ret = OB_SUCCESS;
  tablet_id_ = other.tablet_id_;
  return ret;
}

OB_SERIALIZE_MEMBER(ObRootMinorFreezeArg,
                    tablet_id_,
                    ls_id_);


OB_SERIALIZE_MEMBER(ObTabletMajorFreezeArg,
                    ls_id_,
                    tablet_id_);


bool ObCreateOutlineArg::is_valid() const
{
  bool ret = !outline_info_.get_name_str().empty()
      && !outline_info_.get_outline_content_str().empty();

  if (!outline_info_.is_format()) {
    ret = ret && !(outline_info_.get_sql_text_str().empty() &&
                !ObOutlineInfo::is_sql_id_valid(outline_info_.get_sql_id_str()))
              && !(outline_info_.get_signature_str().empty() &&
                !ObOutlineInfo::is_sql_id_valid(outline_info_.get_sql_id_str()));
  } else {
     ret = ret  && !(outline_info_.get_format_sql_text_str().empty() &&
                  !ObOutlineInfo::is_sql_id_valid(outline_info_.get_format_sql_id_str()))
                && !(outline_info_.get_signature_str().empty() &&
                  !ObOutlineInfo::is_sql_id_valid(outline_info_.get_format_sql_id_str()));
  }
  return ret;
}

OB_SERIALIZE_MEMBER((ObCreateOutlineArg, ObDDLArg),
                    or_replace_,
                    outline_info_,
                    db_name_);

OB_SERIALIZE_MEMBER((ObAlterOutlineArg, ObDDLArg),
                    alter_outline_info_,
                    db_name_);

bool ObDropOutlineArg::is_valid() const
{
  return !db_name_.empty() && !outline_name_.empty();
}

OB_SERIALIZE_MEMBER((ObDropOutlineArg, ObDDLArg),

                    db_name_,
                    outline_name_,
                    is_format_);


OB_SERIALIZE_MEMBER((ObUseDatabaseArg, ObDDLArg));
OB_SERIALIZE_MEMBER(ObGetPartitionCountResult, partition_count_);

OB_SERIALIZE_MEMBER(ObRoutineDDLRes,
                    store_routine_schema_version_);

bool ObCreateRoutineArg::is_valid() const
{
  return !routine_info_.get_routine_name().empty()
      && routine_info_.get_routine_type() != INVALID_ROUTINE_TYPE
      && !routine_info_.get_routine_body().empty();
}


OB_SERIALIZE_MEMBER((ObCreateRoutineArg, ObDDLArg),
                    routine_info_, db_name_,
                    is_or_replace_, is_need_alter_,
                    error_info_, dependency_infos_, with_if_not_exist_);

bool ObDropRoutineArg::is_valid() const
{
  return !routine_name_.empty() && routine_type_ != INVALID_ROUTINE_TYPE;
}

OB_SERIALIZE_MEMBER((ObDropRoutineArg, ObDDLArg),
                    db_name_,
                    routine_name_, routine_type_,
                    if_exist_, error_info_);

bool ObCreatePackageArg::is_valid() const
{
  return !db_name_.empty() && package_info_.is_valid();
}


OB_SERIALIZE_MEMBER((ObCreatePackageArg, ObDDLArg), is_replace_,
                    is_editionable_, db_name_, package_info_,
                    public_routine_infos_, error_info_, dependency_infos_);

bool ObDropPackageArg::is_valid() const
{
  return !db_name_.empty()
      && !package_name_.empty();
}

OB_SERIALIZE_MEMBER((ObDropPackageArg, ObDDLArg),
                    db_name_, package_name_, package_type_,
                    error_info_);

bool ObCreateTriggerArg::is_valid() const
{
  return !trigger_database_.empty()
      && !base_object_name_.empty()
      && trigger_info_.is_valid_for_create();
}


OB_SERIALIZE_MEMBER((ObCreateTriggerArg, ObDDLArg),
                    trigger_database_,
                    base_object_database_,
                    base_object_name_,
                    trigger_info_,
                    flags_,
                    error_info_,
                    dependency_infos_);

OB_SERIALIZE_MEMBER(ObCreateTriggerRes,
                    table_schema_version_,
                    trigger_schema_version_);

bool ObDropTriggerArg::is_valid() const
{
  return true
      && !trigger_database_.empty()
      && !trigger_name_.empty();
}

OB_SERIALIZE_MEMBER((ObDropTriggerArg, ObDDLArg),

                    trigger_database_,
                    trigger_name_,
                    if_exist_);

bool ObAlterTriggerArg::is_valid() const
{
  return trigger_infos_.count() != 0;
}


OB_SERIALIZE_MEMBER((ObAlterTriggerArg, ObDDLArg), trigger_database_,
                    trigger_info_, trigger_infos_, is_set_status_);





OB_SERIALIZE_MEMBER(ObCancelTaskArg, task_id_);





DEF_TO_STRING(ObForceCreateSysTableArg)
{
  int64_t pos = 0;
  J_KV(
       K(table_id_),
       K(last_replay_log_id_));
  return pos;
}

OB_SERIALIZE_MEMBER(ObForceCreateSysTableArg, table_id_, last_replay_log_id_);

DEF_TO_STRING(ObUpdateStatCacheArg)
{
  int64_t pos = 0;
  J_KV(K_(table_id),

       K_(partition_ids),
       K_(column_ids),
       K_(no_invalidate),
       K_(update_system_stats_only));
  return pos;
}
OB_SERIALIZE_MEMBER(ObUpdateStatCacheArg,

                    table_id_,
                    partition_ids_,
                    column_ids_,
                    no_invalidate_,
                    update_system_stats_only_);




OB_SERIALIZE_MEMBER(ObGetWRSArg, scope_, need_filter_);
OB_SERIALIZE_MEMBER(ObGetWRSResult, self_addr_, err_code_);

int64_t ObEstPartArgElement::get_serialize_size(void) const
{
  int64_t len = 0;
  OB_UNIS_ADD_LEN(scan_flag_);
  OB_UNIS_ADD_LEN(index_id_);
  OB_UNIS_ADD_LEN(range_columns_count_);
  OB_UNIS_ADD_LEN(batch_);
  OB_UNIS_ADD_LEN(tablet_id_);
  OB_UNIS_ADD_LEN(tx_id_);

  return len;
}

int ObEstPartArgElement::serialize(char *buf,
                                   const int64_t buf_len,
                                   int64_t &pos) const
{
  int ret = OB_SUCCESS;
  OB_UNIS_ENCODE(scan_flag_);
  OB_UNIS_ENCODE(index_id_);
  OB_UNIS_ENCODE(range_columns_count_);
  OB_UNIS_ENCODE(batch_);
  OB_UNIS_ENCODE(tablet_id_);
  OB_UNIS_ENCODE(tx_id_);

  return ret;
}

int ObEstPartArgElement::deserialize(common::ObIAllocator &allocator,
                                     const char *buf,
                                     const int64_t data_len,
                                     int64_t &pos)
{
  int ret = OB_SUCCESS;
  OB_UNIS_DECODE(scan_flag_);
  OB_UNIS_DECODE(index_id_);
  OB_UNIS_DECODE(range_columns_count_);
  if (OB_SUCC(ret)) {
    if (OB_FAIL(batch_.deserialize(allocator, buf, data_len, pos))) {
      LOG_WARN("fail to deserialize batch", K(ret), K(data_len), K(pos));
    }
  }
  OB_UNIS_DECODE(tablet_id_);
  OB_UNIS_DECODE(tx_id_);
  return ret;
}

void ObEstPartArg::reset()
{
  for (int64_t i = 0; i < index_params_.count(); ++i) {
    index_params_.at(i).batch_.destroy();
  }
}

OB_DEF_SERIALIZE_SIZE(ObEstPartArg)
{
  int64_t len = 0;
  OB_UNIS_ADD_LEN(schema_version_);
  OB_UNIS_ADD_LEN(index_params_);
  return len;
}

OB_DEF_SERIALIZE(ObEstPartArg)
{
  int ret = OB_SUCCESS;
  OB_UNIS_ENCODE(schema_version_);
  OB_UNIS_ENCODE(index_params_);
  return ret;
}

OB_DEF_DESERIALIZE(ObEstPartArg)
{
  int ret = OB_SUCCESS;
  OB_UNIS_DECODE(schema_version_);
  int64_t N = 0;
  OB_UNIS_DECODE(N);
  for (int64_t i = 0; OB_SUCC(ret) && i < N; i++) {
    ObEstPartArgElement arg;
    if (OB_FAIL(arg.deserialize(allocator_, buf, data_len, pos))) {
      SQL_OPT_LOG(WARN, "fail to deserialize index param", K(ret));
    } else if (OB_FAIL(index_params_.push_back(arg))) {
      SQL_OPT_LOG(WARN, "failed to push back arg element", K(ret));
    }
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObEstPartResElement, logical_row_count_,
                                         physical_row_count_,
                                         reliable_,
                                         est_records_);

OB_SERIALIZE_MEMBER(ObEstPartRes, index_param_res_);

OB_SERIALIZE_MEMBER((ObDDLNopOpreatorArg, ObDDLArg),
                     schema_operation_);
int ObDDLRes::assign(const ObDDLRes &other)
{
  int ret = OB_SUCCESS;

  schema_id_ = other.schema_id_;
  task_id_ = other.task_id_;
  return ret;
}

OB_SERIALIZE_MEMBER(ObDDLRes, schema_id_, task_id_);


OB_SERIALIZE_MEMBER(ObParallelDDLRes, schema_version_);
void ObAlterTableRes::reset()
{
  index_table_id_ = OB_INVALID_ID;
  constriant_id_ = OB_INVALID_ID;
  schema_version_ = OB_INVALID_VERSION;
  res_arg_array_.reset();
  ddl_type_ = ObDDLType::DDL_INVALID;
  task_id_ = 0;
  ddl_res_array_.reset();
  ddl_need_retry_at_executor_ = false;
}



OB_SERIALIZE_MEMBER(ObDropDatabaseRes, ddl_res_, affected_row_);
OB_SERIALIZE_MEMBER(ObAlterTableResArg, schema_type_, schema_id_, schema_version_, part_object_id_);
OB_SERIALIZE_MEMBER(ObAlterTableRes, index_table_id_, constriant_id_, schema_version_,
res_arg_array_, ddl_type_, task_id_, ddl_res_array_, ddl_need_retry_at_executor_);
OB_SERIALIZE_MEMBER(ObGetRuntimeSchemaVersionArg);
OB_SERIALIZE_MEMBER(ObGetRuntimeSchemaVersionResult, schema_version_);

OB_SERIALIZE_MEMBER(CheckLeaderRpcIndex, switchover_timestamp_, epoch_,
                    ml_pk_index_, pkey_info_start_index_);


bool CheckLeaderRpcIndex::is_valid() const
{
  return switchover_timestamp_ > 0 && epoch_ >= 0
      && ml_pk_index_ >= 0 && pkey_info_start_index_ >= 0;
}

void CheckLeaderRpcIndex::reset()
{
  switchover_timestamp_ = 0;
  epoch_ = -1;
  ml_pk_index_ = -1;
  pkey_info_start_index_ = -1;

}










OB_SERIALIZE_MEMBER(ObDDLLocalBuildResponse, tablet_id_,
                    source_table_id_, dest_schema_id_, ret_code_, snapshot_version_, schema_version_,
                    task_id_, execution_id_, row_scanned_, row_inserted_, dest_schema_version_,
                    server_addr_, physical_row_count_);

















bool ObBatchRemoveTabletArg::is_valid() const
{
  bool is_valid = !tablet_ids_.empty();
  for (int64_t i = 0; i < tablet_ids_.count() && is_valid; i++) {
    is_valid = tablet_ids_.at(i).is_valid();
  }
  return is_valid;
}

void ObBatchRemoveTabletArg::reset()
{
  tablet_ids_.reset();
}

int ObBatchRemoveTabletArg::assign(const ObBatchRemoveTabletArg &arg)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(tablet_ids_.assign(arg.tablet_ids_))) {
    LOG_WARN("failed to assign table ids", KR(ret), K(arg));
  }
  return ret;
}

int ObBatchRemoveTabletArg::init(const ObIArray<common::ObTabletID> &tablet_ids)
{
  int ret = OB_SUCCESS;
  bool is_valid = !tablet_ids.empty();
  for (int64_t i = 0; i < tablet_ids.count() && is_valid; i++) {
    is_valid = tablet_ids.at(i).is_valid();
  }
  if (OB_UNLIKELY(!is_valid)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tablet_ids));
  } else if (OB_FAIL(tablet_ids_.assign(tablet_ids))) {
    LOG_WARN("failed to assign table schema index", KR(ret), K(tablet_ids));
  }
  return ret;
}

DEF_TO_STRING(ObBatchRemoveTabletArg)
{
  int64_t pos = 0;
  J_KV(K_(tablet_ids));
  return pos;
}

OB_DEF_SERIALIZE(ObBatchRemoveTabletArg)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE, tablet_ids_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObBatchRemoveTabletArg)
{
  int len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN, tablet_ids_);
  return len;
}


OB_DEF_DESERIALIZE(ObBatchRemoveTabletArg)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE, tablet_ids_);
  return ret;
}

// ----------------------
bool ObCreateTabletInfo::is_valid() const
{
  bool is_valid = data_tablet_id_.is_valid()
                  && table_schema_index_.count() > 0
                  && table_schema_index_.count() == tablet_ids_.count()
                  && (create_commit_versions_.empty() || create_commit_versions_.count() == tablet_ids_.count());
  for (int64_t i = 0; i < tablet_ids_.count() && is_valid; i++) {
    is_valid = tablet_ids_.at(i).is_valid();
  }
  return is_valid;
}

void ObCreateTabletInfo::reset()
{
  tablet_ids_.reset();
  data_tablet_id_.reset();
  table_schema_index_.reset();
  is_create_bind_hidden_tablets_ = false;
  create_commit_versions_.reset();
  fork_tablet_infos_.reset();
}

int ObCreateTabletInfo::assign(const ObCreateTabletInfo &info)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("info is invalid", KR(ret), K(info));
  } else if (OB_FAIL(tablet_ids_.assign(info.tablet_ids_))) {
    LOG_WARN("failed to assign table ids", KR(ret), K(info));
  } else if (OB_FAIL(table_schema_index_.assign(info.table_schema_index_))) {
    LOG_WARN("failed to assign table schema index", KR(ret), K(info));
  } else if (OB_FAIL(create_commit_versions_.assign(info.create_commit_versions_))) {
    LOG_WARN("failed to assign create commit versions", KR(ret), K(info));
  } else if (OB_FAIL(fork_tablet_infos_.assign(info.fork_tablet_infos_))) {
    LOG_WARN("failed to assign fork tablet infos", KR(ret), K(info));
  } else {
    data_tablet_id_ = info.data_tablet_id_;
    is_create_bind_hidden_tablets_ = info.is_create_bind_hidden_tablets_;
  }
  return ret;
}

int ObCreateTabletInfo::init(const ObIArray<common::ObTabletID> &tablet_ids,
                             common::ObTabletID data_tablet_id,
                             const common::ObIArray<int64_t> &table_schema_index,
                             const bool is_create_bind_hidden_tablets,
                             const ObIArray<int64_t> &create_commit_versions)
{
  int ret = OB_SUCCESS;
  bool is_valid = data_tablet_id.is_valid()
                  // && OB_INVALID_VERSION != schema_version
                  && table_schema_index.count() > 0
                  && table_schema_index.count() == tablet_ids.count();
  for (int64_t i = 0; i < tablet_ids.count() && is_valid; i++) {
    is_valid = tablet_ids.at(i).is_valid();
  }
  if (OB_UNLIKELY(!is_valid)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tablet_ids), K(data_tablet_id), K(table_schema_index));
  } else if (OB_FAIL(tablet_ids_.assign(tablet_ids))) {
    LOG_WARN("failed to assign table schema index", KR(ret), K(table_schema_index));
  } else if (OB_FAIL(table_schema_index_.assign(table_schema_index))) {
    LOG_WARN("failed to assign table schema index", KR(ret), K(table_schema_index));
  } else if (OB_FAIL(create_commit_versions_.assign(create_commit_versions))) {
    LOG_WARN("failed to assign create commit versions", KR(ret), K(create_commit_versions));
  } else {
    data_tablet_id_ = data_tablet_id;
    is_create_bind_hidden_tablets_ = is_create_bind_hidden_tablets;
  }
  return ret;
}

int ObCreateTabletInfo::init(const ObIArray<common::ObTabletID> &tablet_ids,
                             common::ObTabletID data_tablet_id,
                             const common::ObIArray<int64_t> &table_schema_index,
                             const bool is_create_bind_hidden_tablets,
                             const ObIArray<int64_t> &create_commit_versions,
                             const ObIArray<share::ObForkTabletInfo> &fork_tablet_infos)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(fork_tablet_infos.count() != 0 && fork_tablet_infos.count() != tablet_ids.count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("fork tablet infos count not match tablet ids count", KR(ret), K(tablet_ids.count()),
      K(fork_tablet_infos));
  } else if (OB_FAIL(init(tablet_ids, data_tablet_id, table_schema_index, is_create_bind_hidden_tablets,
      create_commit_versions))) {
    LOG_WARN("failed to init create tablet info", KR(ret));
  } else if (OB_FAIL(fork_tablet_infos_.assign(fork_tablet_infos))) {
    LOG_WARN("failed to assign fork tablet infos", KR(ret), K(fork_tablet_infos));
  }
  return ret;
}

int ObCreateTabletInfo::get_fork_tablet_info(const int64_t idx, share::ObForkTabletInfo &fork_tablet_info) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(idx < 0 || idx >= tablet_ids_.count() || (fork_tablet_infos_.count() > 0 && idx >= fork_tablet_infos_.count()))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid index", KR(ret), K(idx), "tablet_count", tablet_ids_.count(), "fork_tablet_infos_count", fork_tablet_infos_.count());
  } else if (fork_tablet_infos_.empty()) {
    fork_tablet_info.reset();
  } else {
    fork_tablet_info = fork_tablet_infos_.at(idx);
  }
  return ret;
}

DEF_TO_STRING(ObCreateTabletInfo)
{
  int64_t pos = 0;
  J_KV(K_(tablet_ids), K_(data_tablet_id), K_(table_schema_index), K_(is_create_bind_hidden_tablets), K_(create_commit_versions), K_(fork_tablet_infos));
  return pos;
}

OB_SERIALIZE_MEMBER(ObCreateTabletInfo, tablet_ids_, data_tablet_id_, table_schema_index_, is_create_bind_hidden_tablets_, create_commit_versions_, fork_tablet_infos_);

int ObCreateTabletExtraInfo::init(const uint64_t data_format_version,
                                  const bool need_create_empty_major,
                                  const bool micro_index_clustered)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(data_format_version <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg",
             K(ret), K(data_format_version), K(need_create_empty_major),
             K(micro_index_clustered));
  } else {
    data_format_version_ = data_format_version;
    need_create_empty_major_ = need_create_empty_major;
    micro_index_clustered_ = micro_index_clustered;
  }
  return ret;
}

void ObCreateTabletExtraInfo::reset()
{
  need_create_empty_major_ = true;
  data_format_version_ = 0;
  micro_index_clustered_ = false;
}

int ObCreateTabletExtraInfo::assign(const ObCreateTabletExtraInfo &other)
{
  int ret = OB_SUCCESS;
  data_format_version_ = other.data_format_version_;
  need_create_empty_major_ = other.need_create_empty_major_;
  micro_index_clustered_ = other.micro_index_clustered_;
  return ret;
}

OB_SERIALIZE_MEMBER(ObCreateTabletExtraInfo,
                    data_format_version_,
                    need_create_empty_major_,
                    micro_index_clustered_);

// ObBatchCreateTabletArg implementation moved to storage/tablet/ob_batch_create_tablet_arg.cpp

bool ObFetchTabletSeqArg::is_valid() const
{
  return true
         && tablet_id_.is_valid();
}




DEF_TO_STRING(ObFetchTabletSeqArg)
{
  int64_t pos = 0;
  J_KV(K_(tablet_id));
  return pos;
}

OB_SERIALIZE_MEMBER(ObFetchTabletSeqArg, cache_size_, tablet_id_);

bool ObFetchTabletSeqRes::is_valid() const
{
  return true
         && cache_interval_.is_valid();
}

void ObFetchTabletSeqRes::reset()
{
  cache_interval_.reset();

}



DEF_TO_STRING(ObFetchTabletSeqRes)
{
  int64_t pos = 0;
  J_KV(K_(cache_interval));
  return pos;
}

OB_SERIALIZE_MEMBER(ObFetchTabletSeqRes, cache_interval_);




















OB_SERIALIZE_MEMBER(ObSwitchSchemaResult, ret_);

int ObRuntimeConfigArg::assign(const ObRuntimeConfigArg &other)
{
  int ret = OB_SUCCESS;

  config_str_ = other.config_str_;
  return ret;
}

OB_SERIALIZE_MEMBER(ObRuntimeConfigArg, config_str_);











OB_SERIALIZE_MEMBER(ObFlushOptStatArg, is_flush_col_usage_, is_flush_dml_stat_);

ObCancelDDLTaskArg::ObCancelDDLTaskArg()
  : task_id_()
{
}

ObCancelDDLTaskArg::ObCancelDDLTaskArg(const ObCurTraceId::TraceId &task_id)
  : task_id_(task_id)
{
}



OB_SERIALIZE_MEMBER(ObCancelDDLTaskArg, task_id_);

int ObEstBlockArgElement::assign(const ObEstBlockArgElement &other)
{
  int ret = OB_SUCCESS;

  tablet_id_ = other.tablet_id_;
  return ret;
}

OB_SERIALIZE_MEMBER(ObEstBlockArgElement, tablet_id_);



OB_SERIALIZE_MEMBER(ObEstBlockArg, tablet_params_arg_);

int ObEstBlockResElement::assign(const ObEstBlockResElement &other)
{
  int ret = OB_SUCCESS;
  macro_block_count_ = other.macro_block_count_;
  micro_block_count_ = other.micro_block_count_;
  sstable_row_count_ = other.sstable_row_count_;
  memtable_row_count_ = other.memtable_row_count_;
  return ret;
}

OB_SERIALIZE_MEMBER(ObEstBlockResElement, macro_block_count_, micro_block_count_,
    sstable_row_count_, memtable_row_count_);


OB_SERIALIZE_MEMBER(ObEstBlockRes, tablet_params_res_);

OB_SERIALIZE_MEMBER(ObBatchGetTabletAutoincSeqArg, src_tablet_ids_, dest_tablet_ids_);

int ObBatchGetTabletAutoincSeqArg::assign(const ObBatchGetTabletAutoincSeqArg &other)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(src_tablet_ids_.assign(other.src_tablet_ids_))) {
    LOG_WARN("failed to assign src tablet ids", K(ret), K(other));
  } else if (OB_FAIL(dest_tablet_ids_.assign(other.dest_tablet_ids_))) {
    LOG_WARN("failed to assign dest tablet ids", K(ret), K(other));
  }
  return ret;
}

int ObBatchGetTabletAutoincSeqArg::init(const ObIArray<share::ObTabletAutoincSeqCopyParam> &params)
{
  int ret = OB_SUCCESS;

  src_tablet_ids_.reset();
  dest_tablet_ids_.reset();
  for (int64_t i = 0; OB_SUCC(ret) && i < params.count(); i++) {
    const ObTabletAutoincSeqCopyParam &param = params.at(i);
    if (OB_FAIL(src_tablet_ids_.push_back(param.src_tablet_id_))) {
      LOG_WARN("failed to push src tablet id", K(ret));
    } else if (OB_FAIL(dest_tablet_ids_.push_back(param.dest_tablet_id_))) {
      LOG_WARN("failed to push dest tablet id", K(ret));
    }
  }
  if (OB_SUCC(ret) && OB_UNLIKELY(!is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(*this));
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObBatchGetTabletAutoincSeqRes, autoinc_params_);


OB_SERIALIZE_MEMBER(ObBatchSetTabletAutoincSeqArg, autoinc_params_, is_tablet_creating_);

int ObBatchSetTabletAutoincSeqArg::assign(const ObBatchSetTabletAutoincSeqArg &other)
{
  int ret = OB_SUCCESS;

  is_tablet_creating_ = other.is_tablet_creating_;
  if (OB_FAIL(autoinc_params_.assign(other.autoinc_params_))) {
    LOG_WARN("failed to assign autoinc params", K(ret), K(other));
  }
  return ret;
}

int ObBatchSetTabletAutoincSeqArg::init(const ObIArray<share::ObTabletAutoincSeqCopyParam> &params)
{
  int ret = OB_SUCCESS;

  autoinc_params_.reset();
  for (int64_t i = 0; OB_SUCC(ret) && i < params.count(); i++) {
    const ObTabletAutoincSeqCopyParam &param = params.at(i);
    if (OB_FAIL(autoinc_params_.push_back(param))) {
      LOG_WARN("failed to push dest tablet id", K(ret));
    }
  }
  if (OB_SUCC(ret) && OB_UNLIKELY(!is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(*this));
  }
  return ret;
}

void ObBatchSetTabletAutoincSeqArg::reset()
{

  autoinc_params_.reset();
  is_tablet_creating_ = false;
  return;
}

OB_SERIALIZE_MEMBER(ObBatchSetTabletAutoincSeqRes, autoinc_params_);


OB_SERIALIZE_MEMBER(ObBatchGetTabletBindingArg, tablet_ids_, check_committed_);


int ObBatchGetTabletBindingArg::init(const ObIArray<ObTabletID> &tablet_ids, const bool check_committed)
{
  int ret = OB_SUCCESS;

  check_committed_ = check_committed;
  if (OB_FAIL(tablet_ids_.assign(tablet_ids))) {
    LOG_WARN("failed to assign", K(ret));
  }
  if (OB_SUCC(ret) && OB_UNLIKELY(!is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(*this));
  }
  return ret;
}



OB_SERIALIZE_MEMBER(ObInitRuntimeConfigArg, configs_);

int ObInitRuntimeConfigArg::assign(const ObInitRuntimeConfigArg &other)
{
  int ret = OB_SUCCESS;
  if (this == &other) {
  } else if (OB_FAIL(configs_.assign(other.configs_))) {
    LOG_WARN("fail to assign runtime configs", KR(ret), K(other));
      }
  return ret;
}


int ObInitRuntimeConfigArg::add_config(const ObRuntimeConfigArg &arg)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(configs_.push_back(arg))) {
    LOG_WARN("fail to append runtime config", KR(ret), K(arg));
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObInitRuntimeConfigRes, ret_);

OB_SERIALIZE_MEMBER((ObRecompileAllViewsBatchArg, ObDDLArg),
                    view_ids_);


OB_SERIALIZE_MEMBER(ObCancelGatherStatsArg, task_id_);


















OB_SERIALIZE_MEMBER((ObCreateAiModelArg, ObDDLArg), model_info_);
OB_SERIALIZE_MEMBER((ObDropAiModelArg, ObDDLArg), ai_model_name_);

int ObCreateAiModelArg::check_valid() const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(model_info_.check_valid())) {
    LOG_WARN("invalid model info", K(ret), K(model_info_));
  }
  return ret;
}

int ObCreateAiModelArg::assign(const ObCreateAiModelArg &other)
{
  int ret = OB_SUCCESS;
  if (this == &other) {
  } else if (OB_FAIL(ObDDLArg::assign(other))) {
    LOG_WARN("fail to assign ddl arg", KR(ret), K(other));
  } else {
    model_info_ = other.model_info_;
  }
  return ret;
}

int ObDropAiModelArg::assign(const ObDropAiModelArg &other)
{
  int ret = OB_SUCCESS;
  if (this == &other) {
  } else if (OB_FAIL(ObDDLArg::assign(other))) {
    LOG_WARN("fail to assign ddl arg", KR(ret), K(other));
  } else {
    ai_model_name_ = other.ai_model_name_;
  }
  return ret;
}

bool ObRevokeObjMysqlArg::is_valid() const
{
  return OB_INVALID_ID != user_id_
      && !obj_name_.empty();
}

int ObRevokeObjMysqlArg::assign(const ObRevokeObjMysqlArg& other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObDDLArg::assign(other))) {
    LOG_WARN("fail to assign ddl arg", KR(ret));
  } else {

    user_id_ = other.user_id_;
    obj_name_ = other.obj_name_;
    priv_set_ = other.priv_set_;
    grant_ = other.grant_;
    obj_type_ = other.obj_type_;
    grantor_ = other.grantor_;
    grantor_host_ = other.grantor_host_;
  }
  return ret;
}

OB_SERIALIZE_MEMBER((ObRevokeObjMysqlArg, ObDDLArg),

                    user_id_,
                    obj_name_,
                    obj_type_,
                    priv_set_,
                    grant_,
                    grantor_,
                    grantor_host_);


}//end namespace obcall
}//end namespace oceanbase
