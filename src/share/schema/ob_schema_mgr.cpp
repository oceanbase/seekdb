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

#define USING_LOG_PREFIX SHARE_SCHEMA


#include "ob_schema_mgr.h"

#include <new>

#include "lib/alloc/alloc_struct.h"
#include "lib/alloc/ob_iallocator.h"
#include "lib/container/ob_array.h"
#include "lib/container/ob_array_wrap.h"
#include "lib/container/ob_iarray.h"
#include "lib/ob_check_macros.h"
#include "lib/oblog/ob_log_level.h"
#include "lib/oblog/ob_log_print_kv.h"
#include "lib/time/ob_time_utility.h"
#include "lib/utility/ob_backtrace.h"
#include "lib/utility/ob_hang_fatal_error.h"
#include "lib/utility/ob_mod_define.h"
#include "lib/utility/ob_tracepoint.h"
#include "lib/utility/utility.h"
#include "lib/worker.h"
#include "share/inner_table/ob_inner_table_schema_constants.h"
#include "share/ob_errno.h"
#include "share/ob_force_print_log.h"
#include "share/schema/ob_schema_utils.h"

namespace oceanbase
{
using namespace common;
using namespace common::hash;

namespace share
{
namespace schema
{

ObSimpleServerRuntimeSchema::ObSimpleServerRuntimeSchema()
  : ObSchema()
{
  reset();
}

ObSimpleServerRuntimeSchema::ObSimpleServerRuntimeSchema(ObIAllocator *allocator)
  : ObSchema(allocator)
{
  reset();
}

ObSimpleServerRuntimeSchema::ObSimpleServerRuntimeSchema(const ObSimpleServerRuntimeSchema &other)
  : ObSchema()
{
  reset();
  *this = other;
}

ObSimpleServerRuntimeSchema::~ObSimpleServerRuntimeSchema()
{
}

ObSimpleServerRuntimeSchema &ObSimpleServerRuntimeSchema::operator =(const ObSimpleServerRuntimeSchema &other)
{
  if (this != &other) {
    reset();
    int ret = OB_SUCCESS;
    error_ret_ = other.error_ret_;

    schema_version_ = other.schema_version_;
    name_case_mode_ = other.name_case_mode_;
    read_only_ = other.read_only_;
    gmt_modified_ = other.gmt_modified_;
    status_ = other.status_;
    in_recyclebin_ = other.in_recyclebin_;
    if (OB_FAIL(deep_copy_str(other.runtime_name_, runtime_name_))) {
      LOG_WARN("Fail to deep copy runtime_name", K(ret));
    }
    if (OB_FAIL(ret)) {
      error_ret_ = ret;
    }
  }

  return *this;
}


void ObSimpleServerRuntimeSchema::reset()
{
  ObSchema::reset();

  schema_version_ = OB_INVALID_VERSION;
  runtime_name_.reset();
  name_case_mode_ = OB_NAME_CASE_INVALID;
  read_only_ = false;
  gmt_modified_ = 0;
  status_ = SERVER_RUNTIME_STATUS_NORMAL;
  in_recyclebin_ = false;
}

bool ObSimpleServerRuntimeSchema::is_valid() const
{
  bool ret = true;
  if (schema_version_ < 0
      || runtime_name_.empty()) {
    ret = false;
  }
  return ret;
}

int64_t ObSimpleServerRuntimeSchema::get_convert_size() const
{
  int64_t convert_size = 0;

  convert_size += sizeof(ObSimpleServerRuntimeSchema);
  convert_size += runtime_name_.length() + 1;

  return convert_size;
}

ObSimpleUserSchema::ObSimpleUserSchema()
  : ObSchema()
{
  reset();
}

ObSimpleUserSchema::ObSimpleUserSchema(ObIAllocator *allocator)
  : ObSchema(allocator)
{
  reset();
}

ObSimpleUserSchema::ObSimpleUserSchema(const ObSimpleUserSchema &other)
  : ObSchema()
{
  reset();
  *this = other;
}

ObSimpleUserSchema::~ObSimpleUserSchema()
{
}

ObSimpleUserSchema &ObSimpleUserSchema::operator =(const ObSimpleUserSchema &other)
{
  if (this != &other) {
    reset();
    int ret = OB_SUCCESS;
    error_ret_ = other.error_ret_;
    
    user_id_ = other.user_id_;
    type_ = other.type_;
    schema_version_ = other.schema_version_;
    if (OB_FAIL(deep_copy_str(other.user_name_, user_name_))) {
      LOG_WARN("Fail to deep copy user_name", K(ret));
    } else if (OB_FAIL(deep_copy_str(other.host_name_, host_name_))) {
      LOG_WARN("Fail to deep copy host_name", K(ret));
    }
    if (OB_FAIL(ret)) {
      error_ret_ = ret;
    }
  }

  return *this;
}


void ObSimpleUserSchema::reset()
{
  ObSchema::reset();
  
  user_id_ = OB_INVALID_ID;
  schema_version_ = OB_INVALID_VERSION;
  user_name_.reset();
  host_name_.reset();
  type_ = OB_USER;
}

bool ObSimpleUserSchema::is_valid() const
{
  bool ret = true;
  if (OB_INVALID_ID == user_id_
      || schema_version_ < 0) {
    ret = false;
  }
  return ret;
}

int64_t ObSimpleUserSchema::get_convert_size() const
{
  int64_t convert_size = 0;

  convert_size += sizeof(ObSimpleUserSchema);
  convert_size += user_name_.length() + host_name_.length() + 2;

  return convert_size;
}

ObSimpleDatabaseSchema::ObSimpleDatabaseSchema()
  : ObSchema()
{
  reset();
}

ObSimpleDatabaseSchema::ObSimpleDatabaseSchema(ObIAllocator *allocator)
  : ObSchema(allocator)
{
  reset();
}

ObSimpleDatabaseSchema::ObSimpleDatabaseSchema(const ObSimpleDatabaseSchema &other)
  : ObSchema()
{
  reset();
  *this = other;
}

ObSimpleDatabaseSchema::~ObSimpleDatabaseSchema()
{
}

ObSimpleDatabaseSchema &ObSimpleDatabaseSchema::operator =(const ObSimpleDatabaseSchema &other)
{
  if (this != &other) {
    reset();
    int ret = OB_SUCCESS;
    error_ret_ = other.error_ret_;
    
    database_id_ = other.database_id_;
    schema_version_ = other.schema_version_;
    name_case_mode_ = other.name_case_mode_;
    if (OB_FAIL(deep_copy_str(other.database_name_, database_name_))) {
      LOG_WARN("Fail to deep copy database_name", K(ret));
    }
    if (OB_FAIL(ret)) {
      error_ret_ = ret;
    }
  }

  return *this;
}


void ObSimpleDatabaseSchema::reset()
{
  ObSchema::reset();
  
  database_id_ = OB_INVALID_ID;
  schema_version_ = OB_INVALID_VERSION;
  database_name_.reset();
  name_case_mode_ = OB_NAME_CASE_INVALID;
}

bool ObSimpleDatabaseSchema::is_valid() const
{
  bool ret = true;
  if (OB_INVALID_ID == database_id_
      || schema_version_ < 0
      || database_name_.empty()) {
    ret = false;
  }
  return ret;
}

int64_t ObSimpleDatabaseSchema::get_convert_size() const
{
  int64_t convert_size = 0;

  convert_size += sizeof(ObSimpleDatabaseSchema);
  convert_size += database_name_.length() + 1;

  return convert_size;
}

////////////////////////////////////////////////////////////////
ObSchemaMgr::ObSchemaMgr()
    : local_allocator_(lib::ObMemAttr(ObModIds::OB_SCHEMA_GETTER_GUARD, ObCtxIds::SCHEMA_SERVICE)),
      allocator_(local_allocator_),
      schema_version_(OB_INVALID_VERSION),
      is_consistent_(true),
      user_infos_(0, NULL, lib::ObMemAttr(ObModIds::OB_SCHEMA_USER_INFO_VEC, ObCtxIds::SCHEMA_SERVICE)),
      database_infos_(0, NULL, lib::ObMemAttr(ObModIds::OB_SCHEMA_DB_INFO_VEC, ObCtxIds::SCHEMA_SERVICE)),
      database_name_map_(lib::ObMemAttr(ObModIds::OB_SCHEMA_DATABASE_NAME_MAP, ObCtxIds::SCHEMA_SERVICE)),
      table_infos_(0, NULL, lib::ObMemAttr(ObModIds::OB_SCHEMA_TABLE_INFO_VEC, ObCtxIds::SCHEMA_SERVICE)),
      index_infos_(0, NULL, lib::ObMemAttr(ObModIds::OB_SCHEMA_INDEX_INFO_VEC, ObCtxIds::SCHEMA_SERVICE)),
      lob_meta_infos_(0, NULL, lib::ObMemAttr(ObModIds::OB_SCHEMA_LOB_META_INFO_VEC, ObCtxIds::SCHEMA_SERVICE)),
      lob_piece_infos_(0, NULL, lib::ObMemAttr(ObModIds::OB_SCHEMA_LOB_PIECE_INFO_VEC, ObCtxIds::SCHEMA_SERVICE)),
      table_id_map_(lib::ObMemAttr(ObModIds::OB_SCHEMA_TABLE_ID_MAP, ObCtxIds::SCHEMA_SERVICE)),
      table_name_map_(lib::ObMemAttr(ObModIds::OB_SCHEMA_TABLE_NAME_MAP, ObCtxIds::SCHEMA_SERVICE)),
      normal_index_name_map_(lib::ObMemAttr(ObModIds::OB_SCHEMA_INDEX_NAME_MAP, ObCtxIds::SCHEMA_SERVICE)),
      outline_mgr_(allocator_),
      routine_mgr_(allocator_),
      priv_mgr_(allocator_),
      package_mgr_(allocator_),
      trigger_mgr_(allocator_),
      foreign_key_name_map_(lib::ObMemAttr(ObModIds::OB_SCHEMA_FOREIGN_KEY_NAME_MAP, ObCtxIds::SCHEMA_SERVICE)),
      constraint_name_map_(lib::ObMemAttr(ObModIds::OB_SCHEMA_CONSTRAINT_NAME_MAP, ObCtxIds::SCHEMA_SERVICE)),
      sys_variable_mgr_(allocator_),
      hidden_table_name_map_(lib::ObMemAttr("HiddenTblNames", ObCtxIds::SCHEMA_SERVICE)),
      built_in_index_name_map_(lib::ObMemAttr("BuiltInIdxNames", ObCtxIds::SCHEMA_SERVICE)),
      mock_fk_parent_table_mgr_(allocator_),
      timestamp_in_slot_(0),
      allocator_idx_(OB_INVALID_INDEX),
      ai_model_mgr_(allocator_)
{
}

ObSchemaMgr::ObSchemaMgr(ObIAllocator &allocator)
    : local_allocator_(lib::ObMemAttr(ObModIds::OB_SCHEMA_GETTER_GUARD, ObCtxIds::SCHEMA_SERVICE)),
      allocator_(allocator),
      schema_version_(OB_INVALID_VERSION),
      is_consistent_(true),
      user_infos_(0, NULL, lib::ObMemAttr(ObModIds::OB_SCHEMA_USER_INFO_VEC, ObCtxIds::SCHEMA_SERVICE)),
      database_infos_(0, NULL, lib::ObMemAttr(ObModIds::OB_SCHEMA_DB_INFO_VEC, ObCtxIds::SCHEMA_SERVICE)),
      database_name_map_(lib::ObMemAttr(ObModIds::OB_SCHEMA_DATABASE_NAME_MAP, ObCtxIds::SCHEMA_SERVICE)),
      table_infos_(0, NULL, lib::ObMemAttr(ObModIds::OB_SCHEMA_TABLE_INFO_VEC, ObCtxIds::SCHEMA_SERVICE)),
      index_infos_(0, NULL, lib::ObMemAttr(ObModIds::OB_SCHEMA_INDEX_INFO_VEC, ObCtxIds::SCHEMA_SERVICE)),
      lob_meta_infos_(0, NULL, lib::ObMemAttr(ObModIds::OB_SCHEMA_LOB_META_INFO_VEC, ObCtxIds::SCHEMA_SERVICE)),
      lob_piece_infos_(0, NULL, lib::ObMemAttr(ObModIds::OB_SCHEMA_LOB_PIECE_INFO_VEC, ObCtxIds::SCHEMA_SERVICE)),
      table_id_map_(lib::ObMemAttr(ObModIds::OB_SCHEMA_TABLE_ID_MAP, ObCtxIds::SCHEMA_SERVICE)),
      table_name_map_(lib::ObMemAttr(ObModIds::OB_SCHEMA_TABLE_NAME_MAP, ObCtxIds::SCHEMA_SERVICE)),
      normal_index_name_map_(lib::ObMemAttr(ObModIds::OB_SCHEMA_INDEX_NAME_MAP, ObCtxIds::SCHEMA_SERVICE)),
      outline_mgr_(allocator_),
      routine_mgr_(allocator_),
      priv_mgr_(allocator_),
      package_mgr_(allocator_),
      trigger_mgr_(allocator_),
      foreign_key_name_map_(lib::ObMemAttr(ObModIds::OB_SCHEMA_FOREIGN_KEY_NAME_MAP, ObCtxIds::SCHEMA_SERVICE)),
      constraint_name_map_(lib::ObMemAttr(ObModIds::OB_SCHEMA_CONSTRAINT_NAME_MAP, ObCtxIds::SCHEMA_SERVICE)),
      sys_variable_mgr_(allocator_),
      hidden_table_name_map_(lib::ObMemAttr("HiddenTblNames", ObCtxIds::SCHEMA_SERVICE)),
      built_in_index_name_map_(lib::ObMemAttr("BuiltInIdxNames", ObCtxIds::SCHEMA_SERVICE)),
      mock_fk_parent_table_mgr_(allocator_),
      timestamp_in_slot_(0),
      allocator_idx_(OB_INVALID_INDEX),
      ai_model_mgr_(allocator_)
{
}

ObSchemaMgr::~ObSchemaMgr()
{
}

int ObSchemaMgr::init()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(database_name_map_.init())) {
    LOG_WARN("init database name map failed", K(ret));
  } else if (OB_FAIL(table_id_map_.init())) {
    LOG_WARN("init table id map failed", K(ret));
  } else if (OB_FAIL(table_name_map_.init())) {
    LOG_WARN("init table name map failed", K(ret));
  } else if (OB_FAIL(normal_index_name_map_.init())) {
    LOG_WARN("init index name map failed", K(ret));
  } else if (OB_FAIL(foreign_key_name_map_.init())) {
    LOG_WARN("init foreign key name map failed", K(ret));
  } else if (OB_FAIL(constraint_name_map_.init())) {
    LOG_WARN("init constraint name map failed", K(ret));
  } else if (OB_FAIL(outline_mgr_.init())) {
    LOG_WARN("init outline mgr failed", K(ret));
  } else if (OB_FAIL(routine_mgr_.init())) {
    LOG_WARN("init procedure mgr failed", K(ret));
  } else if (OB_FAIL(priv_mgr_.init())) {
    LOG_WARN("init priv mgr failed", K(ret));
  } else if (OB_FAIL(package_mgr_.init())) {
    LOG_WARN("init package mgr failed", K(ret));
  } else if (OB_FAIL(trigger_mgr_.init())) {
    LOG_WARN("init trigger mgr failed", K(ret));
  } else if (OB_FAIL(sys_variable_mgr_.init())) {
    LOG_WARN("init sys variable mgr failed", K(ret));
  } else if (OB_FAIL(hidden_table_name_map_.init())) {
    LOG_WARN("init hidden table name map failed", K(ret));
  } else if (OB_FAIL(built_in_index_name_map_.init())) {
    LOG_WARN("init built in index name map failed", K(ret));
  } else if (OB_FAIL(mock_fk_parent_table_mgr_.init())) {
    LOG_WARN("init mock_fk_parent_table_mgr_ failed", K(ret));
  } else if (OB_FAIL(ai_model_mgr_.init())) {
    LOG_WARN("init ai_model_mgr_ failed", K(ret));
  } else {
    
  }

  return ret;
}

void ObSchemaMgr::reset()
{
  int ret = OB_SUCCESS;

  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    timestamp_in_slot_ = 0;
    schema_version_ = OB_INVALID_VERSION;
    is_consistent_ = true;

    // reset will not free memory for vector
    runtime_info_ = NULL;
    user_infos_.clear();
    database_infos_.clear();
    table_infos_.clear();
    index_infos_.clear();
    lob_meta_infos_.clear();
    lob_piece_infos_.clear();

    database_name_map_.clear();
    table_id_map_.clear();
    table_name_map_.clear();
    normal_index_name_map_.clear();
    foreign_key_name_map_.clear();
    constraint_name_map_.clear();
    outline_mgr_.reset();
    priv_mgr_.reset();
    package_mgr_.reset();
    routine_mgr_.reset();
    trigger_mgr_.reset();
    sys_variable_mgr_.reset();
    hidden_table_name_map_.clear();
    built_in_index_name_map_.clear();
    mock_fk_parent_table_mgr_.reset();
    ai_model_mgr_.reset();
  }
}


int ObSchemaMgr::assign(const ObSchemaMgr &other)
{
  int ret = OB_SUCCESS;
  int64_t start_time = ObTimeUtility::current_time();
  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (this != &other) {
    reset();
    schema_version_ = other.schema_version_;
    
    is_consistent_ = other.is_consistent_;
    #define ASSIGN_FIELD(x)                        \
      if (OB_SUCC(ret)) {                          \
        int64_t start_ts = ObTimeUtility::current_time();   \
        if (OB_FAIL(x.assign(other.x))) {          \
          LOG_WARN("assign " #x "failed", K(ret)); \
        }                                          \
        LOG_INFO("assign "#x" cost", KR(ret),                       \
                 "cost", ObTimeUtility::current_time() - start_ts); \
      }
    // runtime_info_ collapsed to a single slot; mirror ASSIGN_FIELD's shallow
    // pointer-copy semantics (ObSortedVector::assign copies pointers, not pointees).
    runtime_info_ = other.runtime_info_;
    // System variables need to be assigned first
    if (OB_SUCC(ret)) {
      if (OB_FAIL(sys_variable_mgr_.assign(other.sys_variable_mgr_))) {
        LOG_WARN("assign sys variable mgr failed", K(ret));
      }
    }
    ASSIGN_FIELD(user_infos_);
    ASSIGN_FIELD(database_infos_);
    ASSIGN_FIELD(database_name_map_);
    ASSIGN_FIELD(table_infos_);
    ASSIGN_FIELD(index_infos_);
    ASSIGN_FIELD(lob_meta_infos_);
    ASSIGN_FIELD(lob_piece_infos_);
    ASSIGN_FIELD(table_id_map_);
    ASSIGN_FIELD(table_name_map_);
    ASSIGN_FIELD(normal_index_name_map_);
    ASSIGN_FIELD(foreign_key_name_map_);
    ASSIGN_FIELD(constraint_name_map_);
    ASSIGN_FIELD(hidden_table_name_map_);
    ASSIGN_FIELD(built_in_index_name_map_);
    #undef ASSIGN_FIELD
    if (OB_SUCC(ret)) {
      if (OB_FAIL(outline_mgr_.assign(other.outline_mgr_))) {
        LOG_WARN("assign outline mgr failed", K(ret));
      } else if (OB_FAIL(priv_mgr_.assign(other.priv_mgr_))) {
        LOG_WARN("assign priv mgr failed", K(ret));
      } else if (OB_FAIL(routine_mgr_.assign(other.routine_mgr_))) {
        LOG_WARN("assign procedure mgr failed", K(ret));
      } else if (OB_FAIL(package_mgr_.assign(other.package_mgr_))) {
        LOG_WARN("assign package mgr failed", K(ret));
      } else if (OB_FAIL(trigger_mgr_.assign(other.trigger_mgr_))) {
        LOG_WARN("assign trigger mgr failed", K(ret));
      } else if (OB_FAIL(mock_fk_parent_table_mgr_.assign(other.mock_fk_parent_table_mgr_))) {
        LOG_WARN("assign mock_fk_parent_table_mgr_ failed", K(ret));
      } else if (OB_FAIL(ai_model_mgr_.assign(other.ai_model_mgr_))) {
        LOG_WARN("assign ai_model_mgr_ failed", K(ret));
      }
    }
  }
  LOG_INFO("ObSchemaMgr assign cost", KR(ret), "cost", ObTimeUtility::current_time() - start_time);
  return ret;
}

int ObSchemaMgr::deep_copy(const ObSchemaMgr &other)
{
  int ret = OB_SUCCESS;
  int64_t start_time = ObTimeUtility::current_time();
  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (this != &other) {
    reset();
    schema_version_ = other.schema_version_;
    
    is_consistent_ = other.is_consistent_;
    #define ADD_SCHEMA(SCHEMA, SCHEMA_TYPE, SCHEMA_ITER)  \
      if (OB_SUCC(ret)) {                                 \
        int64_t start_ts = ObTimeUtility::current_time(); \
        for (SCHEMA_ITER iter = other.SCHEMA##_infos_.begin();               \
            OB_SUCC(ret) && iter != other.SCHEMA##_infos_.end(); iter++) {   \
          const SCHEMA_TYPE *schema = *iter;                                 \
          if (OB_ISNULL(schema)) {                                           \
            ret = OB_ERR_UNEXPECTED;                                         \
            LOG_WARN("NULL ptr", K(ret), KP(schema));                        \
          } else if (OB_FAIL(add_##SCHEMA(*schema))) {                       \
            LOG_WARN("add "#SCHEMA" failed", K(ret), K(*schema));            \
          }                                                                  \
        }                                                                    \
        LOG_INFO("add "#SCHEMA"s cost", KR(ret),                             \
                 "count", other.SCHEMA##_infos_.count(),                     \
                 "cost", ObTimeUtility::current_time() - start_ts);          \
      }
    // runtime_info_ collapsed to a single slot; mirror ADD_SCHEMA's deep-copy
    // semantics: add_runtime_schema() does an alloc_schema deep copy of the pointee.
    if (OB_SUCC(ret) && OB_NOT_NULL(other.runtime_info_)) {
      if (OB_FAIL(add_runtime_schema(*other.runtime_info_))) {
        LOG_WARN("add runtime schema failed", K(ret), KPC(other.runtime_info_));
      }
    }
    // System variables need to be copied first
    if (OB_SUCC(ret)) {
      if (OB_FAIL(sys_variable_mgr_.deep_copy(other.sys_variable_mgr_))) {
        LOG_WARN("deep copy sys variable mgr failed", K(ret));
      }
    }
    ADD_SCHEMA(user, ObSimpleUserSchema, ConstUserIterator);
    ADD_SCHEMA(database, ObSimpleDatabaseSchema, ConstDatabaseIterator);
    ADD_SCHEMA(table, ObSimpleTableSchemaV2, ConstTableIterator);
    #undef ADD_SCHEMA
    if (OB_SUCC(ret)) {
      if (OB_FAIL(outline_mgr_.deep_copy(other.outline_mgr_))) {
        LOG_WARN("deep copy outline mgr failed", K(ret));
      } else if (OB_FAIL(priv_mgr_.deep_copy(other.priv_mgr_))) {
        LOG_WARN("deep copy priv mgr failed", K(ret));
      } else if (OB_FAIL(routine_mgr_.deep_copy(other.routine_mgr_))) {
        LOG_WARN("deep copy procedure mgr failed", K(ret));
      } else if (OB_FAIL(package_mgr_.deep_copy(other.package_mgr_))) {
        LOG_WARN("deep copy package mgr failed", K(ret));
      } else if (OB_FAIL(trigger_mgr_.deep_copy(other.trigger_mgr_))) {
        LOG_WARN("deep copy trigger mgr failed", K(ret));
      } else if (OB_FAIL(mock_fk_parent_table_mgr_.deep_copy(other.mock_fk_parent_table_mgr_))) {
        LOG_WARN("deep copy mock_fk_parent_table_mgr_ failed", K(ret));
      } else if (OB_FAIL(ai_model_mgr_.deep_copy(other.ai_model_mgr_))) {
        LOG_WARN("deep copy ai_model mgr failed", K(ret));
      }
    }
  }
  LOG_INFO("ObSchemaMgr deep_copy cost", KR(ret), "cost", ObTimeUtility::current_time() - start_time);
  return ret;
}

bool ObSchemaMgr::check_inner_stat() const
{
  bool ret = true;
  return ret;
}

bool ObSchemaMgr::compare_user(const ObSimpleUserSchema *lhs,
                                   const ObSimpleUserSchema *rhs)
{
  return lhs->get_user_id() < rhs->get_user_id();
}

bool ObSchemaMgr::equal_user(const ObSimpleUserSchema *lhs,
                                 const ObSimpleUserSchema *rhs)
{
  return lhs->get_user_id() == rhs->get_user_id();
}

bool ObSchemaMgr::compare_with_user_id(const ObSimpleUserSchema *lhs,
                                                 const ObUserId &user_id)
{
  return NULL != lhs ? (lhs->get_user_id() < user_id.user_id_) : false;
}

bool ObSchemaMgr::equal_with_user_id(const ObSimpleUserSchema *lhs,
                                                const ObUserId &user_id)
{
  return NULL != lhs ? (lhs->get_user_id() == user_id.user_id_) : false;
}

bool ObSchemaMgr::compare_database(const ObSimpleDatabaseSchema *lhs,
                                   const ObSimpleDatabaseSchema *rhs)
{
  return lhs->get_database_id() < rhs->get_database_id();
}

bool ObSchemaMgr::equal_database(const ObSimpleDatabaseSchema *lhs,
                                 const ObSimpleDatabaseSchema *rhs)
{
  return lhs->get_database_id() == rhs->get_database_id();
}

bool ObSchemaMgr::compare_with_database_id(const ObSimpleDatabaseSchema *lhs,
                                                 const ObDatabaseId &database_id)
{
  return NULL != lhs ? (lhs->get_database_id() < database_id.database_id_) : false;
}

bool ObSchemaMgr::equal_with_database_id(const ObSimpleDatabaseSchema *lhs,
                                                const ObDatabaseId &database_id)
{
  return NULL != lhs ? (lhs->get_database_id() == database_id.database_id_) : false;
}

bool ObSchemaMgr::compare_table(const ObSimpleTableSchemaV2 *lhs,
                                const ObSimpleTableSchemaV2 *rhs)
{
  return lhs->get_table_id() < rhs->get_table_id();
}

bool ObSchemaMgr::compare_aux_table(const ObSimpleTableSchemaV2 *lhs,
                                    const ObSimpleTableSchemaV2 *rhs)
{
  bool ret = lhs->get_data_table_key() < rhs->get_data_table_key();
  if (lhs->get_data_table_key() == rhs->get_data_table_key()) {
    ret = lhs->get_table_id() < rhs->get_table_id();
  }
  return ret;
}

bool ObSchemaMgr::equal_table(const ObSimpleTableSchemaV2 *lhs,
                              const ObSimpleTableSchemaV2 *rhs)
{
  return lhs->get_table_id() == rhs->get_table_id();
}

bool ObSchemaMgr::compare_with_table_id(const ObSimpleTableSchemaV2 *lhs,
                                               const ObTableId &table_id)
{
  return NULL != lhs ? (lhs->get_table_id() < table_id.table_id_) : false;
}

bool ObSchemaMgr::compare_with_data_table_id(const ObSimpleTableSchemaV2 *lhs,
                                             const ObTableId &table_id)
{
  return NULL != lhs ? (lhs->get_data_table_key() < table_id) : false;
}

bool ObSchemaMgr::equal_with_table_id(const ObSimpleTableSchemaV2 *lhs,
                                             const ObTableId &table_id)
{
  return NULL != lhs ? (lhs->get_table_id() == table_id.table_id_) : false;
}


int ObSchemaMgr::add_runtime_schemas(const ObIArray<ObSimpleServerRuntimeSchema> &runtime_schemas)
{
  int ret = OB_SUCCESS;

  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    FOREACH_CNT_X(runtime_schema, runtime_schemas, OB_SUCC(ret)) {
      if (OB_FAIL(add_runtime_schema(*runtime_schema))) {
        LOG_WARN("add runtime schema failed", K(ret),
                 "runtime_schema", *runtime_schema);
      }
    }
  }

  return ret;
}


int ObSchemaMgr::add_runtime_schema(const ObSimpleServerRuntimeSchema &runtime_schema)
{
  int ret = OB_SUCCESS;

  ObSimpleServerRuntimeSchema *new_runtime_schema = NULL;
  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!runtime_schema.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(runtime_schema));
  } else if (OB_FAIL(ObSchemaUtils::alloc_schema(allocator_, runtime_schema, new_runtime_schema))) {
    LOG_WARN("alloc schema failed", K(ret));
  } else if (OB_ISNULL(new_runtime_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", K(ret), K(new_runtime_schema));
  } else {
    runtime_info_ = new_runtime_schema;
    LOG_INFO("add runtime schema", K(ret), K(runtime_schema));
  }

  return ret;
}


int ObSchemaMgr::get_package_schema(
    const uint64_t package_id,
    const ObSimplePackageSchema *&package_schema) const
{
  int ret = OB_SUCCESS;
  {
    ret = package_mgr_.get_package_schema(package_id, package_schema);
  }
  return ret;
}
int ObSchemaMgr::get_routine_schema(
    const uint64_t routine_id,
    const ObSimpleRoutineSchema *&routine_schema) const
{
  int ret = OB_SUCCESS;
  {
    ret = routine_mgr_.get_routine_schema(routine_id, routine_schema);
  }
  return ret;
}
int ObSchemaMgr::get_trigger_schema(
    const uint64_t trigger_id,
    const ObSimpleTriggerSchema *&trigger_schema) const
{
  int ret = OB_SUCCESS;
  {
    ret = trigger_mgr_.get_trigger_schema(trigger_id, trigger_schema);
  }
  return ret;
}
int ObSchemaMgr::get_server_runtime_schema(
    const ObSimpleServerRuntimeSchema *&runtime_schema) const
{
  int ret = OB_SUCCESS;
  runtime_schema = NULL;

  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (NULL == runtime_info_) {
    // do-nothing
  } else {
    runtime_schema = runtime_info_;
  }

  return ret;
}

int ObSchemaMgr::get_server_runtime_schema(
  const ObString &runtime_name,
  const ObSimpleServerRuntimeSchema *&runtime_schema) const
{
  int ret = OB_SUCCESS;
  runtime_schema = NULL;

  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (runtime_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(runtime_name));
  } else if (NULL == runtime_info_) {
    // do-nothing
  } else if (runtime_info_->get_runtime_name_str() != runtime_name) {
    // do-nothing, name mismatch
  } else {
    runtime_schema = runtime_info_;
  }

  return ret;
}

int ObSchemaMgr::add_users(const ObIArray<ObSimpleUserSchema> &user_schemas)
{
  int ret = OB_SUCCESS;
  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    FOREACH_CNT_X(user_schema, user_schemas, OB_SUCC(ret)) {
      if (OB_FAIL(add_user(*user_schema))) {
        LOG_WARN("add user failed", K(ret),
            "user_schema", *user_schema);
      }
    }
  }
  return ret;
}

// NOT USED

int ObSchemaMgr::add_user(const ObSimpleUserSchema &user_schema)
{
  int ret = OB_SUCCESS;

  const ObSimpleServerRuntimeSchema *runtime_schema = NULL;
  ObSimpleUserSchema *new_user_schema = NULL;
  UserIterator iter = NULL;
  ObSimpleUserSchema *replaced_user = NULL;
  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!user_schema.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(user_schema));
  } else if (OB_FAIL(get_server_runtime_schema( runtime_schema))) {
    LOG_WARN("get runtime schema failed", K(ret));
  } else if (OB_ISNULL(runtime_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", K(ret), K(runtime_schema));
  } else if (OB_FAIL(ObSchemaUtils::alloc_schema(allocator_, user_schema, new_user_schema))) {
    LOG_WARN("alloc schema failed", K(ret));
  } else if (OB_ISNULL(new_user_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", K(ret), K(new_user_schema));
  } else if (OB_FAIL(user_infos_.replace(new_user_schema,
                                         iter,
                                         compare_user,
                                         equal_user,
                                         replaced_user))) {
    LOG_WARN("failed to add user schema", K(ret));
  } else {
  }

  return ret;
}

int ObSchemaMgr::del_user(const ObUserId user)
{
  int ret = OB_SUCCESS;

  const ObSimpleServerRuntimeSchema *runtime_schema = NULL;
  ObSimpleUserSchema *schema_to_del = NULL;
  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!user.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(user));
  } else if (OB_FAIL(get_server_runtime_schema( runtime_schema))) {
    LOG_WARN("get runtime schema failed", K(ret));
  } else if (OB_ISNULL(runtime_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", K(ret), K(runtime_schema));
  } else if (OB_FAIL(user_infos_.remove_if(user,
                                           compare_with_user_id,
                                           equal_with_user_id,
                                           schema_to_del))) {
    LOG_WARN("failed to remove user schema, ",
             "user_id",
             user.user_id_,
             K(ret));
  } else if (OB_ISNULL(schema_to_del)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("removed user schema return NULL, ",
             "user_id",
             user.user_id_,
             K(ret));
  }
  return ret;
}

int ObSchemaMgr::get_user_schema(
    const uint64_t user_id,
    const ObSimpleUserSchema *&user_schema) const
{
  int ret = OB_SUCCESS;
  user_schema = NULL;

  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_INVALID_ID == user_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(user_id));
  } else {
    ObSimpleUserSchema *tmp_schema = NULL;
    ObUserId user_id_lower(user_id);
    ConstUserIterator iter =
        user_infos_.lower_bound(user_id_lower, compare_with_user_id);
    if (iter == user_infos_.end()) {
      // do-nothing
    } else if (OB_ISNULL(tmp_schema = *iter)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("NULL ptr", K(tmp_schema), K(ret));
    } else if (user_id != tmp_schema->get_user_id()) {
      // do-nothing
    } else {
      user_schema = tmp_schema;
    }
  }

  return ret;
}

int ObSchemaMgr::get_user_schema(
  const ObString &user_name,
  const ObString &host_name,
  const ObSimpleUserSchema *&user_schema) const
{
  int ret = OB_SUCCESS;
  user_schema = NULL;

  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ObUserId user_id_lower(OB_MIN_ID);
    const ObSimpleUserSchema *tmp_schema = NULL;
    ConstUserIterator iter =
        user_infos_.lower_bound(user_id_lower, compare_with_user_id);
    bool is_stop = false;
    for (; OB_SUCC(ret) && iter != user_infos_.end() && !is_stop; iter++) {
      if (OB_ISNULL(tmp_schema = *iter)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("NULL ptr", K(tmp_schema), K(ret));
      } else if (tmp_schema->get_user_name_str() != user_name) {
        // do-nothing
      } else if (tmp_schema->get_host_name_str() != host_name) {
        // do-nothing
      } else {
        user_schema = tmp_schema;
        is_stop = true;
      }
    }
  }

  return ret;
}

int ObSchemaMgr::get_user_schema(
                                const ObString &user_name,
                                ObIArray<const ObSimpleUserSchema *> &users_schema) const
{
  int ret = OB_SUCCESS;
  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ObUserId user_id_lower(OB_MIN_ID);
    const ObSimpleUserSchema *tmp_schema = NULL;
    ConstUserIterator iter = user_infos_.lower_bound(user_id_lower, compare_with_user_id);
    bool is_stop = false;
    for (; OB_SUCC(ret) && iter != user_infos_.end() && !is_stop; iter++) {
      if (OB_ISNULL(tmp_schema = *iter)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("NULL ptr", K(tmp_schema), K(ret));
      } else if (tmp_schema->get_user_name_str() != user_name) {
        // do-nothing
      } else if (OB_FAIL(users_schema.push_back(tmp_schema))) {
        LOG_WARN("failed to push back user schema", K(tmp_schema), K(ret));
      } else {
        tmp_schema = NULL;;
      }
    }
  }

  return ret;
}

int ObSchemaMgr::add_databases(const ObIArray<ObSimpleDatabaseSchema> &database_schemas)
{
  int ret = OB_SUCCESS;

  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    FOREACH_CNT_X(database_schema, database_schemas, OB_SUCC(ret)) {
      if (OB_FAIL(add_database(*database_schema))) {
        LOG_WARN("add database failed", K(ret),
                 "database_schema", *database_schema);
      }
    }
  }

  return ret;
}


int ObSchemaMgr::add_database(const ObSimpleDatabaseSchema &db_schema)
{
  int ret = OB_SUCCESS;

  const ObSimpleServerRuntimeSchema *runtime_schema = NULL;
  ObSimpleDatabaseSchema *new_db_schema = NULL;
  DatabaseIterator db_iter = NULL;
  ObSimpleDatabaseSchema *replaced_db = NULL;
  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!db_schema.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(db_schema));
  } else if (OB_FAIL(get_server_runtime_schema( runtime_schema))) {
    LOG_WARN("get runtime schema failed", K(ret));
  } else if (OB_ISNULL(runtime_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", K(ret), K(runtime_schema));
  }

  ObNameCaseMode mode = OB_NAME_CASE_INVALID;
  if (OB_SUCC(ret)) {
    if (OB_FAIL(get_runtime_name_case_mode(mode))) {
      LOG_WARN("fail to get_runtime_name_case_mode", K(ret));
    } else if (OB_NAME_CASE_INVALID == mode) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid case mode", K(ret), K(mode));
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ObSchemaUtils::alloc_schema(allocator_, db_schema, new_db_schema))) {
    LOG_WARN("alloc schema failed", K(ret));
  } else if (OB_ISNULL(new_db_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", K(ret), K(new_db_schema));
  } else if (FALSE_IT(new_db_schema->set_name_case_mode(mode))) {
    // will not reach here
  } else if (OB_FAIL(database_infos_.replace(new_db_schema,
                                             db_iter,
                                             compare_database,
                                             equal_database,
                                             replaced_db))) {
    LOG_WARN("failed to add db schema", K(ret));
  }
  if (OB_FAIL(ret)) {
  } else if (NULL == replaced_db) {
    //do-nothing
  } else if (OB_FAIL(deal_with_db_rename(*replaced_db, *new_db_schema))) {
    LOG_WARN("failed to deal with rename", K(ret));
  }
  if (OB_SUCC(ret)) {
    ObDatabaseSchemaHashWrapper database_name_wrapper(new_db_schema->get_name_case_mode(),
                                                      new_db_schema->get_database_name_str());
    int over_write = 1;
    int hash_ret = database_name_map_.set_refactored(database_name_wrapper, new_db_schema, over_write);
    if (OB_SUCCESS != hash_ret && OB_HASH_EXIST != hash_ret) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("build database name hashmap failed", K(ret), K(hash_ret),
               "database_name", new_db_schema->get_database_name());
    }
  }

  return ret;
}

int ObSchemaMgr::del_database(const ObDatabaseId database)
{
  int ret = OB_SUCCESS;

  const ObSimpleServerRuntimeSchema *runtime_schema = NULL;
  ObSimpleDatabaseSchema *schema_to_del = NULL;
  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!database.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(database));
  } else if (OB_FAIL(get_server_runtime_schema( runtime_schema))) {
    LOG_WARN("get runtime schema failed", K(ret));
  } else if (OB_ISNULL(runtime_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", K(ret), K(runtime_schema));
  }

  ObNameCaseMode mode = OB_NAME_CASE_INVALID;
  if (OB_SUCC(ret)) {
    if (OB_FAIL(get_runtime_name_case_mode(mode))) {
      LOG_WARN("fail to get_runtime_name_case_mode", K(ret));
    } else if (OB_NAME_CASE_INVALID == mode) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid case mode", K(ret), K(mode));
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(database_infos_.remove_if(database,
                                               compare_with_database_id,
                                               equal_with_database_id,
                                               schema_to_del))) {
    LOG_WARN("failed to remove db schema, ",
             "database_id",
             database.database_id_,
             K(ret));
  } else if (OB_ISNULL(schema_to_del)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("removed db schema return NULL, ",
             "database_id",
             database.database_id_,
             K(ret));
  } else {
    ObDatabaseSchemaHashWrapper database_name_wrapper(mode,
                                                      schema_to_del->get_database_name_str());
    int hash_ret = database_name_map_.erase_refactored(database_name_wrapper);
    if (OB_SUCCESS != hash_ret) {
      LOG_WARN("failed delete database from database name hashmap",
               K(ret),
               K(hash_ret),
               "database_name", schema_to_del->get_database_name());
      // Increase the fault-tolerant processing of incremental schema refresh, no error is reported at this time,
      // and the solution is solved by rebuild logic
      ret = OB_HASH_NOT_EXIST != hash_ret ? hash_ret : ret;
    }
  }
  // ignore ret
  if (database_infos_.count() != database_name_map_.item_count()) {
    LOG_WARN("database info is non-consistent",
             "database_infos_count",
             database_infos_.count(),
             "database_name_map_item_count",
             database_name_map_.item_count(),
             "database_id",
             database.database_id_);
  }

  return ret;
}

int ObSchemaMgr::get_database_schema(
    const uint64_t database_id,
    const ObSimpleDatabaseSchema *&database_schema) const
{
  int ret = OB_SUCCESS;
  database_schema = NULL;

  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_INVALID_ID == database_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(database_id));
  } else {
    ObSimpleDatabaseSchema *tmp_schema = NULL;
    ObDatabaseId database_id_lower(database_id);
    ConstDatabaseIterator database_iter =
        database_infos_.lower_bound(database_id_lower, compare_with_database_id);
    if (database_iter == database_infos_.end()) {
      // do-nothing
    } else if (OB_ISNULL(tmp_schema = *database_iter)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("NULL ptr", K(tmp_schema), K(ret));
    } else if (database_id != tmp_schema->get_database_id()) {
      // do-nothing
    } else {
      database_schema = tmp_schema;
    }
  }

  return ret;
}

int ObSchemaMgr::get_database_schema(
  const ObString &database_name,
  const ObSimpleDatabaseSchema *&database_schema) const
{
  int ret = OB_SUCCESS;
  database_schema = NULL;

  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (database_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(database_name));
  } else {
    ObSimpleDatabaseSchema *tmp_schema = NULL;
    ObNameCaseMode mode = OB_NAME_CASE_INVALID;
    if (OB_SUCC(ret)) {
      if (OB_FAIL(get_runtime_name_case_mode(mode))) {
        LOG_WARN("fail to get_runtime_name_case_mode", K(ret));
      } else if (OB_NAME_CASE_INVALID == mode) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid case mode", K(ret), K(mode));
      }
    }
    if (OB_SUCC(ret)) {
      const ObDatabaseSchemaHashWrapper database_name_wrapper(mode, database_name);
      int hash_ret = database_name_map_.get_refactored(database_name_wrapper, tmp_schema);
      if (OB_SUCCESS == hash_ret) {
        if (OB_ISNULL(tmp_schema)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("NULL ptr", K(ret), K(tmp_schema));
        } else {
          database_schema = tmp_schema;
        }
      }
    }
  }

  return ret;
}

int ObSchemaMgr::add_tables(
    const ObIArray<ObSimpleTableSchemaV2 *> &table_schemas,
    const bool refresh_full_schema/*= false*/)
{
  int ret = OB_SUCCESS;
  int64_t start_time = ObTimeUtility::current_time();
  static const int64_t STAGE_CNT = 5;
  int64_t cost_time_array[STAGE_CNT] = {0};
  ObArrayWrap<int64_t> cost_array(cost_time_array, STAGE_CNT);
  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (refresh_full_schema && OB_FAIL(reserved_mem_for_tables_(table_schemas))) {
    LOG_WARN("fail to reserved mem for tables", KR(ret));
  } else {
    bool desc_order = true;
    if (OB_SUCC(ret) && table_schemas.count() >= 2) {
      if (OB_ISNULL(table_schemas.at(0)) || OB_ISNULL(table_schemas.at(1))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table is null", KR(ret), KP(table_schemas.at(0)), K(table_schemas.at(1)));
      } else {
        // 1. when refresh user simple table schemas, table_schemas will be sorted in desc order by sql.
        // 2. when broadcast schema or refresh core/system tables or other situations, table_schemas will be sorted in asc order.
        // Because table_infos_ are sorted in asc order, we should also add table in asc order to reduce performance lost.
        // Normally, we consider table_schemas are in desc order in most situations.
        desc_order = table_schemas.at(0)->get_table_id() > table_schemas.at(1)->get_table_id();
      }
    }

    if (OB_SUCC(ret)) {
      if (desc_order) {
        for (int64_t i = table_schemas.count() - 1; OB_SUCC(ret) && i >= 0; i--) {
          const ObSimpleTableSchemaV2 *table = table_schemas.at(i);
          if (OB_ISNULL(table)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("table is null", KR(ret), K(i));
          } else if (OB_FAIL(add_table(*table, &cost_array))) {
            LOG_WARN("add table failed", KR(ret), KPC(table));
          }
        } // end for
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < table_schemas.count(); i++) {
          const ObSimpleTableSchemaV2 *table = table_schemas.at(i);
          if (OB_ISNULL(table)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("table is null", KR(ret), K(i));
          } else if (OB_FAIL(add_table(*table, &cost_array))) {
            LOG_WARN("add table failed", KR(ret), KPC(table));
          }
        } // end for
      }
    }
  }
  FLOG_INFO("add tables", KR(ret),
            "stage_cost", cost_array,
            "cost", ObTimeUtility::current_time() - start_time);
  return ret;
}

int ObSchemaMgr::reserved_mem_for_tables_(
    const ObIArray<ObSimpleTableSchemaV2*> &table_schemas)
{
  int ret = OB_SUCCESS;
  int64_t start_time = ObTimeUtility::current_time();
  const int64_t table_cnt = table_schemas.count();
  int64_t index_cnt = 0;
  int64_t lob_meta_cnt = 0;
  int64_t lob_piece_cnt = 0;
  int64_t hidden_table_cnt = 0;
  int64_t other_table_cnt = 0;
  int64_t fk_cnt = 0;
  int64_t cst_cnt = 0;
  const int64_t OBJECT_SIZE = sizeof(void*);
  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_FAIL(table_infos_.reserve(table_cnt))) {
    LOG_WARN("fail to reserved array", KR(ret), K(table_cnt));
  } else {
    //(void) table_id_map_.set_sub_map_mem_size(table_cnt * OBJECT_SIZE);

    for (int64_t i = 0; OB_SUCC(ret) && i < table_schemas.count(); i++) {
      const ObSimpleTableSchemaV2 *table = table_schemas.at(i);
      if (OB_ISNULL(table)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table is null", KR(ret), K(i));
      } else {
        if (table->is_index_table()) {
          index_cnt++;
        } else if (table->is_aux_lob_meta_table()) {
          lob_meta_cnt++;
        } else if (table->is_aux_lob_piece_table()) {
          lob_piece_cnt++;
        } else if (table->is_user_hidden_table()) {
          hidden_table_cnt++;
        } else {
          other_table_cnt++;
        }

        if (table->is_table()
            && !table->is_user_hidden_table()) {
          fk_cnt += table->get_simple_foreign_key_info_array().count();
        }

        if (table->is_table()
            && !table->is_user_hidden_table()
            && !table->is_mysql_tmp_table()) {
          cst_cnt += table->get_simple_constraint_info_array().count();
        }
      }
    } // end for

    if (OB_SUCC(ret) && index_cnt > 0) {
      if (OB_FAIL(index_infos_.reserve(index_cnt))) {
        LOG_WARN("fail to reserved array", KR(ret), K(index_cnt));
      } else {
        //(void) index_name_map_.set_sub_map_mem_size(index_cnt * OBJECT_SIZE);
      }
    }

    if (OB_SUCC(ret) && lob_meta_cnt > 0) {
      if (OB_FAIL(lob_meta_infos_.reserve(lob_meta_cnt))) {
        LOG_WARN("fail to reserved array", KR(ret), K(lob_meta_cnt));
      }
    }

    if (OB_SUCC(ret) && lob_piece_cnt > 0) {
      if (OB_FAIL(lob_piece_infos_.reserve(lob_piece_cnt))) {
        LOG_WARN("fail to reserved array", KR(ret), K(lob_piece_cnt));
      }
    }

    if (OB_SUCC(ret) && other_table_cnt > 0) {
      //(void) table_name_map_.set_sub_map_mem_size(other_table_cnt * OBJECT_SIZE);
    }

    if (OB_SUCC(ret) && fk_cnt > 0) {
      //(void) foreign_key_name_map_.set_sub_map_mem_size(fk_cnt * OBJECT_SIZE);
    }

    if (OB_SUCC(ret) && cst_cnt > 0) {
      //(void) constraint_name_map_.set_sub_map_mem_size(cst_cnt * OBJECT_SIZE);
    }

  }
  FLOG_INFO("reserve mem", KR(ret),
            K(table_cnt), K(index_cnt),
            K(lob_meta_cnt), K(lob_piece_cnt),
            K(hidden_table_cnt),
            K(other_table_cnt), K(fk_cnt), K(cst_cnt),
            "cost", ObTimeUtility::current_time() - start_time);
  return ret;
}



int ObSchemaMgr::add_table(
    const ObSimpleTableSchemaV2 &table_schema,
    common::ObArrayWrap<int64_t> *cost_array /*= NULL*/)
{
  int ret = OB_SUCCESS;

  const ObSimpleServerRuntimeSchema *runtime_schema = NULL;
  ObSimpleTableSchemaV2 *new_table_schema = NULL;
  TableIterator iter = NULL;
  ObSimpleTableSchemaV2 *replaced_table = NULL;
  const uint64_t table_id = table_schema.get_table_id();
  bool is_runtime_space_table = false;
  int64_t idx = 0;
  if (OB_ALL_CORE_TABLE_TID == table_schema.get_table_id()) {
    FLOG_INFO("add __all_core_table schema", KR(ret), K(table_schema), K(lbt()));
  }

  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!table_schema.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(table_schema));
  } else if (OB_FAIL(get_server_runtime_schema( runtime_schema))) {
    LOG_WARN("get runtime schema failed", K(ret));
  } else if (OB_ISNULL(runtime_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", K(ret), K(runtime_schema));
  }

  ObNameCaseMode mode = OB_NAME_CASE_INVALID;
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ObSysTableChecker::is_runtime_space_table_id(table_id, is_runtime_space_table))) {
    LOG_WARN("fail to check if table_id is in runtime space", K(ret), K(table_id));
  } else if (OB_FAIL(get_runtime_name_case_mode(mode))) {
    LOG_WARN("fail to get_runtime_name_case_mode",  K(ret));
  } else if (OB_NAME_CASE_INVALID == mode) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid case mode", K(ret), K(mode));
  }

  int64_t start_time = ObTimeUtility::current_time();
  if (OB_FAIL(ret)){
  } else if (OB_FAIL(ObSchemaUtils::alloc_schema(allocator_, table_schema, new_table_schema))) {
    LOG_WARN("alloc schema failed", K(ret));
  } else if (OB_ISNULL(new_table_schema) || !new_table_schema->is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", K(ret), K(new_table_schema));
  }
  if (OB_NOT_NULL(cost_array) && idx < cost_array->count()) {
    cost_array->at(idx++) += ObTimeUtility::current_time() - start_time;
  }

  start_time = ObTimeUtility::current_time();
  if (OB_FAIL(ret)) {
  } else if (FALSE_IT(new_table_schema->set_name_case_mode(mode))) {
    // will not reach here
  } else if (OB_FAIL(table_infos_.replace(new_table_schema,
                                          iter,
                                          compare_table,
                                          equal_table,
                                          replaced_table))) {
    LOG_WARN("failed to add table schema", K(ret));
  } else if (new_table_schema->is_index_table()) {
    ObSimpleTableSchemaV2 *replaced_index_table = NULL;
    if (OB_FAIL(index_infos_.replace(new_table_schema,
                                     iter,
                                     compare_aux_table,
                                     equal_table,
                                     replaced_index_table))) {
      LOG_WARN("failed to add index schema", K(ret));
    }
  } else if (new_table_schema->is_aux_lob_meta_table()) {
    ObSimpleTableSchemaV2 *replaced_lob_meta_table = NULL;
    if (OB_FAIL(lob_meta_infos_.replace(new_table_schema,
                                        iter,
                                        compare_aux_table,
                                        equal_table,
                                        replaced_lob_meta_table))) {
      LOG_WARN("failed to add lob meta schema", K(ret));
    }
  } else if (new_table_schema->is_aux_lob_piece_table()) {
    ObSimpleTableSchemaV2 *replaced_lob_piece_table = NULL;
    if (OB_FAIL(lob_piece_infos_.replace(new_table_schema,
                                         iter,
                                         compare_aux_table,
                                         equal_table,
                                         replaced_lob_piece_table))) {
      LOG_WARN("failed to add lob piece schema", K(ret));
    }
  }
  if (OB_NOT_NULL(cost_array) && idx < cost_array->count()) {
    cost_array->at(idx++) += ObTimeUtility::current_time() - start_time;
  }

  start_time = ObTimeUtility::current_time();
  if (OB_SUCC(ret)) {
    if (NULL == replaced_table) {
      // do-nothing
    } else if (OB_FAIL(deal_with_table_rename(*replaced_table, *new_table_schema))) {
      LOG_WARN("failed to deal with rename", K(ret));
    } else if (OB_FAIL(deal_with_change_table_state(*replaced_table, *new_table_schema))) {
      LOG_WARN("failed to deal with change table state", K(ret));
    }
  }
  if (OB_NOT_NULL(cost_array) && idx < cost_array->count()) {
    cost_array->at(idx++) += ObTimeUtility::current_time() - start_time;
  }

  if (OB_SUCC(ret)) {
    start_time = ObTimeUtility::current_time();
    int over_write = 1;
    int hash_ret = table_id_map_.set_refactored(new_table_schema->get_table_id(),
                                     new_table_schema,
                                     over_write);
    if (OB_NOT_NULL(cost_array) && idx < cost_array->count()) {
      cost_array->at(idx++) += ObTimeUtility::current_time() - start_time;
    }

    start_time = ObTimeUtility::current_time();
    if (OB_SUCCESS != hash_ret && OB_HASH_EXIST != hash_ret) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("build table id hashmap failed", K(ret), K(hash_ret),
               "table_id", new_table_schema->get_table_id());
    } else {
      if (new_table_schema->is_user_hidden_table()) { // hidden table will not be added to the map
        ObTableSchemaHashWrapper table_name_wrapper(new_table_schema->get_database_id(),
                                                    new_table_schema->get_session_id(),
                                                    new_table_schema->get_name_case_mode(),
                                                    new_table_schema->get_table_name_str());
        hash_ret = hidden_table_name_map_.set_refactored(table_name_wrapper, new_table_schema,
                                                         over_write);
        if (OB_SUCCESS != hash_ret && OB_HASH_EXIST != hash_ret) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("build hidden table name hashmap failed", K(ret), K(hash_ret),
                   "table_id", new_table_schema->get_table_id(),
                   "table_name", new_table_schema->get_table_name());
        }
      } else if (new_table_schema->is_index_table()) { // index is in recyclebin
        const bool is_built_in_index = new_table_schema->is_built_in_index();
        IndexNameMap &index_name_map = get_index_name_map_(is_built_in_index);
        if (new_table_schema->is_in_recyclebin()) {
          ObIndexSchemaHashWrapper index_name_wrapper(new_table_schema->get_database_id(),
                                                      common::OB_INVALID_ID,
                                                      new_table_schema->get_table_name_str());
          hash_ret = index_name_map.set_refactored(index_name_wrapper, new_table_schema, over_write);
          if (OB_SUCCESS != hash_ret && OB_HASH_EXIST != hash_ret) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("build index name hashmap failed", K(ret), K(hash_ret), K(is_built_in_index),
                     "table_id", new_table_schema->get_table_id(),
                     "index_name", new_table_schema->get_table_name());
          }
        } else { // index is not in recyclebin
          if (OB_FAIL(new_table_schema->generate_origin_index_name())) {
            LOG_WARN("generate origin index name failed", K(ret), K(new_table_schema->get_table_name_str()));
          } else {
            ObIndexSchemaHashWrapper cutted_index_name_wrapper(new_table_schema->get_database_id(),
                                                               new_table_schema->get_data_table_id(),
                                                               new_table_schema->get_origin_index_name_str());
            hash_ret = index_name_map.set_refactored(cutted_index_name_wrapper, new_table_schema, over_write);
            if (OB_SUCCESS != hash_ret && OB_HASH_EXIST != hash_ret) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("build index name hashmap failed", K(ret), K(hash_ret), K(is_built_in_index),
                       K(new_table_schema->get_table_id()),
                       K(new_table_schema->get_data_table_id()),
                       K(new_table_schema->get_origin_index_name_str()));
            }
          }
        }
      } else if (new_table_schema->is_aux_lob_table()) {
        // do nothing
      } else {
        ObTableSchemaHashWrapper table_name_wrapper(new_table_schema->get_database_id(),
                                                    new_table_schema->get_session_id(),
                                                    new_table_schema->get_name_case_mode(),
                                                    new_table_schema->get_table_name_str());
        hash_ret = table_name_map_.set_refactored(table_name_wrapper, new_table_schema, over_write);
        if (OB_SUCCESS != hash_ret && OB_HASH_EXIST != hash_ret) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("build table name hashmap failed", K(ret), K(hash_ret),
                   "table_id", new_table_schema->get_table_id(),
                   "table_name", new_table_schema->get_table_name());
        }
      }
      if (OB_SUCC(ret) && new_table_schema->is_table()) {
        if (NULL != replaced_table) {
          if (!replaced_table->is_user_hidden_table()
              && new_table_schema->is_user_hidden_table()) {
            if (OB_FAIL(delete_foreign_keys_in_table(*replaced_table))) {
              LOG_WARN("delete foreign keys info from a hash map failed",
              K(ret), K(*replaced_table));
            }
          // deal with the situation that alter table drop fk and truncate table enter the recycle bin,
          // and delete the foreign key information dropped from the hash map
          // First delete the foreign key information on the table from the hash map when truncate table,
          // and add it back when rebuild_table_hashmap
          } else if (OB_FAIL(check_and_delete_given_fk_in_table(replaced_table, new_table_schema))) {
            LOG_WARN("check and delete given fk in table failed", K(ret), K(*replaced_table), K(*new_table_schema));
          }
        }
        if (OB_SUCC(ret) && !new_table_schema->is_user_hidden_table()) {
          if (OB_FAIL(add_foreign_keys_in_table(new_table_schema->get_simple_foreign_key_info_array(), 1 /*over_write*/))) {
            LOG_WARN("add foreign keys info to a hash map failed", K(ret), K(*new_table_schema));
          } else {
            // do nothing
          }
        }
      }
      if (OB_SUCC(ret) && new_table_schema->is_table()) {
        // In mysql mode, check constraints in non-temporary tables don't share namespace with constraints in temporary tables
        if (NULL != replaced_table) {
          if (!replaced_table->is_user_hidden_table()
              && new_table_schema->is_user_hidden_table()) {
            if (OB_FAIL(delete_constraints_in_table(*replaced_table))) {
              LOG_WARN("delete constraint info from a hash map failed",
              K(ret), K(*replaced_table));
            }
          // deal with the situation that alter table drop cst and truncate table enter the recycle bin,
          // delete the constraint information dropped from the hash map
          // When truncate table, delete the constraint information on the table from the hash map first,
          // and add it back when rebuild_table_hashmap
          } else if (OB_FAIL(check_and_delete_given_cst_in_table(replaced_table, new_table_schema))) {
            LOG_WARN("check and delete given cst in table failed", K(ret), K(*replaced_table), K(*new_table_schema));
          }
        }
        if (OB_SUCC(ret) && !new_table_schema->is_user_hidden_table()) {
          if (OB_FAIL(add_constraints_in_table(new_table_schema, 1 /*over_write*/))) {
            LOG_WARN("add foreign keys info to a hash map failed", K(ret), K(*new_table_schema));
          } else {
            // do nothing
          }
        }
      }
    }
    if (OB_NOT_NULL(cost_array) && idx < cost_array->count()) {
      cost_array->at(idx++) += ObTimeUtility::current_time() - start_time;
    }
  }

  return ret;
}

// Used to add all foreign key information in a table to the member variable ForeignKeyNameMap of ObSchemaMgr
int ObSchemaMgr::add_foreign_keys_in_table(
    const ObIArray<ObSimpleForeignKeyInfo> &fk_info_array,
    const int over_write)
{
  int ret = OB_SUCCESS;

  if (fk_info_array.empty()) {
    // If there is no foreign key in the table, do nothing
  } else {
    FOREACH_CNT_X(simple_foreign_key_info, fk_info_array, OB_SUCC(ret)) {
      ObForeignKeyInfoHashWrapper foreign_key_name_wrapper(simple_foreign_key_info->database_id_,
                                                           simple_foreign_key_info->foreign_key_name_);
      int hash_ret = foreign_key_name_map_.set_refactored(foreign_key_name_wrapper,
                                                          const_cast<ObSimpleForeignKeyInfo*> (simple_foreign_key_info),
                                                          over_write);
      if (OB_SUCCESS != hash_ret) {
        ret = OB_HASH_EXIST == hash_ret ? OB_SUCCESS : OB_ERR_UNEXPECTED;
        LOG_ERROR("build fk name hashmap failed", K(ret), K(hash_ret),
                  "fk_id", simple_foreign_key_info->foreign_key_id_,
                  "fk_name", simple_foreign_key_info->foreign_key_name_);
      }
    }
  }

  return ret;
}

// According to table_schema and foreign key name, delete the specified foreign key related to the corresponding table_schema
int ObSchemaMgr::delete_given_fk_from_mgr(const ObSimpleForeignKeyInfo &fk_info)
{
  int ret = OB_SUCCESS;

  if (fk_info.database_id_ == common::OB_INVALID_ID
      || fk_info.foreign_key_name_.empty()){
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fk_info should not be null", K(ret), K(fk_info));
  } else {
    ObForeignKeyInfoHashWrapper foreign_key_name_wrapper(fk_info.database_id_,
                                                         fk_info.foreign_key_name_);
    int hash_ret = foreign_key_name_map_.erase_refactored(foreign_key_name_wrapper);
    if (OB_HASH_NOT_EXIST == hash_ret) {
      // Because there is no guarantee to refresh in strict accordance with the version order of the schema version,
      // the return value of OB_HASH_NOT_EXIST is reasonable in very special scenarios
      // At this time, the foreign key information in foreign_key_name_map_ is inconsistent with the correct foreign key information.
      // It is necessary to rebuild foreign_key_name_map_ according to the correct foreign key information.
      is_consistent_= false;
      LOG_WARN("fail to delete fk from fk name hashmap", K(ret), K(hash_ret),
               "database id", fk_info.database_id_,
               "fk name", fk_info.foreign_key_name_);
    } else if (OB_SUCCESS != hash_ret) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fail to delete fk from fk name hashmap", K(ret), K(hash_ret),
               "database id", fk_info.database_id_,
               "fk name", fk_info.foreign_key_name_);
    }
  }

  return ret;
}

// Handle the situation of alter table drop fk, delete the foreign key information dropped from the hash map
int ObSchemaMgr::check_and_delete_given_fk_in_table(const ObSimpleTableSchemaV2 *replaced_table, const ObSimpleTableSchemaV2 *new_table)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(replaced_table)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("replaced_table should not be null", K(ret));
  } else if (OB_ISNULL(new_table)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("new_table should not be null", K(ret));
  } else {
    const ObIArray<ObSimpleForeignKeyInfo> &replaced_fk_info_array = replaced_table->get_simple_foreign_key_info_array();
    const ObIArray<ObSimpleForeignKeyInfo> &new_fk_info_array = new_table->get_simple_foreign_key_info_array();
    for (int64_t i = 0; OB_SUCC(ret) && i < replaced_fk_info_array.count(); ++i) {
      const ObSimpleForeignKeyInfo & fk_info = replaced_fk_info_array.at(i);
      if (!has_exist_in_array(new_fk_info_array, fk_info)) {
        if (OB_FAIL(delete_given_fk_from_mgr(fk_info))) {
          LOG_WARN("fail to delete fk from fk name hashmap", K(ret));
        }
      }
    }
  }

  return ret;
}

// Used to delete all foreign key information in a table from the member variable ForeignKeyNameMap of ObSchemaMgr
int ObSchemaMgr::delete_foreign_keys_in_table(const ObSimpleTableSchemaV2 &table_schema)
{
  int ret = OB_SUCCESS;

  const ObIArray<ObSimpleForeignKeyInfo> &fk_info_array = table_schema.get_simple_foreign_key_info_array();

  if (fk_info_array.empty()) {
    // If there is no foreign key in the table, do nothing
  } else {
    FOREACH_CNT_X(simple_foreign_key_info, fk_info_array, OB_SUCC(ret)) {
      if (OB_FAIL(delete_given_fk_from_mgr(*simple_foreign_key_info))) {
        LOG_WARN("fail to delete fk from table name hashmap", K(ret));
      }
    }
  }

  return ret;
}

// Get foreign_key_id according to foreign_key_name
int ObSchemaMgr::get_foreign_key_id(const uint64_t database_id,
                                    const ObString &foreign_key_name,
                                    uint64_t &foreign_key_id) const
{
  int ret = OB_SUCCESS;
  foreign_key_id = OB_INVALID_ID;

  if (!check_inner_stat()) {
      ret = OB_NOT_INIT;
      LOG_WARN("not init", K(ret));
    } else if (OB_INVALID_ID == database_id
               || foreign_key_name.empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(ret), K(database_id), K(foreign_key_name));
    } else {
      ObSimpleForeignKeyInfo *simple_foreign_key_info = NULL;
      const ObForeignKeyInfoHashWrapper foreign_key_name_wrapper(database_id, foreign_key_name);
      int hash_ret = foreign_key_name_map_.get_refactored(foreign_key_name_wrapper, simple_foreign_key_info);
      if (OB_SUCCESS == hash_ret) {
        if (OB_ISNULL(simple_foreign_key_info)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("NULL ptr", K(ret), K(simple_foreign_key_info));
        } else {
          foreign_key_id = simple_foreign_key_info->foreign_key_id_;
        }
      } else {
        // If the table id is not found based on the library name and table name, nothing will be done
      }
    }

  return ret;
}

// Get foreign_key_info according to foreign_key_name
int ObSchemaMgr::get_foreign_key_info(
                                    const uint64_t database_id,
                                    const ObString &foreign_key_name,
                                    ObSimpleForeignKeyInfo &foreign_key_info) const
{
  int ret = OB_SUCCESS;

  if (!check_inner_stat()) {
      ret = OB_NOT_INIT;
      LOG_WARN("not init", K(ret));
    } else if (OB_INVALID_ID == database_id
               || foreign_key_name.empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(ret), K(database_id), K(foreign_key_name));
    } else {
      ObSimpleForeignKeyInfo *simple_foreign_key_info = NULL;
      const ObForeignKeyInfoHashWrapper foreign_key_name_wrapper(database_id,
                                                                foreign_key_name);
      int hash_ret = foreign_key_name_map_.get_refactored(foreign_key_name_wrapper,
                                                          simple_foreign_key_info);
      if (OB_SUCCESS == hash_ret) {
        if (OB_ISNULL(simple_foreign_key_info)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("NULL ptr", K(ret), K(simple_foreign_key_info));
        } else {
          foreign_key_info = *simple_foreign_key_info;
          foreign_key_info.foreign_key_name_.assign(const_cast<char *>(foreign_key_name.ptr()),
                                                    foreign_key_name.length());
        }
      } else {
        // If the table id is not found based on the library name and table name, nothing will be done
      }
    }

  return ret;
}

// Used to add all constraint information in a table to the member variable constraint_name_map_ of ObSchemaMgr
int ObSchemaMgr::add_constraints_in_table(
    const ObSimpleTableSchemaV2 *new_table_schema,
    const int over_write)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(new_table_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("new_table_schema is NULL ptr", K(ret));
  } else if (new_table_schema->is_mysql_tmp_table()) {
    // In mysql mode, check constraints in non-temporary tables don't share the namespace with constraints in temporary tables
  } else {
    const common::ObIArray<ObSimpleConstraintInfo> &cst_info_array = new_table_schema->get_simple_constraint_info_array();
    if (cst_info_array.empty()) {
      // If there is no cst in the table, do nothing
    } else {
      FOREACH_CNT_X(simple_constraint_info, cst_info_array, OB_SUCC(ret)) {
        ObConstraintInfoHashWrapper constraint_name_wrapper(simple_constraint_info->database_id_,
                                                            simple_constraint_info->constraint_name_);
        int hash_ret = constraint_name_map_.set_refactored(constraint_name_wrapper,
                                                           const_cast<ObSimpleConstraintInfo*> (simple_constraint_info),
                                                           over_write);
        if (OB_SUCCESS != hash_ret) {
          ret = OB_HASH_EXIST == hash_ret ? OB_SUCCESS : OB_ERR_UNEXPECTED;
          LOG_ERROR("build cst name hashmap failed", K(ret), K(hash_ret),
                    "database_id", simple_constraint_info->database_id_,
                    "table_id", simple_constraint_info->table_id_,
                    "cst_id", simple_constraint_info->constraint_id_,
                    "cst_name", simple_constraint_info->constraint_name_);
        }
      }
    }
  }

  return ret;
}

// According to table_schema and constraint name, delete the specified constraint related to the corresponding table_schema
int ObSchemaMgr::delete_given_cst_from_mgr(const ObSimpleConstraintInfo &cst_info)
{
  int ret = OB_SUCCESS;

  if (cst_info.database_id_ == common::OB_INVALID_ID
      || cst_info.constraint_name_.empty()){
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("cst_info should not be null", K(ret), K(cst_info));
  } else {
    ObConstraintInfoHashWrapper constraint_name_wrapper(cst_info.database_id_,
                                                        cst_info.constraint_name_);
    int hash_ret = constraint_name_map_.erase_refactored(constraint_name_wrapper);
    if (OB_HASH_NOT_EXIST == hash_ret) {
      // Because there is no guarantee to refresh in strict accordance with the version order of the schema version,
      // the return value of OB_HASH_NOT_EXIST is reasonable in very special scenarios
      // At this time, the cst information in constraint_name_map_ is inconsistent with the correct foreign key information.
      // It is necessary to rebuild the constraint_name_map_ according to the correct cst information.
      is_consistent_ = false;
      LOG_WARN("fail to delete cst from cst name hashmap", K(ret), K(hash_ret),
               "database id", cst_info.database_id_,
               "cst name", cst_info.constraint_name_);
    } else if (OB_SUCCESS != hash_ret) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fail to delete cst from cst name hashmap", K(ret), K(hash_ret),
               "database id", cst_info.database_id_,
               "cst name", cst_info.constraint_name_);
    }
  }

  return ret;
}

// Handle the situation of alter table drop cst, delete the constraint information dropped from the hash map
int ObSchemaMgr::check_and_delete_given_cst_in_table(const ObSimpleTableSchemaV2 *replaced_table, const ObSimpleTableSchemaV2 *new_table)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(replaced_table)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("replaced_table should not be null", K(ret));
  } else if (OB_ISNULL(new_table)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("new_table should not be null", K(ret));
  } else {
    const ObIArray<ObSimpleConstraintInfo> &replaced_cst_info_array = replaced_table->get_simple_constraint_info_array();
    const ObIArray<ObSimpleConstraintInfo> &new_cst_info_array = new_table->get_simple_constraint_info_array();
    for (int64_t i = 0; OB_SUCC(ret) && i < replaced_cst_info_array.count(); ++i) {
      const ObSimpleConstraintInfo & cst_info = replaced_cst_info_array.at(i);
      if (!has_exist_in_array(new_cst_info_array, cst_info)) {
        if (OB_FAIL(delete_given_cst_from_mgr(cst_info))) {
          LOG_WARN("fail to delete cst from cst name hashmap", K(ret));
        }
      }
    }
  }

  return ret;
}

// Used to delete all constraint information in a table from the member variable ConstraintNameMap of ObSchemaMgr
int ObSchemaMgr::delete_constraints_in_table(const ObSimpleTableSchemaV2 &table_schema)
{
  int ret = OB_SUCCESS;
  const ObIArray<ObSimpleConstraintInfo> &cst_info_array = table_schema.get_simple_constraint_info_array();

  if (table_schema.is_mysql_tmp_table()) {
    // In mysql mode, check constraints in non-temporary tables don't share namespace with constraints in temporary tables
  } else if (cst_info_array.empty()) {
    // If there are no constraint in the table, do nothing
  } else {
    FOREACH_CNT_X(simple_constraint_info, cst_info_array, OB_SUCC(ret)) {
      if (OB_FAIL(delete_given_cst_from_mgr(*simple_constraint_info))) {
        LOG_WARN("fail to delete cst from table name hashmap", K(ret));
      }
    }
  }

  return ret;
}

// Obtain constraint_id according to constraint_name
int ObSchemaMgr::get_constraint_id(const uint64_t database_id,
                                   const ObString &constraint_name,
                                   uint64_t &constraint_id) const
{
  int ret = OB_SUCCESS;
  constraint_id = OB_INVALID_ID;

  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_INVALID_ID == database_id ||
              constraint_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(database_id), K(constraint_name));
  } else {
    ObSimpleConstraintInfo *simple_constraint_info = NULL;
    const ObConstraintInfoHashWrapper constraint_name_wrapper(database_id, constraint_name);
    int hash_ret = constraint_name_map_.get_refactored(constraint_name_wrapper, simple_constraint_info);
    if (OB_SUCCESS == hash_ret) {
      if (OB_ISNULL(simple_constraint_info)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("NULL ptr", K(ret), K(simple_constraint_info));
      } else {
        constraint_id = simple_constraint_info->constraint_id_;
      }
    } else {
      // If the table id is not found based on the library name and table name, nothing will be done
    }
  }

  return ret;
}

int ObSchemaMgr::get_constraint_info(
                                    const uint64_t database_id,
                                    const common::ObString &constraint_name,
                                    ObSimpleConstraintInfo &constraint_info) const
{
  int ret = OB_SUCCESS;
  constraint_info.constraint_id_ = OB_INVALID_ID;
  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_INVALID_ID == database_id ||
              constraint_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(database_id), K(constraint_name));
  } else {
    ObSimpleConstraintInfo *simple_constraint_info = NULL;
    const ObConstraintInfoHashWrapper constraint_name_wrapper(database_id,
                                                              constraint_name);
    int hash_ret = constraint_name_map_.get_refactored(constraint_name_wrapper,
                                                       simple_constraint_info);
    if (OB_SUCCESS == hash_ret) {
      if (OB_ISNULL(simple_constraint_info)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("NULL ptr", K(ret), K(simple_constraint_info));
      } else {
        constraint_info = *simple_constraint_info;
        constraint_info.constraint_name_.assign(const_cast<char *>(constraint_name.ptr()),
                                                constraint_name.length());
      }
    } else {
      LOG_INFO("get constraint info failed, entry not exist", K(constraint_name));
      // If the table id is not found based on the library name and table name, nothing will be done
    }
  }

  return ret;
}

bool ObSchemaMgr::check_schema_meta_consistent()
{
  // Check the number of foreign keys here, if not, you need to rebuild
  if (!is_consistent_) {
    // false == is_consistent, do nothing
    LOG_WARN_RET(OB_ERR_UNEXPECTED, "fk or cst info is not consistent");
  }

  if (database_infos_.count() != database_name_map_.item_count()) {
    is_consistent_ = false;
    LOG_WARN_RET(OB_ERR_UNEXPECTED, "database info is not consistent",
             "database_infos_count", database_infos_.count(),
             "database_name_map_item_count", database_name_map_.item_count());
  }

  if (table_infos_.count() != table_id_map_.item_count()
      || table_id_map_.item_count() !=
        (table_name_map_.item_count() +
         normal_index_name_map_.item_count() +
         lob_meta_infos_.count() +
         lob_piece_infos_.count() +
         hidden_table_name_map_.item_count() +
         built_in_index_name_map_.item_count())) {
    is_consistent_ = false;
    LOG_WARN_RET(OB_ERR_UNEXPECTED, "schema meta is not consistent, need rebuild",
             "schema_mgr version", get_schema_version(),
             "table_infos_count", table_infos_.count(),
             "table_id_map_item_count", table_id_map_.item_count(),
             "table_name_map_item_count", table_name_map_.item_count(),
             "index_name_map_item_count", normal_index_name_map_.item_count(),
             "lob_meta_infos_count", lob_meta_infos_.count(),
             "lob_piece_infos_count", lob_piece_infos_.count(),
             "hidden_table_map count", hidden_table_name_map_.item_count(),
             "built_in_index_map count", built_in_index_name_map_.item_count());
  }

  return is_consistent_;
}

int ObSchemaMgr::rebuild_schema_meta_if_not_consistent()
{
  int ret = OB_SUCCESS;
  uint64_t fk_cnt = 0;
  uint64_t cst_cnt = 0;

  if (!check_schema_meta_consistent()) {
    LOG_WARN("schema meta is not consistent, need rebuild", K(ret));
    // 
    if (OB_FAIL(rebuild_table_hashmap(fk_cnt, cst_cnt))) {
      LOG_WARN("rebuild table hashmap failed", K(ret));
    } else if (OB_FAIL(rebuild_db_hashmap())) {
      LOG_WARN("rebuild db hashmap failed", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    // If it is inconsistent (!is_consistent_), rebuild is required, after the rebuild is over,
    // check whether fk and cst are consistent
    // If they are the same, there is no need to rebuild and check whether fk and cst are the same
    if (!is_consistent_ && (fk_cnt != foreign_key_name_map_.item_count())) {
      is_consistent_ = false;
      LOG_WARN("fk info is still not consistent after rebuild, need fixing", K(fk_cnt), K(foreign_key_name_map_.item_count()));
    } else if (!is_consistent_ && (cst_cnt != constraint_name_map_.item_count())) {
      is_consistent_ = false;
      LOG_WARN("cst info is still not consistent after rebuild, need fixing", K(cst_cnt), K(constraint_name_map_.item_count()));
    } else {
      is_consistent_ = true;
    }
    // Check whether db and table are consistent
    if (!check_schema_meta_consistent()) {
      ret = OB_DUPLICATE_OBJECT_NAME_EXIST;
      LOG_ERROR("schema meta is still not consistent after rebuild, need fixing", KR(ret));
      LOG_DBA_ERROR(OB_DUPLICATE_OBJECT_NAME_EXIST,
                    "msg", "duplicate table/database/foreign key/constraint exist",
                    "db_cnt", database_infos_.count(), "db_name_cnt", database_name_map_.item_count(),
                    "table_cnt", table_infos_.count(), "table_id_cnt", table_id_map_.item_count(),
                    "table_name_cnt", table_name_map_.item_count(), "index_name_cnt", normal_index_name_map_.item_count(),
                    "lob_meta_cnt", lob_meta_infos_.count(),
                    "log_piece_cnt", lob_piece_infos_.count(), "hidden_table_cnt", hidden_table_name_map_.item_count(),
                    "built_in_index_cnt", built_in_index_name_map_.item_count(),
                    "fk_cnt", fk_cnt, "fk_name_cnt", foreign_key_name_map_.item_count(),
                    "cst_cnt", cst_cnt, "cst_name_cnt", constraint_name_map_.item_count());
      right_to_die_or_duty_to_live();
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(trigger_mgr_.try_rebuild_trigger_hashmap())) {
      LOG_WARN("rebuild trigger hashmap failed", K(ret));
    }
  }
  return ret;
}

int ObSchemaMgr::del_table(const ObTableId table)
{
  int ret = OB_SUCCESS;

  const ObSimpleServerRuntimeSchema *runtime_schema = NULL;
  ObSimpleTableSchemaV2 *schema_to_del = NULL;
  const uint64_t table_id = table.table_id_;
  bool is_runtime_space_table = false;
  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!table.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(table));
  } else if (OB_FAIL(get_server_runtime_schema( runtime_schema))) {
    LOG_WARN("get runtime schema failed", K(ret));
  } else if (OB_ISNULL(runtime_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", K(ret), K(runtime_schema));
  }

  ObNameCaseMode mode = OB_NAME_CASE_INVALID;
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ObSysTableChecker::is_runtime_space_table_id(table_id, is_runtime_space_table))) {
    LOG_WARN("fail to check if table_id is in runtime space", K(ret), K(table_id));
  } else if (OB_FAIL(get_runtime_name_case_mode(mode))) {
    LOG_WARN("fail to get_runtime_name_case_mode",  K(ret));
  } else if (OB_NAME_CASE_INVALID == mode) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid case mode", K(ret), K(mode));
  }

  if (OB_FAIL((ret))) {
  } else if (OB_FAIL(table_infos_.remove_if(table,
                                            compare_with_table_id,
                                            equal_with_table_id,
                                            schema_to_del))) {
    LOG_WARN("failed to remove table schema, ",
             "table_id",
             table.table_id_,
             K(ret));
  } else if (OB_ISNULL(schema_to_del)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("removed table schema return NULL, ",
             "table_id",
             table.table_id_,
             K(ret));
  } else {
    if (schema_to_del->is_index_table()) {
      if (OB_FAIL(remove_aux_table(*schema_to_del))) {
        LOG_WARN("failed to remove aux table schema", K(ret), K(*schema_to_del));
      }
    } else if (schema_to_del->is_aux_lob_meta_table()) {
      if (OB_FAIL(remove_aux_table(*schema_to_del))) {
        LOG_WARN("failed to remove aux table schema", K(ret), K(*schema_to_del));
      }
    } else if (schema_to_del->is_aux_lob_piece_table()) {
      if (OB_FAIL(remove_aux_table(*schema_to_del))) {
        LOG_WARN("failed to remove aux table schema", K(ret), K(*schema_to_del));
      }
    }
  }
  if (OB_SUCC(ret)) {
    int hash_ret = table_id_map_.erase_refactored(schema_to_del->get_table_id());
    if (OB_SUCCESS != hash_ret) {
      LOG_WARN("failed delete table from table id hashmap, ",
               "hash_ret", hash_ret,
               "table_id", schema_to_del->get_table_id());
      // Increase the fault-tolerant processing of incremental schema refresh, no error is reported at this time,
      // and the solution is solved by rebuild logic
      ret = OB_HASH_NOT_EXIST != hash_ret ? hash_ret : ret;
    } else {
      if (schema_to_del->is_user_hidden_table()) {
        // when delete a hidden table, need to remove it from hidden_table_name_map_
        ObTableSchemaHashWrapper table_schema_wrapper(schema_to_del->get_database_id(),
                                                      schema_to_del->get_session_id(),
                                                      mode,
                                                      schema_to_del->get_table_name_str());
        int hash_ret = hidden_table_name_map_.erase_refactored(table_schema_wrapper);
        LOG_WARN("failed delete table from table name hashmap, ",
                   K(ret),
                   K(hash_ret),
                   "database_id", schema_to_del->get_database_id(),
                   "table_name", schema_to_del->get_table_name());
        if (OB_SUCCESS != hash_ret) {
          LOG_WARN("failed delete table from table name hashmap, ",
                   K(ret),
                   K(hash_ret),
                   "database_id", schema_to_del->get_database_id(),
                   "table_name", schema_to_del->get_table_name());
          // Increase fault tolerance for incremental schema refresh, do not report errors, rely on rebuild logic to resolve
          ret = OB_HASH_NOT_EXIST != hash_ret ? hash_ret : ret;
        }
      } else if (schema_to_del->is_index_table()) {
        const bool is_built_in_index = schema_to_del->is_built_in_index();
        IndexNameMap &index_name_map = get_index_name_map_(is_built_in_index);
        if (schema_to_del->is_in_recyclebin()) { // index is in recyclebin
          ObIndexSchemaHashWrapper index_schema_wrapper(schema_to_del->get_database_id(),
                                                        common::OB_INVALID_ID,
                                                        schema_to_del->get_table_name_str());
          int hash_ret = index_name_map.erase_refactored(index_schema_wrapper);
          if (OB_SUCCESS != hash_ret) {
            LOG_WARN("failed delete index from index name hashmap, ",
                     K(ret),
                     K(hash_ret),
                     K(is_built_in_index),
                     "index_name", schema_to_del->get_table_name());
            // Increase fault tolerance for incremental schema refresh, no error is reported at this time, rely on rebuild logic to resolve
            ret = OB_HASH_NOT_EXIST != hash_ret ? hash_ret : ret;
          }
        } else { // index is not in recyclebin
          if (OB_FAIL(schema_to_del->generate_origin_index_name())) {
            LOG_WARN("generate origin index name failed", K(ret), K(schema_to_del->get_table_name_str()));
          } else {
            int hash_ret = OB_SUCCESS;
            ObIndexSchemaHashWrapper cutted_index_name_wrapper(schema_to_del->get_database_id(),
                                                               schema_to_del->get_data_table_id(),
                                                               schema_to_del->get_origin_index_name_str());
            hash_ret = index_name_map.erase_refactored(cutted_index_name_wrapper);
            if (OB_SUCCESS != hash_ret) {
              LOG_WARN("failed delete index from index name hashmap, ",
                       K(ret),
                       K(hash_ret),
                       K(is_built_in_index),
                       K(schema_to_del->get_database_id()),
                       K(schema_to_del->get_data_table_id()),
                       "index_name", schema_to_del->get_origin_index_name_str());
              // Increase the fault-tolerant processing of incremental schema refresh, no error is reported at this time,
              // and the solution is solved by rebuild logic
              ret = OB_HASH_NOT_EXIST != hash_ret ? hash_ret : ret;
            }
          }
        }
      } else if (schema_to_del->is_aux_lob_table()) {
        // do nothing
      } else {
        ObTableSchemaHashWrapper table_schema_wrapper(schema_to_del->get_database_id(),
                                                      schema_to_del->get_session_id(),
                                                      mode,
                                                      schema_to_del->get_table_name_str());
        int hash_ret = table_name_map_.erase_refactored(table_schema_wrapper);
        if (OB_SUCCESS != hash_ret) {
          LOG_WARN("failed delete table from table name hashmap, ",
                   K(ret),
                   K(hash_ret),
                   "database_id", schema_to_del->get_database_id(),
                   "table_name", schema_to_del->get_table_name());
          // Increase the fault-tolerant processing of incremental schema refresh, no error is reported at this time,
          // and the solution is solved by rebuild logic
          ret = OB_HASH_NOT_EXIST != hash_ret ? hash_ret : ret;
        }
        if (OB_SUCC(ret)) {
          if (OB_FAIL(delete_foreign_keys_in_table(*schema_to_del))) {
            LOG_WARN("delete foreign keys info from a hash map failed", K(ret), K(*schema_to_del));
          }
        }
        if (OB_SUCC(ret)) {
          if (OB_FAIL(delete_constraints_in_table(*schema_to_del))) {
            LOG_WARN("delete constraint info from a hash map failed", K(ret), K(*schema_to_del));
          }
        }
      }
    }
  }
  // ignore ret
  if (table_infos_.count() != table_id_map_.item_count()
      || table_id_map_.item_count() !=
         (table_name_map_.item_count() +
          normal_index_name_map_.item_count() +
          lob_meta_infos_.count() +
          lob_piece_infos_.count() +
          hidden_table_name_map_.item_count() +
          built_in_index_name_map_.item_count())) {
    LOG_WARN("table info is non-consistent",
             "table_infos_count",
             table_infos_.count(),
             "table_id_map_item_count",
             table_id_map_.item_count(),
             "table_name_map_item_count",
             table_name_map_.item_count(),
             "index_name_map_item_count",
             normal_index_name_map_.item_count(),
             "lob_meta_infos_count",
             lob_meta_infos_.count(),
             "lob_piece_infos_count",
             lob_piece_infos_.count(),
             "table_id",
             table.table_id_,
             "hidden_table_map_item_count",
             hidden_table_name_map_.item_count(),
             "built_in_index_map_item_count",
             built_in_index_name_map_.item_count());
  }

  return ret;
}

int ObSchemaMgr::remove_aux_table(const ObSimpleTableSchemaV2 &schema_to_del)
{
  int ret = OB_SUCCESS;
  ObSimpleTableSchemaV2 *aux_schema_to_del = NULL;
  ObTableId table_id(schema_to_del.get_table_id());
  ObTableId data_table_key(schema_to_del.get_data_table_id());
  TableInfos *infos = nullptr;
  if (schema_to_del.is_index_table()) {
    infos = &index_infos_;
  } else if (schema_to_del.is_aux_lob_meta_table()) {
    infos = &lob_meta_infos_;
  } else if (schema_to_del.is_aux_lob_piece_table()) {
    infos = &lob_piece_infos_;
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Invalid table type.", K(ret), K(schema_to_del.get_table_type()));
  }
  TableIterator iter = infos->lower_bound(data_table_key, compare_with_data_table_id);
  TableIterator dst_iter = NULL;
  bool is_stop = false;
  for (;
      iter != (infos->end()) && OB_SUCC(ret) && !is_stop;
      ++iter) {
    if (OB_ISNULL(aux_schema_to_del = *iter)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("NULL ptr", K(aux_schema_to_del), K(ret));
    } else if (!(aux_schema_to_del->get_data_table_key() == data_table_key)) {
      is_stop = true;
    } else if (aux_schema_to_del->get_table_id() != table_id.table_id_) {
      // do-nothing
    } else {
      dst_iter = iter;
      is_stop = true;
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_ISNULL(dst_iter) || OB_ISNULL(aux_schema_to_del)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("dst_iter or aux_schema_to_del is NULL",
        K(dst_iter), K(aux_schema_to_del), K(ret));
    } else if (OB_FAIL(infos->remove(dst_iter, dst_iter + 1))) {
      LOG_WARN("failed to remove aux schema, ",
          "table_id", table_id.table_id_, K(ret));
    }
  }
  return ret;
}

int ObSchemaMgr::get_table_schema(
    const uint64_t table_id,
    const ObSimpleTableSchemaV2 *&table_schema) const
{
  int ret = OB_SUCCESS;
  table_schema = NULL;

  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_INVALID_ID == table_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(table_id));
  } else {
    ObSimpleTableSchemaV2 *tmp_schema = NULL;
    int hash_ret = table_id_map_.get_refactored(table_id, tmp_schema);
    if (OB_SUCCESS == hash_ret) {
      if (OB_ISNULL(tmp_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("NULL ptr", K(ret), K(tmp_schema));
      } else {
        table_schema = tmp_schema;
      }
    }
  }

  return ret;
}

//table_schema->session_id = 0, This is a general situation, the schema is visible to any session;
//table_schema->session_id<>0, schema is a) temp table; or b) The visibility of the table in the process of querying
//  the table creation is as follows:
// For the internal session (parameter value session_id = OB_INVALID_ID), only b# is visible, a# is not visible,
// because the temporary table T may exist between different sessions; (create temporary table as select not support yet);
// For non-internal sessions (including session_id = 0), judge according to session->session_id == table_schema->session_id;
// There may be problems, such as the SQL statement executed by ObMySQLProxy.write in the internal session, when it involves
// a temporary table or incorrectly uses a non-temporary table with the same name or reports an error that cannot be found;
// See the code for specific judgments ObTableSchemaHashWrapper::operator ==
int ObSchemaMgr::get_table_schema(
  const uint64_t database_id,
  // ObSchemaGetterGuard session_id, default value=0, initialized in ObSql::generate_stmt, if=OB_INVALID_ID is internal session
  const uint64_t session_id,
  const ObString &table_name,
  const ObSimpleTableSchemaV2 *&table_schema) const
{
  int ret = OB_SUCCESS;
  table_schema = NULL;

  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_INVALID_ID == database_id
             || table_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(database_id), K(table_name));
  } else {
    ObSimpleTableSchemaV2 *tmp_schema = NULL;
    ObNameCaseMode mode = OB_NAME_CASE_INVALID;
    if (OB_FAIL(get_runtime_name_case_mode(mode))) {
      LOG_WARN("fail to get_runtime_name_case_mode", KR(ret));
    } else if (OB_NAME_CASE_INVALID == mode) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid case mode", KR(ret), K(mode));
    }
    if (OB_SUCC(ret)) {
      const ObTableSchemaHashWrapper table_name_wrapper(database_id, session_id, mode, table_name);
      int hash_ret = table_name_map_.get_refactored(table_name_wrapper, tmp_schema);
      if (OB_SUCCESS == hash_ret) {
        if (OB_ISNULL(tmp_schema)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("NULL ptr", KR(ret), K(table_name_wrapper));
        } else {
          table_schema = tmp_schema;
        }
      } else if (OB_HASH_NOT_EXIST == hash_ret && 0 != session_id && OB_INVALID_ID != session_id) {
        // If session_id != 0, the search just now is based on the possible match of the temporary table.
        // If it is not found, then it will be searched according to session_id = 0, which is the normal table.
        const ObTableSchemaHashWrapper table_name_wrapper1(database_id, 0, mode, table_name);
        hash_ret = table_name_map_.get_refactored(table_name_wrapper1, tmp_schema);
        if (OB_SUCCESS == hash_ret) {
          if (OB_ISNULL(tmp_schema)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("NULL ptr", KR(ret), K(table_name_wrapper1));
          } else {
            table_schema = tmp_schema;
          }
        }
      }
    }
  }

  return ret;
}

int ObSchemaMgr::get_hidden_table_schema(
    const uint64_t database_id,
    const ObString &table_name,
    const ObSimpleTableSchemaV2 *&table_schema) const
{
  int ret = OB_SUCCESS;
  table_schema = NULL;
  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_INVALID_ID == database_id
             || table_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(database_id), K(table_name));
  } else {
    ObSimpleTableSchemaV2 *tmp_schema = NULL;
    ObNameCaseMode mode = OB_NAME_CASE_INVALID;
    if (OB_FAIL(get_runtime_name_case_mode(mode))) {
      LOG_WARN("fail to get_runtime_name_case_mode", K(ret));
    } else if (OB_NAME_CASE_INVALID == mode) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid case mode", K(ret), K(mode));
    }
    if (OB_SUCC(ret)) {
      const ObTableSchemaHashWrapper table_name_wrapper(database_id, 0, mode, table_name);
      int hash_ret = hidden_table_name_map_.get_refactored(table_name_wrapper, tmp_schema);
      if (OB_SUCCESS == hash_ret) {
        if (OB_ISNULL(tmp_schema)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("NULL ptr", K(ret), K(tmp_schema));
        } else {
          table_schema = tmp_schema;
        }
      }
    }
  }

  return ret;
}

ERRSIM_POINT_DEF(ERRSIM_INVALID_INDEX_NAME);

int ObSchemaMgr::get_index_schema(
  const uint64_t database_id,
  const ObString &table_name,
  const ObSimpleTableSchemaV2 *&table_schema,
  const bool is_built_in/* = false*/) const
{
  int ret = OB_SUCCESS;
  table_schema = NULL;
  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_INVALID_ID == database_id
             || table_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(database_id), K(table_name));
  } else {
    ObSimpleTableSchemaV2 *tmp_schema = NULL;
    const IndexNameMap &index_name_map = get_index_name_map_(is_built_in);
    if (is_recyclebin_database_id(database_id)) { // in recyclebin
      const ObIndexSchemaHashWrapper index_name_wrapper(
          database_id, common::OB_INVALID_ID, table_name);
      int hash_ret = index_name_map.get_refactored(index_name_wrapper, tmp_schema);
      if (OB_SUCCESS == hash_ret) {
        if (OB_ISNULL(tmp_schema)) {
         ret = OB_ERR_UNEXPECTED;
         LOG_WARN("NULL ptr", K(ret), K(table_name), K(is_built_in), KP(tmp_schema));
        } else {
         table_schema = tmp_schema;
        }
      }
    } else { // not in recyclebin
      // The database id determines whether the index is in the recycle bin.
      ObString cutted_index_name;
      uint64_t data_table_id = ObSimpleTableSchemaV2::extract_data_table_id_from_index_name(table_name);
      if (OB_UNLIKELY(ERRSIM_INVALID_INDEX_NAME)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("turn on ERRSIM_INVALID_INDEX_NAME", KR(ret));
      } else if (OB_INVALID_ID == data_table_id) {
        // nothing to do, need to go on and it will get a empty ptr of dst table_schema
      } else if (OB_FAIL(ObSimpleTableSchemaV2::get_index_name(table_name, cutted_index_name))) {
        if (OB_SCHEMA_ERROR == ret) {
          // If the input table_name of the function does not conform to the prefixed index name format of'__idx_DataTableId_IndexName',
          // an empty table schema pointer should be returned, and no error should be reported, so reset the error code to OB_SUCCESS
          ret = OB_SUCCESS;
        }
        LOG_WARN("fail to get index name", K(ret));
      } else {
        const ObIndexSchemaHashWrapper cutted_index_name_wrapper(database_id,
            data_table_id, cutted_index_name);
        int hash_ret = index_name_map.get_refactored(cutted_index_name_wrapper, tmp_schema);
        if (OB_SUCCESS == hash_ret) {
          if (OB_ISNULL(tmp_schema)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("NULL ptr", K(ret), K(is_built_in), K(tmp_schema));
          } else {
            table_schema = tmp_schema;
          }
        }
      }
    }
  }

  return ret;
}

int ObSchemaMgr::deep_copy_index_name_map(
    common::ObIAllocator &allocator,
    ObIndexNameMap &index_name_cache)
{
  int ret = OB_SUCCESS;
  {
    // index_name_cache will destory or not init, so sub_map_mem_size should be set first
    // to reduce dynamic memory allocation and avoid error.
    (void) index_name_cache.set_sub_map_mem_size(normal_index_name_map_.get_sub_map_mem_size());
    if (OB_FAIL(index_name_cache.init())) {
      LOG_WARN("init index name cache failed", KR(ret));
    }
  }
  for (int64_t sub_map_id = 0;
       OB_SUCC(ret) && sub_map_id < normal_index_name_map_.get_sub_map_count();
       sub_map_id++) {
    IndexNameMap::iterator it = normal_index_name_map_.begin(sub_map_id);
    IndexNameMap::iterator end = normal_index_name_map_.end(sub_map_id);
    for (; OB_SUCC(ret) && it != end; ++it) {
      const ObSimpleTableSchemaV2 *index_schema = *it;
      void *buf = NULL;
      ObIndexNameInfo *index_name_info = NULL;
      uint64_t data_table_id = OB_INVALID_ID;
      uint64_t database_id = OB_INVALID_ID;
      ObString index_name;
      if (OB_ISNULL(index_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("index schema is null", KR(ret));
      } else if (FALSE_IT(database_id = index_schema->get_database_id())) {
      } else if (OB_UNLIKELY(!is_recyclebin_database_id(database_id)
                 && index_schema->get_origin_index_name_str().empty())) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid index schema", KR(ret), KPC(index_schema));
      } else if (OB_ISNULL(buf = allocator.alloc(sizeof(ObIndexNameInfo)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("fail to alloc index name info", KR(ret));
      } else if (FALSE_IT(index_name_info = new (buf) ObIndexNameInfo())) {
      } else if (OB_FAIL(index_name_info->init(allocator, *index_schema))) {
        LOG_WARN("fail to init index name info", KR(ret), KPC(index_schema));
      } else if (is_recyclebin_database_id(database_id)) {
        data_table_id = OB_INVALID_ID;
        index_name = index_name_info->get_index_name();
      } else {
        data_table_id = index_name_info->get_data_table_id();
        index_name = index_name_info->get_original_index_name();
      }
      if (OB_SUCC(ret)) {
        int overwrite = 0;
        ObIndexSchemaHashWrapper index_name_wrapper(database_id,
                                                    data_table_id,
                                                    index_name);
        if (OB_FAIL(index_name_cache.set_refactored(
            index_name_wrapper, index_name_info, overwrite))) {
          LOG_WARN("fail to set refactored", KR(ret), KPC(index_name_info));
          if (OB_HASH_EXIST == ret) {
            ObIndexNameInfo **exist_index_info = index_name_cache.get(index_name_wrapper);
            if (OB_NOT_NULL(exist_index_info) && OB_NOT_NULL(*exist_index_info)) {
              FLOG_ERROR("duplicated index info exist", KR(ret),
                         KPC(index_name_info), KPC(*exist_index_info));
            }
          }
        }
      }
    } // end for
  } // end for
  return ret;
}

int ObSchemaMgr::get_table_schema(
                                  const uint64_t database_id,
                                  const uint64_t session_id,
                                  const ObString &table_name,
                                  const bool is_index,
                                  const ObSimpleTableSchemaV2 *&table_schema,
                                  const bool with_hidden_flag/*false*/,
                                  const bool is_built_in_index/*false*/) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(with_hidden_flag)) {
    ret = get_hidden_table_schema( database_id, table_name, table_schema);
  } else {
    if (!is_index) {
      ret = get_table_schema( database_id, session_id, table_name, table_schema);
    } else {
      ret = get_index_schema( database_id, table_name, table_schema, is_built_in_index);
    }
  }
  return ret;
}

int ObSchemaMgr::get_runtime_schemas(
    ObIArray<const ObSimpleServerRuntimeSchema *> &runtime_schemas) const
{
  int ret = OB_SUCCESS;
  runtime_schemas.reset();

  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (NULL != runtime_info_) {
    if (OB_FAIL(runtime_schemas.push_back(runtime_info_))) {
      LOG_WARN("push_back failed", K(ret));
    }
  }

  return ret;
}



// The runtime schema manager owns the current server's simple schemas.
#define GET_SCHEMAS_IN_RUNTIME_FUNC_DEFINE(SCHEMA, SCHEMA_TYPE, SCHEMA_ID_TYPE, SCHEMA_ITER) \
  int ObSchemaMgr::get_##SCHEMA##_schemas_in_runtime(                             \
      ObIArray<const SCHEMA_TYPE *> &schema_array) const                         \
  {                                                                              \
    int ret = OB_SUCCESS;                                                        \
    if (!check_inner_stat()) {                                                   \
      ret = OB_NOT_INIT;                                                         \
      LOG_WARN("not init", K(ret));                                              \
    } else {                                                                     \
      const SCHEMA_TYPE *schema = NULL;                                          \
      SCHEMA_ID_TYPE runtime_schema_id_lower(OB_MIN_ID);        \
      SCHEMA_ITER iter = SCHEMA##_infos_.lower_bound(runtime_schema_id_lower,     \
          compare_with_##SCHEMA##_id);                                           \
      for (; OB_SUCC(ret) && iter != SCHEMA##_infos_.end(); iter++) {            \
        if (OB_ISNULL(schema = *iter)) {                                         \
          ret = OB_ERR_UNEXPECTED;                                               \
          LOG_WARN("NULL ptr", K(ret), KP(schema));                              \
        } else if (OB_FAIL(schema_array.push_back(schema))) {                    \
          LOG_WARN("failed to push back "#SCHEMA" schema", K(ret));              \
        }                                                                        \
      }                                                                          \
    }                                                                            \
    return ret;                                                                  \
  }
GET_SCHEMAS_IN_RUNTIME_FUNC_DEFINE(user, ObSimpleUserSchema, ObUserId, ConstUserIterator);
GET_SCHEMAS_IN_RUNTIME_FUNC_DEFINE(database, ObSimpleDatabaseSchema, ObDatabaseId, ConstDatabaseIterator);

#undef GET_SCHEMAS_IN_RUNTIME_FUNC_DEFINE

#define GET_TABLE_SCHEMAS_IN_DST_SCHEMA_FUNC_DEFINE(DST_SCHEMA)                  \
  int ObSchemaMgr::get_table_schemas_in_##DST_SCHEMA(                            \
      const uint64_t dst_schema_id,                                              \
      ObIArray<const ObSimpleTableSchemaV2 *> &schema_array) const               \
  {                                                                              \
    int ret = OB_SUCCESS;                                                        \
    schema_array.reset();                                                        \
    if (!check_inner_stat()) {                                                   \
      ret = OB_NOT_INIT;                                                         \
      LOG_WARN("not init", K(ret));                                              \
    } else if (OB_INVALID_ID == dst_schema_id) {                                 \
      ret = OB_INVALID_ARGUMENT;                                                 \
      LOG_WARN("invalid argument", K(ret),                         \
               #DST_SCHEMA"_id", dst_schema_id);                                 \
    } else {                                                                     \
      const ObSimpleTableSchemaV2 *schema = NULL;                                \
      ObTableId table_id_lower(OB_MIN_ID);               \
      ConstTableIterator iter = table_infos_.lower_bound(table_id_lower,  \
          compare_with_table_id);                                         \
      for (; OB_SUCC(ret) && iter != table_infos_.end(); iter++) {               \
        if (OB_ISNULL(schema = *iter)) {                                         \
          ret = OB_ERR_UNEXPECTED;                                               \
          LOG_WARN("NULL ptr", K(ret), KP(schema));                              \
        } else if (dst_schema_id == schema->get_##DST_SCHEMA##_id()) {           \
          if (OB_FAIL(schema_array.push_back(schema))) {                         \
            LOG_WARN("failed to push back table schema", K(ret));                \
          }                                                                      \
        }                                                                        \
      }                                                                          \
    }                                                                            \
    return ret;                                                                  \
  }
GET_TABLE_SCHEMAS_IN_DST_SCHEMA_FUNC_DEFINE(database);

#undef GET_TABLE_SCHEMAS_IN_DST_SCHEMA_FUNC_DEFINE

int ObSchemaMgr::get_table_schemas_in_runtime(ObIArray<const ObSimpleTableSchemaV2*> &schema_array) const
{
  int ret = OB_SUCCESS;
  schema_array.reset();
  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    const ObSimpleTableSchemaV2 *schema = NULL;
    ObTableId runtime_schema_id_lower(OB_MIN_ID);
    ConstTableIterator iter = table_infos_.lower_bound(runtime_schema_id_lower,
        compare_with_table_id);
    for (; OB_SUCC(ret) && iter != table_infos_.end(); iter++) {
      if (OB_ISNULL(schema = *iter)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("NULL ptr",  K(ret), KP(schema));
      } else if (OB_FAIL(schema_array.push_back(schema))) {
        LOG_WARN("failed to push back SCHEMA schema", K(ret));
      }
    }
  }
  return ret;
}

int ObSchemaMgr::get_vector_index_schemas_in_runtime(
    ObIArray<const ObSimpleTableSchemaV2*> &schema_array) const
{
  int ret = OB_SUCCESS;
  schema_array.reset();
  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    const ObSimpleTableSchemaV2 *schema = NULL;
    ObTableId index_schema_id_lower(OB_MIN_ID);
    ConstTableIterator iter = index_infos_.lower_bound(index_schema_id_lower,
        compare_with_table_id);
    for (; OB_SUCC(ret) && iter != index_infos_.end(); iter++) {
      if (OB_ISNULL(schema = *iter)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("NULL ptr",  K(ret), KP(schema));
      } else if (schema->is_vec_index() && OB_FAIL(schema_array.push_back(schema))) {
        LOG_WARN("failed to push back SCHEMA schema", K(ret));
      }
    }
  }
  return ret;
}

int ObSchemaMgr::get_aux_schemas(
    const uint64_t data_table_id,
    ObIArray<const ObSimpleTableSchemaV2 *> &aux_schemas,
    const ObTableType table_type) const
{
  int ret = OB_SUCCESS;
  aux_schemas.reset();

  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_INVALID_ID == data_table_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(data_table_id));
  } else {
    const TableInfos *infos = nullptr;
    if (table_type == USER_INDEX) {
      infos = &index_infos_;
    } else if (table_type == AUX_LOB_META) {
      infos = &lob_meta_infos_;
    } else if (table_type == AUX_LOB_PIECE) {
      infos = &lob_piece_infos_;
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Invalid table type.", K(ret), K(table_type));
    }
    if (OB_SUCC(ret)) {
      ObTableId data_table_key(data_table_id);
      TableIterator iter = infos->lower_bound(data_table_key, compare_with_data_table_id);
      const ObSimpleTableSchemaV2 *aux_schema = NULL;
      bool will_break = false;
      for (; iter != (infos->end()) && OB_SUCC(ret) && !will_break; ++iter) {
        if (OB_ISNULL(aux_schema = *iter)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("NULL ptr", K(aux_schema), K(ret));
        } else if (!(aux_schema->get_data_table_key() == data_table_key)) {
          will_break = true;
        } else if (OB_FAIL(aux_schemas.push_back(aux_schema))) {
          LOG_WARN("push back auxiliary schema failed", K(ret));
        }
      }
    }
  }

  return ret;
}

int ObSchemaMgr::get_non_sys_table_ids(ObIArray<uint64_t> &non_sys_table_ids) const
{
  int ret = OB_SUCCESS;
  non_sys_table_ids.reset();
  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else {
    const ObSimpleTableSchemaV2 *schema = NULL;
    ObTableId table_id_lower(OB_MAX_SYS_TABLE_ID);
    ConstTableIterator iter = table_infos_.lower_bound(
                              table_id_lower,
                              compare_with_table_id);
    bool is_stop = false;
    uint64_t table_id = OB_INVALID_ID;
    for (; OB_SUCC(ret) && iter != table_infos_.end() && !is_stop; iter++) {
      if (OB_ISNULL(schema = *iter)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("NULL ptr", KR(ret), KP(schema));
      } else if (FALSE_IT(table_id = schema->get_table_id())) {
      } else if (table_id >= OB_MAX_SYS_VIEW_ID) {
        is_stop = true;
      } else if (is_inner_table(table_id) && !is_sys_table(table_id)) {
        if (OB_FAIL(non_sys_table_ids.push_back(table_id))) {
          LOG_WARN("failed to push back table id", KR(ret), K(table_id));
        }
      }
    } // end for
  }
  return ret;
}


int ObSchemaMgr::get_schema_count(int64_t &schema_count) const
{
  int ret = OB_SUCCESS;
  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    int64_t runtime_schema_count = (runtime_info_ != NULL ? 1 : 0);
    schema_count = runtime_schema_count + user_infos_.size() + database_infos_.size()
                   + table_infos_.size() + index_infos_.size()
                   + lob_meta_infos_.size() + lob_piece_infos_.size();
    int64_t outline_schema_count = 0;
    int64_t routine_schema_count = 0;
    int64_t priv_schema_count = 0;
    int64_t package_schema_count = 0;
    int64_t trigger_schema_count = 0;
    int64_t sys_variable_schema_count = 0;
    int64_t mock_fk_parent_table_schema_count = 0;
    int64_t ai_model_schema_count = 0;
    if (OB_FAIL(outline_mgr_.get_outline_schema_count(outline_schema_count))) {
      LOG_WARN("get_outline_schema_count failed", K(ret));
    } else if (OB_FAIL(routine_mgr_.get_routine_schema_count(routine_schema_count))) {
      LOG_WARN("get_routine_schema_count failed", K(ret));
    } else if (OB_FAIL(priv_mgr_.get_priv_schema_count(priv_schema_count))) {
      LOG_WARN("get_priv_schema_count failed", K(ret));
    } else if (OB_FAIL(package_mgr_.get_package_schema_count(package_schema_count))) {
      LOG_WARN("get_package_mgr_count failed", K(ret));
    } else if (OB_FAIL(trigger_mgr_.get_trigger_schema_count(trigger_schema_count))) {
      LOG_WARN("get_trigger_mgr_count failed", K(ret));
    } else if (OB_FAIL(sys_variable_mgr_.get_sys_variable_schema_count(sys_variable_schema_count))) {
      LOG_WARN("get_sys_variable_mgr_count failed", K(ret));
    } else if (OB_FAIL(mock_fk_parent_table_mgr_.get_mock_fk_parent_table_schema_count(mock_fk_parent_table_schema_count))) {
      LOG_WARN("get mock_fk_parent_table schema count failed", K(ret));
    } else if (OB_FAIL(ai_model_mgr_.get_ai_model_schema_count(ai_model_schema_count))) {
      LOG_WARN("get ai_model schema count failed", K(ret));
    } else {
      schema_count += (outline_schema_count + routine_schema_count + priv_schema_count
                       + package_schema_count
                       + sys_variable_schema_count
                       + trigger_schema_count
                       + mock_fk_parent_table_schema_count
                       + ai_model_schema_count
                      );
    }
  }
  return ret;
}

int ObSchemaMgr::get_runtime_name_case_mode(ObNameCaseMode &mode) const
{
  int ret = OB_SUCCESS;
  mode = OB_NAME_CASE_INVALID;

  const ObSimpleSysVariableSchema *sys_variable = NULL;
  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(sys_variable_mgr_.get_sys_variable_schema( sys_variable))) {
    LOG_WARN("get sys variable schema failed", K(ret));
  } else if (NULL == sys_variable) {
    // do-nothing
  } else {
    mode = sys_variable->get_name_case_mode();
  }

  return ret;
}

int ObSchemaMgr::get_runtime_read_only(bool &read_only) const
{
  int ret = OB_SUCCESS;

  read_only = false;
  const ObSimpleSysVariableSchema *sys_variable = NULL;
  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(sys_variable_mgr_.get_sys_variable_schema( sys_variable))) {
    LOG_WARN("get sys variable schema failed", K(ret));
  } else if (NULL == sys_variable) {
    ret = OB_ENTRY_NOT_EXIST;
  } else {
    read_only = sys_variable->get_read_only();
  }

  return ret;
}


int ObSchemaMgr::deal_with_db_rename(
  const ObSimpleDatabaseSchema &old_db_schema,
  const ObSimpleDatabaseSchema &new_db_schema)
{
  int ret = OB_SUCCESS;
  if (old_db_schema.get_database_id() != new_db_schema.get_database_id()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(old_db_schema), K(new_db_schema));
  } else {
    if (old_db_schema.get_database_name_str() != new_db_schema.get_database_name_str()) {
      LOG_INFO("db renamed", K(old_db_schema), K(new_db_schema));
      ObDatabaseSchemaHashWrapper db_name_wrapper(old_db_schema.get_name_case_mode(),
                                                  old_db_schema.get_database_name_str());
      int hash_ret = database_name_map_.erase_refactored(db_name_wrapper);
      if (OB_SUCCESS != hash_ret) {
        LOG_WARN("failed to delete database from database name hashmap",
                K(ret), K(hash_ret), K(old_db_schema));
        // Increase the fault-tolerant processing of incremental schema refresh, no error is reported at this time,
        // and the solution is solved by rebuild logic
        ret = OB_HASH_NOT_EXIST != hash_ret ? hash_ret : ret;
      }
    }
  }
  return ret;
}

int ObSchemaMgr::deal_with_change_table_state(const ObSimpleTableSchemaV2 &old_table_schema,
                                              const ObSimpleTableSchemaV2 &new_table_schema)
{
  int ret = OB_SUCCESS;
  bool is_runtime_space_table = false;
  ObNameCaseMode mode = OB_NAME_CASE_INVALID;
  if (OB_FAIL(ObSysTableChecker::is_runtime_space_table_id(
                      old_table_schema.get_table_id(), is_runtime_space_table))) {
    LOG_WARN("fail to check if table_id is in runtime space",
              K(ret), "table_id", old_table_schema.get_table_id());
  } else if (OB_FAIL(get_runtime_name_case_mode(mode))) {
    LOG_WARN("fail to get_runtime_name_case_mode",  K(ret));
  } else if (OB_NAME_CASE_INVALID == mode) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid case mode", K(ret), K(mode));
  }
  if (OB_FAIL(ret)) {
  } else if (old_table_schema.is_user_hidden_table()
            && !new_table_schema.is_user_hidden_table()) {
    // hidden table to non-hidden table
    ObTableSchemaHashWrapper table_name_wrapper(old_table_schema.get_database_id(),
                                                old_table_schema.get_session_id(),
                                                mode,
                                                old_table_schema.get_table_name_str());
    int hash_ret = hidden_table_name_map_.erase_refactored(table_name_wrapper);
    if (OB_SUCCESS != hash_ret) {
      LOG_WARN("fail to delete table from table name hashmap",
                K(ret), K(hash_ret), K(old_table_schema.get_table_name_str()));
      ret = OB_HASH_NOT_EXIST != hash_ret ? hash_ret : ret;
    }
  } else if (!old_table_schema.is_user_hidden_table()
            && new_table_schema.is_user_hidden_table()) {
    // non-hidden table to hidden table
    if (old_table_schema.is_index_table()) {
      const bool is_built_in_index = old_table_schema.is_built_in_index();
      IndexNameMap &index_name_map = get_index_name_map_(is_built_in_index);
      if (old_table_schema.is_in_recyclebin()) { // index is in recyclebin
        ObIndexSchemaHashWrapper index_name_wrapper(old_table_schema.get_database_id(),
                                                    common::OB_INVALID_ID,
                                                    old_table_schema.get_table_name_str());
        int hash_ret = index_name_map.erase_refactored(index_name_wrapper);
        if (OB_SUCCESS != hash_ret) {
          LOG_WARN("fail to delete index from index name hashmap",
                    K(ret), K(hash_ret), K(is_built_in_index), K(old_table_schema.get_table_name_str()));
          // increase the fault-tolerant processing of incremental schema refresh
          ret = OB_HASH_NOT_EXIST != hash_ret ? hash_ret : ret;
        }
      } else { // index is in not recyclebin
        ObString cutted_index_name;
        if (OB_FAIL(old_table_schema.get_index_name(cutted_index_name))) {
          LOG_WARN("fail to get index name", K(ret));
        } else {
          ObIndexSchemaHashWrapper cutted_index_name_wrapper(old_table_schema.get_database_id(),
                                                             old_table_schema.get_data_table_id(),
                                                             cutted_index_name);
          int hash_ret = index_name_map.erase_refactored(cutted_index_name_wrapper);
          if (OB_SUCCESS != hash_ret) {
            LOG_WARN("failed delete index from index name hashmap, ",
                      K(ret), K(hash_ret), K(is_built_in_index), K(cutted_index_name));
            // increase the fault-tolerant processing of incremental schema refresh
            ret = OB_HASH_NOT_EXIST != hash_ret ? hash_ret : ret;
          }
        }
      }
    } else if (old_table_schema.is_aux_lob_table()) {
      // do nothing
    } else {
      ObTableSchemaHashWrapper table_name_wrapper(old_table_schema.get_database_id(),
                                                  old_table_schema.get_session_id(),
                                                  mode,
                                                  old_table_schema.get_table_name_str());
      int hash_ret = table_name_map_.erase_refactored(table_name_wrapper);
      if (OB_SUCCESS != hash_ret) {
        LOG_WARN("fail to delete table from table name hashmap",
                  K(ret), K(hash_ret), K(old_table_schema.get_table_name_str()));
        // increase the fault-tolerant processing of incremental schema refresh
        ret = OB_HASH_NOT_EXIST != hash_ret ? hash_ret : ret;
      }
    }
  } else {
    /* do nothing */
  }
  return ret;
}

int ObSchemaMgr::deal_with_table_rename(
  const ObSimpleTableSchemaV2 &old_table_schema,
  const ObSimpleTableSchemaV2 &new_table_schema)
{
  int ret = OB_SUCCESS;

  if (old_table_schema.get_table_id() != new_table_schema.get_table_id()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument",
        K(old_table_schema),
        K(new_table_schema));
  } else {
    const uint64_t old_database_id = old_table_schema.get_database_id();
    const uint64_t new_database_id = new_table_schema.get_database_id();
    const ObString &old_table_name = old_table_schema.get_table_name_str();
    const ObString &new_table_name = new_table_schema.get_table_name_str();
    bool is_rename = (old_table_name != new_table_name) || (old_database_id != new_database_id);
    // if the old table is a hidden table, the hidden table will not be added to the map, need skip
    // if change a non-hidden table to a hidden table, skip it here and handle it in
    // deal_with_change_table_state_to_hidden
    if (!is_rename
        || old_table_schema.is_user_hidden_table()
        || (!old_table_schema.is_user_hidden_table()
        && new_table_schema.is_user_hidden_table())) {
      // do-nothing
    } else {
      LOG_INFO("table renamed",
               K(old_database_id),
               K(old_table_name),
               K(new_database_id),
               K(new_table_name));
      bool is_runtime_space_table = false;
      if (old_table_schema.is_index_table()) {
        const bool is_built_in_index = old_table_schema.is_built_in_index();
        IndexNameMap &index_name_map = get_index_name_map_(is_built_in_index);
        if (old_table_schema.is_in_recyclebin()) { // index is in recyclebin
          ObIndexSchemaHashWrapper index_name_wrapper(old_table_schema.get_database_id(),
                                                      common::OB_INVALID_ID,
                                                      old_table_schema.get_table_name_str());
          int hash_ret = index_name_map.erase_refactored(index_name_wrapper);
          if (OB_SUCCESS != hash_ret) {
            LOG_WARN("fail to delete index from index name hashmap",
                     K(ret), K(hash_ret), K(is_built_in_index), K(old_table_name));
            // Increase fault tolerance for incremental schema refresh, do not report errors, rely on rebuild logic to resolve
            ret = OB_HASH_NOT_EXIST != hash_ret ? hash_ret : ret;
          }
        } else { // index is not in recyclebin
          ObString cutted_index_name;
          if (OB_FAIL(old_table_schema.get_index_name(cutted_index_name))) {
            LOG_WARN("fail to get index name", K(ret));
          } else {
            ObIndexSchemaHashWrapper cutted_index_name_wrapper(old_table_schema.get_database_id(),
                                                               old_table_schema.get_data_table_id(),
                                                               cutted_index_name);
            int hash_ret = index_name_map.erase_refactored(cutted_index_name_wrapper);
            if (OB_SUCCESS != hash_ret) {
              LOG_WARN("failed delete index from index name hashmap, ",
                       K(ret), K(hash_ret), K(is_built_in_index), K(cutted_index_name));
              // Increase the fault-tolerant processing of incremental schema refresh, no error is reported at this time,
              // and the solution is solved by rebuild logic
              ret = OB_HASH_NOT_EXIST != hash_ret ? hash_ret : ret;
            }
          }
        }
      } else if (old_table_schema.is_aux_lob_table()) {
        // do nothing
      } else {
        ObNameCaseMode mode = OB_NAME_CASE_INVALID;
        if (OB_FAIL(ObSysTableChecker::is_runtime_space_table_id(
                           old_table_schema.get_table_id(), is_runtime_space_table))) {
          LOG_WARN("fail to check if table_id is in runtime space",
                   K(ret), "table_id", old_table_schema.get_table_id());
        } else if (OB_FAIL(get_runtime_name_case_mode(mode))) {
          LOG_WARN("fail to get_runtime_name_case_mode",  K(ret));
        } else if (OB_NAME_CASE_INVALID == mode) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid case mode", K(ret), K(mode));
        }
        if (OB_SUCC(ret)) {
          ObTableSchemaHashWrapper table_name_wrapper(old_table_schema.get_database_id(),
                                                      old_table_schema.get_session_id(),
                                                      mode,
                                                      old_table_schema.get_table_name_str());
          int hash_ret = table_name_map_.erase_refactored(table_name_wrapper);
          if (OB_SUCCESS != hash_ret) {
            LOG_WARN("fail to delete table from table name hashmap",
                     K(ret), K(hash_ret), K(old_table_name));
            // Increase the fault-tolerant processing of incremental schema refresh, no error is reported at this time,
            // and the solution is solved by rebuild logic
            ret = OB_HASH_NOT_EXIST != hash_ret ? hash_ret : ret;
          }
        }
      }
    }
  }

  return ret;
}

int ObSchemaMgr::rebuild_db_hashmap()
{
  int ret = OB_SUCCESS;
  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    database_name_map_.clear();
    int over_write = 0;
    for (ConstDatabaseIterator iter = database_infos_.begin();
        iter != database_infos_.end() && OB_SUCC(ret); ++iter) {
      ObSimpleDatabaseSchema *database_schema = *iter;
      if (OB_ISNULL(database_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("database schema is NULL", K(ret));
      } else {
        ObDatabaseSchemaHashWrapper db_name_wrapper(database_schema->get_name_case_mode(),
                                                    database_schema->get_database_name());
        int hash_ret = database_name_map_.set_refactored(db_name_wrapper,
                                                         database_schema,
                                                         over_write);
        if (OB_SUCCESS != hash_ret) {
          ret = OB_HASH_EXIST == hash_ret ? OB_SUCCESS : OB_ERR_UNEXPECTED;
          LOG_ERROR("build database name hashmap failed", K(ret), K(hash_ret), K(*database_schema));
        }
      }
    }
  }
  return ret;
}

int ObSchemaMgr::rebuild_table_hashmap(uint64_t &fk_cnt, uint64_t &cst_cnt)
{
  int ret = OB_SUCCESS;
  fk_cnt = 0;
  cst_cnt = 0;

  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    table_id_map_.clear();
    table_name_map_.clear();
    normal_index_name_map_.clear();
    foreign_key_name_map_.clear();
    constraint_name_map_.clear();
    hidden_table_name_map_.clear();
    built_in_index_name_map_.clear();
    ObSimpleTableSchemaV2 *table_schema = NULL;
    // It is expected that OB_HASH_EXIST should not appear in the rebuild process
    int over_write = 0;
    int tmp_ret = OB_SUCCESS;
    ObSimpleTableSchemaV2 *exist_schema = NULL;

    for (ConstTableIterator iter = table_infos_.begin();
        iter != table_infos_.end() && OB_SUCC(ret);
        ++iter) {
      table_schema = *iter;
      exist_schema = NULL;
      LOG_TRACE("table_info is", "table_id", table_schema->get_table_id());

      if (OB_ISNULL(table_schema) || !table_schema->is_valid()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table_schema is unexpected", K(ret), K(table_schema));
      } else {
        int hash_ret = table_id_map_.set_refactored(table_schema->get_table_id(),
                                                    table_schema,
                                                    over_write);
        if (OB_SUCCESS != hash_ret) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("build table id hashmap failed", K(ret), K(hash_ret),
                   "table_id", table_schema->get_table_id());
        } else if (table_schema->is_user_hidden_table()) {
          ObTableSchemaHashWrapper table_name_wrapper(table_schema->get_database_id(),
                                                      table_schema->get_session_id(),
                                                      table_schema->get_name_case_mode(),
                                                      table_schema->get_table_name_str());
          hash_ret = hidden_table_name_map_.set_refactored(table_name_wrapper, table_schema,
                                                           over_write);
          if (OB_SUCCESS != hash_ret) {
            ret = OB_HASH_EXIST == hash_ret ? OB_SUCCESS : OB_ERR_UNEXPECTED;
            tmp_ret = hidden_table_name_map_.get_refactored(table_name_wrapper, exist_schema);
            LOG_ERROR("build hidden table name hashmap failed",
                      KR(ret), KR(hash_ret), K(tmp_ret),
                      "exist_table_id", OB_NOT_NULL(exist_schema) ? exist_schema->get_table_id() : OB_INVALID_ID,
                      "exist_database_id", OB_NOT_NULL(exist_schema) ? exist_schema->get_database_id() : OB_INVALID_ID,
                      "exist_session_id", OB_NOT_NULL(exist_schema) ? exist_schema->get_session_id() : OB_INVALID_ID,
                      "exist_name_case_mode", OB_NOT_NULL(exist_schema) ? exist_schema->get_name_case_mode() : OB_NAME_CASE_INVALID,
                      "exist_table_name", OB_NOT_NULL(exist_schema) ? exist_schema->get_table_name() : "",
                      "table_id", table_schema->get_table_id(),
                      "databse_id", table_schema->get_database_id(),
                      "session_id", table_schema->get_session_id(),
                      "name_case_mode", table_schema->get_name_case_mode(),
                      "table_name", table_schema->get_table_name());
          }
        } else {
          if (table_schema->is_index_table()) {
            LOG_TRACE("index is", "table_id", table_schema->get_table_id(),
                      "database_id", table_schema->get_database_id(),
                      "table_name", table_schema->get_table_name_str());
            const bool is_built_in_index = table_schema->is_built_in_index();
            IndexNameMap &index_name_map = get_index_name_map_(is_built_in_index);
            if (table_schema->is_in_recyclebin()) {
              ObIndexSchemaHashWrapper index_name_wrapper(table_schema->get_database_id(),
                                                          common::OB_INVALID_ID,
                                                          table_schema->get_table_name_str());
              hash_ret = index_name_map.set_refactored(index_name_wrapper, table_schema, over_write);
              if (OB_SUCCESS != hash_ret) {
                ret = OB_HASH_EXIST == hash_ret ? OB_SUCCESS : OB_ERR_UNEXPECTED;
                tmp_ret = index_name_map.get_refactored(index_name_wrapper, exist_schema);
                LOG_ERROR("build index name hashmap failed",
                          KR(ret), KR(hash_ret), K(tmp_ret), K(is_built_in_index),
                          "exist_table_id", OB_NOT_NULL(exist_schema) ? exist_schema->get_table_id() : OB_INVALID_ID,
                          "exist_database_id", OB_NOT_NULL(exist_schema) ? exist_schema->get_database_id() : OB_INVALID_ID,
                          "index_name",  OB_NOT_NULL(exist_schema) ? exist_schema->get_table_name() : "",
                          "table_id", table_schema->get_table_id(),
                          "databse_id", table_schema->get_database_id(),
                          "index_name", table_schema->get_table_name());
              }
            } else { // index is not in recyclebin
              if (OB_FAIL(table_schema->generate_origin_index_name())) {
                LOG_WARN("generate origin index name failed", K(ret), K(table_schema->get_table_name_str()));
              } else {
                ObIndexSchemaHashWrapper cutted_index_name_wrapper(table_schema->get_database_id(),
                                                                   table_schema->get_data_table_id(),
                                                                   table_schema->get_origin_index_name_str());
                hash_ret = index_name_map.set_refactored(cutted_index_name_wrapper, table_schema, over_write);
                if (OB_SUCCESS != hash_ret) {
                  ret = OB_HASH_EXIST == hash_ret ? OB_SUCCESS : OB_ERR_UNEXPECTED;
                  tmp_ret = index_name_map.get_refactored(cutted_index_name_wrapper, exist_schema);
                  LOG_ERROR("build index name hashmap failed",
                            KR(ret), KR(hash_ret), K(tmp_ret), K(is_built_in_index),
                            "exist_table_id", OB_NOT_NULL(exist_schema) ? exist_schema->get_table_id() : OB_INVALID_ID,
                            "exist_database_id", OB_NOT_NULL(exist_schema) ? exist_schema->get_database_id() : OB_INVALID_ID,
                            "index_name",  OB_NOT_NULL(exist_schema) ? exist_schema->get_origin_index_name_str() : "",
                            "table_id", table_schema->get_table_id(),
                            "databse_id", table_schema->get_database_id(),
                            "index_name", table_schema->get_origin_index_name_str());
                }
              }
            }
          } else if (table_schema->is_aux_lob_table()) {
            // do nothing
          } else {
            LOG_TRACE("table is", "table_id", table_schema->get_table_id(),
                      "database_id", table_schema->get_database_id(),
                     "table_name", table_schema->get_table_name_str());
            ObTableSchemaHashWrapper table_name_wrapper(table_schema->get_database_id(),
                                                        table_schema->get_session_id(),
                                                        table_schema->get_name_case_mode(),
                                                        table_schema->get_table_name_str());
            hash_ret = table_name_map_.set_refactored(table_name_wrapper, table_schema, over_write);
            if (OB_SUCCESS != hash_ret) {
              ret = OB_HASH_EXIST == hash_ret ? OB_SUCCESS : OB_ERR_UNEXPECTED;
              tmp_ret = table_name_map_.get_refactored(table_name_wrapper, exist_schema);
              LOG_ERROR("build table name hashmap failed",
                        K(ret), K(hash_ret), K(tmp_ret),
                        "exist_table_id", OB_NOT_NULL(exist_schema) ? exist_schema->get_table_id() : OB_INVALID_ID,
                        "exist_database_id", OB_NOT_NULL(exist_schema) ? exist_schema->get_database_id() : OB_INVALID_ID,
                        "exist_session_id", OB_NOT_NULL(exist_schema) ? exist_schema->get_session_id() : OB_INVALID_ID,
                        "exist_name_case_mode", OB_NOT_NULL(exist_schema) ? exist_schema->get_name_case_mode() : OB_NAME_CASE_INVALID,
                        "exist_table_name", OB_NOT_NULL(exist_schema) ? exist_schema->get_table_name() : "",
                        "table_id", table_schema->get_table_id(),
                        "databse_id", table_schema->get_database_id(),
                        "session_id", table_schema->get_session_id(),
                        "name_case_mode", table_schema->get_name_case_mode(),
                        "table_name", table_schema->get_table_name());
            }
            if (OB_SUCC(ret)) {
              if (OB_FAIL(add_foreign_keys_in_table(table_schema->get_simple_foreign_key_info_array(), over_write))) {
                LOG_WARN("add foreign keys info to a hash map failed", K(ret), K(table_schema->get_table_name_str()));
              } else {
                fk_cnt += table_schema->get_simple_foreign_key_info_array().count();
              }
            }
            if (OB_SUCC(ret)) {
              if (table_schema->is_mysql_tmp_table()) {
                // check constraints in non-temporary tables don't share namespace with constraints in temporary tables, do nothing
              } else if (OB_FAIL(add_constraints_in_table(table_schema, over_write))) {
                LOG_WARN("add constraint info to a hash map failed", K(ret), K(table_schema->get_table_name_str()));
              } else {
                cst_cnt += table_schema->get_simple_constraint_info_array().count();
              }
            }
          }
        }
      }
    }
  }

  return ret;
}

int ObSchemaMgr::get_idx_schema_by_origin_idx_name(const uint64_t database_id,
                                                   const common::ObString &ori_index_name,
                                                   const ObSimpleTableSchemaV2 *&table_schema) const
{
  int ret = OB_SUCCESS;
  table_schema = NULL;
  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_INVALID_ID == database_id
             || ori_index_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(database_id), K(ori_index_name));
  } else {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("origin index name lookup is not supported",
             KR(ret), K(database_id));
  }
  return ret;
}

void ObSchemaMgr::dump() const
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  int64_t schema_count = 0;
  int64_t schema_size = 0;
  tmp_ret = get_schema_count(schema_count);
  ret = OB_SUCC(ret) ? tmp_ret : ret;
  tmp_ret = get_schema_size(schema_size);
  LOG_INFO("[SCHEMA_STATISTICS] dump schema_mgr",
           K(tmp_ret),
           
           K_(schema_version),
           K(schema_count),
           K(schema_size));

  #define DUMP_SCHEMA(SCHEMA, SCHEMA_TYPE, SCHEMA_ITER)   \
    {                                                     \
      for (SCHEMA_ITER iter = SCHEMA##_infos_.begin();    \
          iter != SCHEMA##_infos_.end(); iter++) {        \
        SCHEMA_TYPE *schema = *iter;                      \
        if (NULL == schema) {                             \
          LOG_INFO("NULL ptr", KP(schema));                \
        } else {                                          \
          LOG_INFO(#SCHEMA, K(*schema));                  \
        }                                                 \
      }                                                   \
    }
//  DUMP_SCHEMA(user, ObSimpleUserSchema, ConstUserIterator);
//  DUMP_SCHEMA(database, ObSimpleDatabaseSchema, ConstDatabaseIterator);
//  DUMP_SCHEMA(table, ObSimpleTableSchemaV2, ConstTableIterator);
//  DUMP_SCHEMA(index, ObSimpleTableSchemaV2, ConstTableIterator);
  #undef DUMP_SCHEMA
}

int ObSchemaMgr::get_schema_size(int64_t &total_size) const
{
  int ret = OB_SUCCESS;
  ObArray<ObSchemaStatisticsInfo> schema_infos;
  total_size = 0;
  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(get_schema_statistics(schema_infos))) {
    LOG_WARN("fail to get schema size", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < schema_infos.size(); i++) {
      ObSchemaStatisticsInfo &schema_statistics = schema_infos.at(i);
      if (schema_statistics.schema_type_ < SERVER_RUNTIME_SCHEMA
          || schema_statistics.schema_type_ >= OB_MAX_SCHEMA) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid schema type", K(ret), K(schema_statistics));
      } else {
        total_size += schema_statistics.size_;
      }
    }
  }
  return ret;
}

int ObSchemaMgr::get_schema_statistics(common::ObIArray<ObSchemaStatisticsInfo> &schema_infos) const
{
  int ret = OB_SUCCESS;
  ObSchemaStatisticsInfo schema_info;
  schema_infos.reset();
  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(get_runtime_statistics(schema_info))) {
    LOG_WARN("fail to get runtime statistics", K(ret));
  } else if (OB_FAIL(schema_infos.push_back(schema_info))) {
    LOG_WARN("fail to push back schema statistics", K(ret), K(schema_info));
  } else if (OB_FAIL(get_user_statistics(schema_info))) {
    LOG_WARN("fail to get user statistics", K(ret));
  } else if (OB_FAIL(schema_infos.push_back(schema_info))) {
    LOG_WARN("fail to push back schema statistics", K(ret), K(schema_info));
  } else if (OB_FAIL(get_database_statistics(schema_info))) {
    LOG_WARN("fail to get database statistics", K(ret));
  } else if (OB_FAIL(schema_infos.push_back(schema_info))) {
    LOG_WARN("fail to push back schema statistics", K(ret), K(schema_info));
  } else if (OB_FAIL(get_table_statistics(schema_info))) {
    LOG_WARN("fail to get table statistics", K(ret));
  } else if (OB_FAIL(schema_infos.push_back(schema_info))) {
    LOG_WARN("fail to push back schema statistics", K(ret), K(schema_info));
  } else if (OB_FAIL(outline_mgr_.get_schema_statistics(schema_info))) {
    LOG_WARN("fail to get outline statistics", K(ret));
  } else if (OB_FAIL(schema_infos.push_back(schema_info))) {
    LOG_WARN("fail to push back schema statistics", K(ret), K(schema_info));
  } else if (OB_FAIL(routine_mgr_.get_schema_statistics(schema_info))) {
    LOG_WARN("fail to get routine statistics", K(ret));
  } else if (OB_FAIL(schema_infos.push_back(schema_info))) {
    LOG_WARN("fail to push back schema statistics", K(ret), K(schema_info));
  } else if (OB_FAIL(priv_mgr_.get_schema_statistics(TABLE_PRIV, schema_info))) {
    LOG_WARN("fail to get table priv statistics", K(ret));
  } else if (OB_FAIL(schema_infos.push_back(schema_info))) {
    LOG_WARN("fail to push back schema statistics", K(ret), K(schema_info));
  } else if (OB_FAIL(priv_mgr_.get_schema_statistics(ROUTINE_PRIV, schema_info))) {
    LOG_WARN("fail to get table priv statistics", K(ret));
  } else if (OB_FAIL(schema_infos.push_back(schema_info))) {
    LOG_WARN("fail to push back schema statistics", K(ret), K(schema_info));
  } else if (OB_FAIL(priv_mgr_.get_schema_statistics(DATABASE_PRIV, schema_info))) {
    LOG_WARN("fail to get database priv statistics", K(ret));
  } else if (OB_FAIL(schema_infos.push_back(schema_info))) {
    LOG_WARN("fail to push back schema statistics", K(ret), K(schema_info));
  } else if (OB_FAIL(schema_infos.push_back(schema_info))) {
    LOG_WARN("fail to push back schema statistics", K(ret), K(schema_info));
  } else if (OB_FAIL(schema_infos.push_back(schema_info))) {
    LOG_WARN("fail to push back schema statistics", K(ret), K(schema_info));
  } else if (OB_FAIL(package_mgr_.get_schema_statistics(schema_info))) {
    LOG_WARN("fail to get package statistics", K(ret));
  } else if (OB_FAIL(schema_infos.push_back(schema_info))) {
    LOG_WARN("fail to push back schema statistics", K(ret), K(schema_info));
  } else if (OB_FAIL(trigger_mgr_.get_schema_statistics(schema_info))) {
    LOG_WARN("fail to get trigger statistics", K(ret));
  } else if (OB_FAIL(schema_infos.push_back(schema_info))) {
    LOG_WARN("fail to push back schema statistics", K(ret), K(schema_info));
  } else if (OB_FAIL(schema_infos.push_back(schema_info))) {
    LOG_WARN("fail to push back schema statistics", K(ret), K(schema_info));
  } else if (OB_FAIL(sys_variable_mgr_.get_schema_statistics(schema_info))) {
    LOG_WARN("fail to get sys variable statistics", K(ret));
  } else if (OB_FAIL(schema_infos.push_back(schema_info))) {
    LOG_WARN("fail to push back schema statistics", K(ret), K(schema_info));
  } else if (OB_FAIL(priv_mgr_.get_schema_statistics(SYS_PRIV, schema_info))) {
    LOG_WARN("fail to get system priv statistics", K(ret));
  } else if (OB_FAIL(schema_infos.push_back(schema_info))) {
    LOG_WARN("fail to push back schema statistics", K(ret), K(schema_info));
  } else if (OB_FAIL(priv_mgr_.get_schema_statistics(OBJ_PRIV, schema_info))) {
    LOG_WARN("fail to get obj priv statistics", K(ret));
  } else if (OB_FAIL(schema_infos.push_back(schema_info))) {
    LOG_WARN("fail to push back schema statistics", K(ret), K(schema_info));
  } else if (OB_FAIL(priv_mgr_.get_schema_statistics(COLUMN_PRIV, schema_info))) {
    LOG_WARN("fail to get column priv statistics", K(ret));
  } else if (OB_FAIL(schema_infos.push_back(schema_info))) {
    LOG_WARN("fail to push back schema statistics", K(ret), K(schema_info));
  } else if (OB_FAIL(schema_infos.push_back(schema_info))) {
    LOG_WARN("fail to push back schema statistics", K(ret), K(schema_info));
  } else if (OB_FAIL(mock_fk_parent_table_mgr_.get_schema_statistics(schema_info))) {
    LOG_WARN("fail to get mock_fk_parent_table statistics", K(ret));
  } else if (OB_FAIL(schema_infos.push_back(schema_info))) {
    LOG_WARN("fail to push back schema statistics", K(ret), K(schema_info));
  } else if (OB_FAIL(ai_model_mgr_.get_schema_statistics(schema_info))) {
    LOG_WARN("failed to get ai_model statistics", K(ret));
  } else if (OB_FAIL(schema_infos.push_back(schema_info))) {
    LOG_WARN("fail to push back schema statistics", K(ret), K(schema_info));
  }
  return ret;
}

int ObSchemaMgr::get_runtime_statistics(ObSchemaStatisticsInfo &schema_info) const
{
  int ret = OB_SUCCESS;
  schema_info.reset();
  schema_info.schema_type_ = SERVER_RUNTIME_SCHEMA;
  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    schema_info.count_ = (runtime_info_ != NULL ? 1 : 0);
    if (NULL != runtime_info_) {
      schema_info.size_ += runtime_info_->get_convert_size();
    }
  }
  return ret;
}

int ObSchemaMgr::get_user_statistics(ObSchemaStatisticsInfo &schema_info) const
{
  int ret = OB_SUCCESS;
  schema_info.reset();
  schema_info.schema_type_ = USER_SCHEMA;
  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    schema_info.count_ = user_infos_.size();
    for (ConstUserIterator it = user_infos_.begin(); OB_SUCC(ret) && it != user_infos_.end(); it++) {
      if (OB_ISNULL(*it)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("schema is null", K(ret));
      } else {
        schema_info.size_ += (*it)->get_convert_size();
      }
    }
  }
  return ret;
}

int ObSchemaMgr::get_database_statistics(ObSchemaStatisticsInfo &schema_info) const
{
  int ret = OB_SUCCESS;
  schema_info.reset();
  schema_info.schema_type_ = DATABASE_SCHEMA;
  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    schema_info.count_ = database_infos_.size();
    for (ConstDatabaseIterator it = database_infos_.begin(); OB_SUCC(ret) && it != database_infos_.end(); it++) {
      if (OB_ISNULL(*it)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("schema is null", K(ret));
      } else {
        schema_info.size_ += (*it)->get_convert_size();
      }
    }
  }
  return ret;
}

int ObSchemaMgr::get_table_statistics(ObSchemaStatisticsInfo &schema_info) const
{
  int ret = OB_SUCCESS;
  schema_info.reset();
  schema_info.schema_type_ = TABLE_SCHEMA;
  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    schema_info.count_ = table_infos_.size() + index_infos_.size() + lob_meta_infos_.size() + lob_piece_infos_.size();
    for (ConstTableIterator it = table_infos_.begin(); OB_SUCC(ret) && it != table_infos_.end(); it++) {
      if (OB_ISNULL(*it)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("schema is null", K(ret));
      } else {
        schema_info.size_ += (*it)->get_convert_size();
      }
    }
    for (ConstTableIterator it = index_infos_.begin(); OB_SUCC(ret) && it != index_infos_.end(); it++) {
      if (OB_ISNULL(*it)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("schema is null", K(ret));
      } else {
        schema_info.size_ += (*it)->get_convert_size();
      }
    }
    for (ConstTableIterator it = lob_meta_infos_.begin(); OB_SUCC(ret) && it != lob_meta_infos_.end(); it++) {
      if (OB_ISNULL(*it)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("schema is null", K(ret));
      } else {
        schema_info.size_ += (*it)->get_convert_size();
      }
    }
    for (ConstTableIterator it = lob_piece_infos_.begin(); OB_SUCC(ret) && it != lob_piece_infos_.end(); it++) {
      if (OB_ISNULL(*it)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("schema is null", K(ret));
      } else {
        schema_info.size_ += (*it)->get_convert_size();
      }
    }
  }
  return ret;
}

int ObSchemaMgr::get_ai_model_schema(
  const uint64_t &ai_model_id,
  const ObAiModelSchema *&ai_model_schema) const
{
  int ret = OB_SUCCESS;

  {
    ret = ai_model_mgr_.get_ai_model_schema(ai_model_id, ai_model_schema);
  }

  return ret;
}

int ObSchemaMgr::add_ai_models(const common::ObIArray<ObAiModelSchema> &ai_model_schemas)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; i < ai_model_schemas.count() && OB_SUCC(ret); ++i) {
    if (OB_FAIL(add_ai_model(ai_model_schemas.at(i)))) {
      LOG_WARN("push schema failed", K(ret));
    }
  }
  return ret;
}

int ObSchemaMgr::add_ai_model(const ObAiModelSchema &ai_model_schema)
{
  int ret = OB_SUCCESS;
  ObNameCaseMode mode = OB_NAME_CASE_INVALID;
  if (OB_FAIL(get_runtime_name_case_mode(mode))) {
    LOG_WARN("fail to get_runtime_name_case_mode", K(ret));
  } else if (OB_NAME_CASE_INVALID == mode) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid case mode", K(ret), K(mode));
  }

  if (OB_SUCC(ret) && OB_FAIL(ai_model_mgr_.add_ai_model(ai_model_schema, mode))) {
    LOG_WARN("fail to add ai model", K(ret));
  }
  return ret;
}

int ObSchemaMgr::del_ai_model(const ObAiModelId &ai_model_id)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ai_model_mgr_.del_ai_model(ai_model_id))) {
    LOG_WARN("fail to del ai model", K(ret));
  }
  return ret;
}

int ObSchemaMgr::get_ai_model_schema(
  const ObString &ai_model_name,
  const common::ObNameCaseMode &case_mode,
  const ObAiModelSchema *&ai_model_schema) const
{
  int ret = OB_SUCCESS;

  {
    ret = ai_model_mgr_.get_ai_model_schema( ai_model_name, case_mode, ai_model_schema);
  }

  return ret;
}

} //end of namespace schema
} //end of namespace share
} //end of namespace oceanbase
