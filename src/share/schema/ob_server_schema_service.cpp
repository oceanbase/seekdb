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

#include "ob_server_schema_service.h"
#include "share/ob_schema_status_proxy.h"
#include "share/ob_server_struct.h"
#include "share/ob_share_util.h"
#include "share/inner_table/ob_load_inner_table_schema.h"
#include "lib/statistic_event/ob_stat_event.h"
#include "lib/stat/ob_diagnostic_info_guard.h"
namespace oceanbase
{
namespace share
{
namespace schema
{
using namespace oceanbase::common;
using namespace oceanbase::common::hash;
using namespace oceanbase::common::sqlclient;

ObServerSchemaService::ObServerSchemaService()
    : schema_manager_rwlock_(common::ObLatchIds::SCHEMA_MGR_CACHE_LOCK),
      schema_service_(NULL),
      sql_proxy_(NULL),
      config_(NULL),
      schema_status_proxy_(NULL),
      service_status_(NULL),
      in_bootstrap_(NULL)
{
}

ObServerSchemaService::~ObServerSchemaService()
{
  destroy();
}

int ObServerSchemaService::destroy()
{
  int ret = OB_SUCCESS;
  // The concrete backend is owned by the Observer composition root.
  schema_service_ = NULL;
  // Each map held exactly one entry (sys). Mirror the per-entry
  // dtor on the single member, preserving the original FOREACH destroy (no leak).
  if (OB_SUCC(ret) && OB_NOT_NULL(schema_mgr_for_cache_)) {
    schema_mgr_for_cache_->~ObSchemaMgr();
    schema_mgr_for_cache_ = NULL;
  }
  if (OB_SUCC(ret) && OB_NOT_NULL(mem_mgr_)) {
    mem_mgr_->~ObSchemaMemMgr();
    mem_mgr_ = NULL;
  }
  return ret;
}

int ObServerSchemaService::init_runtime_basic_schema()
{
  int ret = OB_SUCCESS;
  SpinWLockGuard guard(schema_manager_rwlock_);
  ObSchemaMgr *schema_mgr_for_cache = NULL;
  if (FALSE_IT(schema_mgr_for_cache = ATOMIC_LOAD(&schema_mgr_for_cache_))) {
    LOG_WARN("fail to get schema mgr for cache", KR(ret));
  } else if (OB_ISNULL(schema_mgr_for_cache)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema mgr for cache is null", KR(ret));
  } else {
    const char *runtime_name = OB_SERVER_RUNTIME_NAME;
    ObSimpleServerRuntimeSchema runtime_schema;

    runtime_schema.set_runtime_name(ObString(runtime_name));
    runtime_schema.set_name_case_mode(OB_LOWERCASE_AND_INSENSITIVE);
    runtime_schema.set_read_only(false);
    runtime_schema.set_schema_version(OB_CORE_SCHEMA_VERSION);

    ObSimpleSysVariableSchema sys_variable;

    sys_variable.set_name_case_mode(OB_LOWERCASE_AND_INSENSITIVE);
    sys_variable.set_schema_version(OB_CORE_SCHEMA_VERSION);

    if (OB_FAIL(schema_mgr_for_cache->add_runtime_schema(runtime_schema))) {
    } else if (OB_FAIL(schema_mgr_for_cache->sys_variable_mgr_.add_sys_variable(sys_variable))) {
    } else if (OB_FAIL(fill_all_core_table_schema(*schema_mgr_for_cache))) {
    } else {
      // The initial runtime includes the root user schema.
      ObSimpleUserSchema user;

      user.set_user_id(OB_SYS_USER_ID);
      user.set_schema_version(OB_CORE_SCHEMA_VERSION);
      if (OB_FAIL(user.set_user_name(OB_SYS_USER_NAME))) {
      } else if (OB_FAIL(user.set_host(OB_SYS_HOST_NAME))) {
      } else if (OB_FAIL(schema_mgr_for_cache->add_user(user))) {
      }
    }
    if (OB_SUCC(ret)) {

      schema_mgr_for_cache->set_schema_version(OB_CORE_SCHEMA_VERSION);
    }
  }
  return ret;
}

int ObServerSchemaService::init(ObMySQLProxy *sql_proxy,
                                const ObCommonConfig *config,
                                ObSchemaStatusProxy &schema_status_proxy,
                                const ObServiceStatus &service_status,
                                bool &in_bootstrap,
                                ObSchemaService &schema_backend)
{
  int ret = OB_SUCCESS;
  auto attr = lib::ObMemAttr(ObModIds::OB_SCHEMA_ID_VERSIONS, ObCtxIds::SCHEMA_SERVICE);
  if (OB_ISNULL(sql_proxy)
     || NULL != schema_service_
     || !sql_proxy->is_inited()
     || OB_ISNULL(config)) {
    ret = OB_INIT_FAIL;
    LOG_WARN("check param failed", KR(ret), KP(sql_proxy), KP_(schema_service),
        KP(config));
  } else if (OB_FAIL(ObSysTableChecker::instance().init())) {
  } else if (FALSE_IT(schema_service_ = &schema_backend)) {
  } else if (OB_FAIL(schema_service_->init(sql_proxy, this))) {
  } else if (FALSE_IT(schema_service_->set_common_config(config))) {
    // will not reach here
  } else if (OB_FAIL(version_his_map_.create(
      common::calculate_scaled_value_by_memory(VERSION_HIS_MAP_BUCKET_NUM_MIN, VERSION_HIS_MAP_BUCKET_NUM_MAX),
      attr))) {
  } else {
    sql_proxy_ = sql_proxy;
    config_ = config;
    schema_status_proxy_ = &schema_status_proxy;
    service_status_ = &service_status;
    in_bootstrap_ = &in_bootstrap;
  }
  // initialize server runtime schema management
  if (OB_SUCC(ret)) {
    if (OB_FAIL(init_schema_struct())) {
    } else if (OB_FAIL(init_runtime_basic_schema())) {
    } else {
      LOG_INFO("init schema service", KR(ret));
    }
  }

  return ret;
}

int ObServerSchemaService::destroy_schema_struct()
{
  int ret = OB_SUCCESS;
  {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  }

  return ret;
}

bool ObServerSchemaService::check_inner_stat() const
{
  bool ret = true;
  if (NULL == schema_service_
      || NULL == sql_proxy_
      || NULL == config_) {
    ret = false;
    LOG_WARN("inner stat error", K(schema_service_),
             K(sql_proxy_), K(config_));
  }
  return ret;
}

int ObServerSchemaService::check_stop() const
{
  int ret = OB_SUCCESS;
  if (nullptr != service_status_
      && (ObServiceStatus::SS_STOPPING == *service_status_
          || ObServiceStatus::SS_STOPPED == *service_status_)) {
    ret = OB_SERVER_IS_STOPPING;
    LOG_WARN("observer is stopping", K(ret));
  }
  return ret;
}


int ObServerSchemaService::AllSchemaKeys::create(int64_t bucket_size)
{
  int ret = OB_SUCCESS;
  if (bucket_size <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("fail to create hashset,", K(bucket_size), K(ret));
  } else if (OB_FAIL(new_user_keys_.create(bucket_size))) {
  } else if (OB_FAIL(del_user_keys_.create(bucket_size))) {
  } else if (OB_FAIL(new_database_keys_.create(bucket_size))) {
  } else if (OB_FAIL(del_database_keys_.create(bucket_size))) {
  } else if (OB_FAIL(new_table_keys_.create(bucket_size))) {
  } else if (OB_FAIL(del_table_keys_.create(bucket_size))) {
  } else if (OB_FAIL(new_outline_keys_.create(bucket_size))) {
  } else if (OB_FAIL(del_outline_keys_.create(bucket_size))) {
  } else if (OB_FAIL(new_db_priv_keys_.create(bucket_size))) {
  } else if (OB_FAIL(del_db_priv_keys_.create(bucket_size))) {
  } else if (OB_FAIL(new_table_priv_keys_.create(bucket_size))) {
  } else if (OB_FAIL(del_table_priv_keys_.create(bucket_size))) {
  } else if (OB_FAIL(new_routine_priv_keys_.create(bucket_size))) {
  } else if (OB_FAIL(del_routine_priv_keys_.create(bucket_size))) {
  } else if (OB_FAIL(new_column_priv_keys_.create(bucket_size))) {
  } else if (OB_FAIL(del_column_priv_keys_.create(bucket_size))) {
  } else if (OB_FAIL(new_routine_keys_.create(bucket_size))) {
  } else if (OB_FAIL(del_routine_keys_.create(bucket_size))) {
  } else if (OB_FAIL(new_package_keys_.create(bucket_size))) {
  } else if (OB_FAIL(del_package_keys_.create(bucket_size))) {
  } else if (OB_FAIL(new_trigger_keys_.create(bucket_size))) {
  } else if (OB_FAIL(del_trigger_keys_.create(bucket_size))) {
  } else if (OB_FAIL(new_udt_keys_.create(bucket_size))) {
  } else if (OB_FAIL(del_udt_keys_.create(bucket_size))) {
  } else if (OB_FAIL(new_sys_variable_keys_.create(bucket_size))) {
  } else if (OB_FAIL(del_sys_variable_keys_.create(bucket_size))) {
  } else if (OB_FAIL(new_sys_priv_keys_.create(bucket_size))) {
  } else if (OB_FAIL(del_sys_priv_keys_.create(bucket_size))) {
  } else if (OB_FAIL(new_obj_priv_keys_.create(bucket_size))) {
  } else if (OB_FAIL(del_obj_priv_keys_.create(bucket_size))) {
  } else if (OB_FAIL(new_obj_mysql_priv_keys_.create(bucket_size))) {
  } else if (OB_FAIL(del_obj_mysql_priv_keys_.create(bucket_size))) {
  } else if (OB_FAIL(new_mock_fk_parent_table_keys_.create(bucket_size))) {
  } else if (OB_FAIL(del_mock_fk_parent_table_keys_.create(bucket_size))) {
  } else if (OB_FAIL(new_ai_model_keys_.create(bucket_size))) {
  } else if (OB_FAIL(del_ai_model_keys_.create(bucket_size))) {
  }
  return ret;
}


//////////////////////////////////////////////////////////////////////////////////////////////
//                              SCHEMA SERVICE RELATED                                      //
//////////////////////////////////////////////////////////////////////////////////////////////

ObSchemaService *ObServerSchemaService::get_schema_service(void) const
{
  return schema_service_;
}

#define REPLAY_OP(key, del_keys, new_keys, is_delete, is_exist)      \
  ({                                                                 \
    int ret = OB_SUCCESS;                           \
    int hash_ret = -1;                              \
    if (is_delete) {                                \
      hash_ret = new_keys.erase_refactored(key);                     \
      if (OB_SUCCESS != hash_ret && OB_HASH_NOT_EXIST != hash_ret) { \
        ret = OB_ERR_UNEXPECTED;                        \
        LOG_WARN("erase failed", K(hash_ret), K(ret));  \
      } else if (is_exist) {                            \
        hash_ret = del_keys.set_refactored(key);        \
        if (OB_SUCCESS != hash_ret) {                   \
          ret = OB_ERR_UNEXPECTED;                      \
          LOG_WARN("erase failed", K(hash_ret), K(ret));        \
        }                                           \
      }                                             \
    } else {                                        \
      hash_ret = new_keys.set_refactored(key);      \
      if (OB_SUCCESS != hash_ret) {                 \
        ret = OB_ERR_UNEXPECTED;                    \
        LOG_WARN("erase failed", K(hash_ret), K(ret));  \
      }                                             \
    }                                               \
    ret;                                            \
  })

int ObServerSchemaService::get_increment_sys_variable_keys(const ObSchemaMgr &schema_mgr,
                                                           const ObSchemaOperation &schema_operation,
                                                           AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;
  if (!(schema_operation.op_type_ > OB_DDL_SYS_VAR_OPERATION_BEGIN
        && schema_operation.op_type_ < OB_DDL_SYS_VAR_OPERATION_END)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(schema_operation.op_type_), KR(ret));
  } else {

    const int64_t schema_version = schema_operation.schema_version_;
    int hash_ret = OB_SUCCESS;
    SchemaKey schema_key;

    schema_key.schema_version_ = schema_version;
    //the server runtime schema is refreshed incrementally as well
    if (!schema_operation.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(schema_operation), KR(ret));
    } else {
      hash_ret = schema_keys.new_sys_variable_keys_.set_refactored_1(schema_key, 1);
      if (OB_SUCCESS != hash_ret) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to add new sys variable keys", K(hash_ret), KR(ret));
      }
    }
  }

  return ret;
}

int ObServerSchemaService::get_increment_sys_variable_keys_reversely(
    const ObSchemaMgr &schema_mgr,
    const ObSchemaOperation &schema_operation,
    AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;
  if (!(schema_operation.op_type_ > OB_DDL_SYS_VAR_OPERATION_BEGIN
        && schema_operation.op_type_ < OB_DDL_SYS_VAR_OPERATION_END)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(schema_operation.op_type_), KR(ret));
  } else {

    const int64_t schema_version = schema_operation.schema_version_;
    SchemaKey schema_key;

    schema_key.schema_version_ = schema_version;
    bool is_delete = false;
    bool is_exist = false;
    const ObSimpleSysVariableSchema *sys_variable = NULL;
    is_exist = false;
    if (OB_FAIL(schema_mgr.sys_variable_mgr_.get_sys_variable_schema( sys_variable))) {
    } else if (NULL != sys_variable) {
      is_exist = true;
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(REPLAY_OP(schema_key, schema_keys.del_sys_variable_keys_,
          schema_keys.new_sys_variable_keys_, is_delete, is_exist))) {
      }
    }
  }
  return ret;
}

int ObServerSchemaService::get_increment_user_keys(
  const ObSchemaMgr &schema_mgr,
  const ObSchemaOperation &schema_operation,
  AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;

  if (!(schema_operation.op_type_ > OB_DDL_USER_OPERATION_BEGIN
        && schema_operation.op_type_ < OB_DDL_USER_OPERATION_END)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(schema_operation.op_type_), KR(ret));
  } else {

    const uint64_t user_id = schema_operation.user_id_;
    const int64_t schema_version = schema_operation.schema_version_;
    int hash_ret = OB_SUCCESS;
    SchemaKey schema_key;

    schema_key.user_id_ = user_id;
    schema_key.schema_version_ = schema_version;
    if (!schema_operation.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(schema_operation), KR(ret));
    } else if (OB_DDL_DROP_USER == schema_operation.op_type_) {
      hash_ret = schema_keys.new_user_keys_.erase_refactored(schema_key);
      if (OB_SUCCESS != hash_ret && OB_HASH_NOT_EXIST != hash_ret) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Failed to del dropped user id", K(hash_ret), KR(ret));
      } else {
        const ObSimpleUserSchema *user = NULL;
        if (OB_FAIL(schema_mgr.get_user_schema( user_id, user))) {
        } else if (NULL != user) {
          hash_ret = schema_keys.del_user_keys_.set_refactored_1(schema_key, 1);
          if (OB_SUCCESS != hash_ret) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("Failed to add del user id", K(hash_ret), KR(ret));
          }
        }
      }
    } else {
      hash_ret = schema_keys.new_user_keys_.set_refactored_1(schema_key, 1);
      if (OB_SUCCESS != hash_ret) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Failed to add new user id", K(hash_ret), KR(ret));
      }
    }
  }

  return ret;
}

int ObServerSchemaService::get_increment_user_keys_reversely(
    const ObSchemaMgr &schema_mgr,
    const ObSchemaOperation &schema_operation,
    AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;
  if (!(schema_operation.op_type_ > OB_DDL_USER_OPERATION_BEGIN
        && schema_operation.op_type_ < OB_DDL_USER_OPERATION_END)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(schema_operation.op_type_), KR(ret));
  } else {

    const uint64_t user_id = schema_operation.user_id_;
    const int64_t schema_version = schema_operation.schema_version_;
    SchemaKey schema_key;

    schema_key.user_id_ = user_id;
    schema_key.schema_version_ = schema_version;
    bool is_delete = (OB_DDL_CREATE_USER == schema_operation.op_type_);
    bool is_exist = false;
    const ObSimpleUserSchema *user = NULL;
    if (OB_FAIL(schema_mgr.get_user_schema( user_id, user))) {
    } else if (NULL != user) {
      is_exist = true;
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(REPLAY_OP(schema_key, schema_keys.del_user_keys_,
          schema_keys.new_user_keys_, is_delete, is_exist))) {
      }
    }
  }
  return ret;
}

int ObServerSchemaService::get_increment_database_keys(
  const ObSchemaMgr &schema_mgr,
  const ObSchemaOperation &schema_operation,
  AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;

  if (!(schema_operation.op_type_ > OB_DDL_DATABASE_OPERATION_BEGIN
        && schema_operation.op_type_ < OB_DDL_DATABASE_OPERATION_END)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(schema_operation.op_type_), KR(ret));
  } else {

    const uint64_t db_id = schema_operation.database_id_;
    const int64_t schema_version = schema_operation.schema_version_;
    int hash_ret = OB_SUCCESS;
    SchemaKey schema_key;

    schema_key.database_id_ = db_id;
    schema_key.schema_version_ = schema_version;
    if (!schema_operation.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(schema_operation), KR(ret));
    } else if (OB_DDL_DEL_DATABASE == schema_operation.op_type_) {
      hash_ret = schema_keys.new_database_keys_.erase_refactored(schema_key);
      if (OB_SUCCESS != hash_ret && OB_HASH_NOT_EXIST != hash_ret) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to del dropped db id", K(hash_ret), KR(ret));
      } else {
        const ObSimpleDatabaseSchema *database = NULL;
        if (OB_FAIL(schema_mgr.get_database_schema( db_id, database))) {
        } else if (NULL != database) {
          hash_ret = schema_keys.del_database_keys_.set_refactored_1(schema_key, 1);
          if (OB_SUCCESS != hash_ret) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("failed to add del db id", K(hash_ret), KR(ret));
          }
        }
      }
    } else {
      hash_ret = schema_keys.new_database_keys_.set_refactored_1(schema_key, 1);
      if (OB_SUCCESS != hash_ret) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to add new database id", K(hash_ret), KR(ret));
      }
    }
  }

  return ret;
}

int ObServerSchemaService::get_increment_database_keys_reversely(
    const ObSchemaMgr &schema_mgr,
    const ObSchemaOperation &schema_operation,
    AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;
  if (!(schema_operation.op_type_ > OB_DDL_DATABASE_OPERATION_BEGIN
        && schema_operation.op_type_ < OB_DDL_DATABASE_OPERATION_END)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(schema_operation.op_type_), KR(ret));
  } else {

    const uint64_t database_id = schema_operation.database_id_;
    const int64_t schema_version = schema_operation.schema_version_;
    SchemaKey schema_key;

    schema_key.database_id_ = database_id;
    schema_key.schema_version_ = schema_version;
    bool is_delete = (OB_DDL_ADD_DATABASE == schema_operation.op_type_);
    bool is_exist = false;
    const ObSimpleDatabaseSchema *database = NULL;
    if (OB_FAIL(schema_mgr.get_database_schema( database_id, database))) {
    } else if (NULL != database) {
      is_exist = true;
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(REPLAY_OP(schema_key, schema_keys.del_database_keys_,
          schema_keys.new_database_keys_, is_delete, is_exist))) {
      }
    }
  }
  return ret;
}

int ObServerSchemaService::get_increment_table_keys(
  const ObSchemaMgr &schema_mgr,
  const ObSchemaOperation &schema_operation,
  AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;

  if (!(schema_operation.op_type_ > OB_DDL_TABLE_OPERATION_BEGIN
        && schema_operation.op_type_ < OB_DDL_TABLE_OPERATION_END)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(schema_operation.op_type_), KR(ret));
  } else if (OB_ALL_CORE_TABLE_TID == schema_operation.table_id_) {
    // won't load __all_core_table schema from inner_table
  } else {

    const uint64_t table_id = schema_operation.table_id_;
    const int64_t schema_version = schema_operation.schema_version_;
    int hash_ret = OB_SUCCESS;
    SchemaKey schema_key;

    schema_key.table_id_ = table_id;
    schema_key.schema_version_ = schema_version;
    if (OB_DDL_DROP_TABLE == schema_operation.op_type_
        || OB_DDL_DROP_INDEX == schema_operation.op_type_
        || OB_DDL_DROP_GLOBAL_INDEX == schema_operation.op_type_
        || OB_DDL_DROP_VIEW == schema_operation.op_type_
        || OB_DDL_TRUNCATE_TABLE_DROP == schema_operation.op_type_) {
      hash_ret = schema_keys.new_table_keys_.erase_refactored(schema_key);
      if (OB_SUCCESS != hash_ret && OB_HASH_NOT_EXIST != hash_ret) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to del dropped table id", K(hash_ret), KR(ret));
      } else {
        const ObSimpleTableSchemaV2 *table = NULL;
        if (OB_FAIL(schema_mgr.get_table_schema( table_id, table))) {
        } else if (NULL != table) {
          hash_ret = schema_keys.del_table_keys_.set_refactored_1(schema_key, 1);
          if (OB_SUCCESS != hash_ret) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("failed to add del table id", K(hash_ret), KR(ret));
          }
        }
      }
    } else {
      hash_ret = schema_keys.new_table_keys_.set_refactored_1(schema_key, 1);
      if (OB_SUCCESS != hash_ret) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to add new table id", K(hash_ret), KR(ret));
      }
    }
  }

  return ret;
}

int ObServerSchemaService::get_increment_table_keys_reversely(
    const ObSchemaMgr &schema_mgr,
    const ObSchemaOperation &schema_operation,
    AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;
  if (!(schema_operation.op_type_ > OB_DDL_TABLE_OPERATION_BEGIN
        && schema_operation.op_type_ < OB_DDL_TABLE_OPERATION_END)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(schema_operation.op_type_), KR(ret));
  } else if (OB_ALL_CORE_TABLE_TID == schema_operation.table_id_) {
    // won't load __all_core_table schema from inner_table
  } else {

    const uint64_t table_id = schema_operation.table_id_;
    const int64_t schema_version = schema_operation.schema_version_;
    SchemaKey schema_key;

    schema_key.table_id_ = table_id;
    schema_key.schema_version_ = schema_version;
    bool is_delete = (OB_DDL_CREATE_TABLE == schema_operation.op_type_
                      || OB_DDL_CREATE_INDEX == schema_operation.op_type_
                      || OB_DDL_CREATE_GLOBAL_INDEX == schema_operation.op_type_
                      || OB_DDL_CREATE_VIEW == schema_operation.op_type_
                      || OB_DDL_TRUNCATE_TABLE_CREATE == schema_operation.op_type_);
    bool is_exist = false;
    const ObSimpleTableSchemaV2 *table = NULL;
    if (OB_FAIL(schema_mgr.get_table_schema( table_id, table))) {
    } else if (NULL != table) {
      is_exist = true;
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(REPLAY_OP(schema_key, schema_keys.del_table_keys_,
          schema_keys.new_table_keys_, is_delete, is_exist))) {
      }
    }
  }
  return ret;
}

int ObServerSchemaService::get_increment_outline_keys(
  const ObSchemaMgr &schema_mgr,
  const ObSchemaOperation &schema_operation,
  AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;
  if (!(schema_operation.op_type_ > OB_DDL_OUTLINE_OPERATION_BEGIN
        && schema_operation.op_type_ < OB_DDL_OUTLINE_OPERATION_END)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(schema_operation.op_type_), KR(ret));
  } else {

    const uint64_t outline_id = schema_operation.outline_id_;
    const int64_t schema_version = schema_operation.schema_version_;
    int hash_ret = OB_SUCCESS;
    SchemaKey schema_key;

    schema_key.outline_id_ = outline_id;
    schema_key.schema_version_ = schema_version;
    if (OB_DDL_DROP_OUTLINE == schema_operation.op_type_) {
      hash_ret = schema_keys.new_outline_keys_.erase_refactored(schema_key);
      if (OB_SUCCESS != hash_ret && OB_HASH_NOT_EXIST != hash_ret) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to del dropped outline id", K(hash_ret), KR(ret));
      } else {
        const ObSimpleOutlineSchema *outline = NULL;
        if (OB_FAIL(schema_mgr.outline_mgr_.get_outline_schema(outline_id, outline))) {
        } else if (NULL != outline) {
          hash_ret = schema_keys.del_outline_keys_.set_refactored_1(schema_key, 1);
          if (OB_SUCCESS != hash_ret) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("failed to add del outline id", K(hash_ret), KR(ret));
          }
        }
      }
    } else {
      hash_ret = schema_keys.new_outline_keys_.set_refactored_1(schema_key, 1);
      if (OB_SUCCESS != hash_ret) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to add new outline id", K(hash_ret), KR(ret));
      }
    }
  }

  return ret;
}

int ObServerSchemaService::get_increment_outline_keys_reversely(
    const ObSchemaMgr &schema_mgr,
    const ObSchemaOperation &schema_operation,
    AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;
  if (!(schema_operation.op_type_ > OB_DDL_OUTLINE_OPERATION_BEGIN
        && schema_operation.op_type_ < OB_DDL_OUTLINE_OPERATION_END)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(schema_operation.op_type_), KR(ret));
  } else {

    const uint64_t outline_id = schema_operation.outline_id_;
    const int64_t schema_version = schema_operation.schema_version_;
    SchemaKey schema_key;

    schema_key.outline_id_ = outline_id;
    schema_key.schema_version_ = schema_version;
    bool is_delete = (OB_DDL_CREATE_OUTLINE == schema_operation.op_type_);
    bool is_exist = false;
    const ObSimpleOutlineSchema *outline = NULL;
    if (OB_FAIL(schema_mgr.outline_mgr_.get_outline_schema(outline_id, outline))) {
    } else if (NULL != outline) {
      is_exist = true;
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(REPLAY_OP(schema_key, schema_keys.del_outline_keys_,
          schema_keys.new_outline_keys_, is_delete, is_exist))) {
      }
    }
  }
  return ret;
}

int ObServerSchemaService::get_increment_routine_keys(
  const ObSchemaMgr &schema_mgr,
  const ObSchemaOperation &schema_operation,
  AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;

  if (!(schema_operation.op_type_ > OB_DDL_ROUTINE_OPERATION_BEGIN
        && schema_operation.op_type_ < OB_DDL_ROUTINE_OPERATION_END)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(schema_operation.op_type_), KR(ret));
  } else {

    uint64_t object_id = schema_operation.routine_id_;
    int64_t schema_version = schema_operation.schema_version_;
    int hash_ret = OB_SUCCESS;
    SchemaKey schema_key;

    schema_key.database_id_ = schema_operation.database_id_;
    schema_key.routine_id_ = object_id;
    schema_key.schema_version_ = schema_version;
    if (OB_DDL_DROP_ROUTINE == schema_operation.op_type_) {
      hash_ret = schema_keys.new_routine_keys_.erase_refactored(schema_key);
      if (OB_SUCCESS != hash_ret && OB_HASH_NOT_EXIST != hash_ret) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to del dropped routine id", K(hash_ret), KR(ret));
      } else {
        const ObSimpleRoutineSchema *routine = NULL;
        if (OB_FAIL(schema_mgr.routine_mgr_.get_routine_schema(object_id, routine))) {
        } else if (NULL != routine) {
          hash_ret = schema_keys.del_routine_keys_.set_refactored_1(schema_key, 1);
          if (OB_SUCCESS != hash_ret) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("failed to add del routine id", K(hash_ret), KR(ret));
          }
        }
      }
    } else {
      hash_ret = schema_keys.new_routine_keys_.set_refactored_1(schema_key, 1);
      if (OB_SUCCESS != hash_ret) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to add new routine id", K(hash_ret), KR(ret));
      }
    }
  }

  return ret;
}

int ObServerSchemaService::get_increment_routine_keys_reversely(
  const ObSchemaMgr &schema_mgr,
  const ObSchemaOperation &schema_operation,
  AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;
  if (!(schema_operation.op_type_ > OB_DDL_ROUTINE_OPERATION_BEGIN
        && schema_operation.op_type_ < OB_DDL_ROUTINE_OPERATION_END)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(schema_operation.op_type_), KR(ret));
  } else {

    uint64_t routine_id = schema_operation.routine_id_;
    int64_t schema_version = schema_operation.schema_version_;
    SchemaKey schema_key;

    schema_key.routine_id_ = routine_id;
    schema_key.schema_version_ = schema_version;
    bool is_delete = (OB_DDL_CREATE_ROUTINE == schema_operation.op_type_);
    bool is_exist = false;
    const ObSimpleRoutineSchema *routine = NULL;
    if (OB_FAIL(schema_mgr.routine_mgr_.get_routine_schema(routine_id, routine))) {
    } else if (NULL != routine) {
      is_exist = true;
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(REPLAY_OP(schema_key, schema_keys.del_routine_keys_,
          schema_keys.new_routine_keys_, is_delete, is_exist))) {
      }
    }
  }
  return ret;
}

int ObServerSchemaService::get_increment_package_keys(
    const ObSchemaMgr &schema_mgr,
    const ObSchemaOperation &schema_operation,
    AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;

  if (!(schema_operation.op_type_ > OB_DDL_PACKAGE_OPERATION_BEGIN
        && schema_operation.op_type_ < OB_DDL_PACKAGE_OPERATION_END)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(schema_operation.op_type_), KR(ret));
  } else {

    uint64_t package_id = schema_operation.package_id_;
    int64_t schema_version = schema_operation.schema_version_;
    int hash_ret = OB_SUCCESS;
    SchemaKey schema_key;

    schema_key.database_id_ = schema_operation.database_id_;
    schema_key.package_id_ = package_id;
    schema_key.schema_version_ = schema_version;
    if (OB_DDL_DROP_PACKAGE == schema_operation.op_type_) {
      hash_ret = schema_keys.new_package_keys_.erase_refactored(schema_key);
      if (OB_SUCCESS != hash_ret && OB_HASH_NOT_EXIST != hash_ret) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to del dropped package id", K(hash_ret), KR(ret));
      } else {
        const ObSimplePackageSchema *package = NULL;
        if (OB_FAIL(schema_mgr.package_mgr_.get_package_schema(package_id, package))) {
        } else if (NULL != package) {
          hash_ret = schema_keys.del_package_keys_.set_refactored_1(schema_key, 1);
          if (OB_SUCCESS != hash_ret) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("failed to add del package id", K(hash_ret), KR(ret));
          }
        }
      }
    } else {
      hash_ret = schema_keys.new_package_keys_.set_refactored_1(schema_key, 1);
      if (OB_SUCCESS != hash_ret) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to add new package id", K(hash_ret), KR(ret));
      }
    }
  }
  return ret;
}

int ObServerSchemaService::get_increment_package_keys_reversely(
    const ObSchemaMgr &schema_mgr,
    const ObSchemaOperation &schema_operation,
    AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;
  if (!(schema_operation.op_type_ > OB_DDL_PACKAGE_OPERATION_BEGIN
        && schema_operation.op_type_ < OB_DDL_PACKAGE_OPERATION_END)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(schema_operation.op_type_), KR(ret));
  } else {

    uint64_t package_id = schema_operation.package_id_;
    int64_t schema_version = schema_operation.schema_version_;
    SchemaKey schema_key;

    schema_key.package_id_ = package_id;
    schema_key.schema_version_ = schema_version;
    bool is_delete = (OB_DDL_CREATE_PACKAGE == schema_operation.op_type_);
    bool is_exist = false;
    const ObSimplePackageSchema *package = NULL;
    if (OB_FAIL(schema_mgr.package_mgr_.get_package_schema(package_id, package))) {
    } else if (NULL != package) {
      is_exist = true;
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(REPLAY_OP(schema_key, schema_keys.del_package_keys_,
          schema_keys.new_package_keys_, is_delete, is_exist))) {
      }
    }
  }
  return ret;
}

int ObServerSchemaService::get_increment_trigger_keys(
    const ObSchemaMgr &schema_mgr,
    const ObSchemaOperation &schema_operation,
    AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;

  if (!(schema_operation.op_type_ > OB_DDL_TRIGGER_OPERATION_BEGIN
        && schema_operation.op_type_ < OB_DDL_TRIGGER_OPERATION_END)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(schema_operation.op_type_), KR(ret));
  } else {

    uint64_t trigger_id = schema_operation.trigger_id_;
    int64_t schema_version = schema_operation.schema_version_;
    int hash_ret = OB_SUCCESS;
    SchemaKey schema_key;

    schema_key.database_id_ = schema_operation.database_id_;
    schema_key.trigger_id_ = trigger_id;
    schema_key.schema_version_ = schema_version;
    if (OB_DDL_DROP_TRIGGER == schema_operation.op_type_) {
      hash_ret = schema_keys.new_trigger_keys_.erase_refactored(schema_key);
      if (OB_SUCCESS != hash_ret && OB_HASH_NOT_EXIST != hash_ret) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to del dropped trigger id", K(hash_ret), KR(ret));
      } else {
        const ObSimpleTriggerSchema *trigger = NULL;
        if (OB_FAIL(schema_mgr.trigger_mgr_.get_trigger_schema(trigger_id, trigger))) {
        } else if (NULL != trigger) {
          hash_ret = schema_keys.del_trigger_keys_.set_refactored_1(schema_key, 1);
          if (OB_SUCCESS != hash_ret) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("failed to add del trigger id", K(hash_ret), KR(ret));
          }
        }
      }
    } else {
      hash_ret = schema_keys.new_trigger_keys_.set_refactored_1(schema_key, 1);
      if (OB_SUCCESS != hash_ret) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to add new trigger id", K(hash_ret), KR(ret));
      }
    }
  }
  return ret;
}

int ObServerSchemaService::get_increment_trigger_keys_reversely(
    const ObSchemaMgr &schema_mgr,
    const ObSchemaOperation &schema_operation,
    AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;
  if (!(schema_operation.op_type_ > OB_DDL_TRIGGER_OPERATION_BEGIN
        && schema_operation.op_type_ < OB_DDL_TRIGGER_OPERATION_END)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(schema_operation.op_type_), KR(ret));
  } else {

    uint64_t trigger_id = schema_operation.trigger_id_;
    int64_t schema_version = schema_operation.schema_version_;
    SchemaKey schema_key;

    schema_key.trigger_id_ = trigger_id;
    schema_key.schema_version_ = schema_version;
    bool is_delete = (OB_DDL_CREATE_TRIGGER == schema_operation.op_type_);
    bool is_exist = false;
    const ObSimpleTriggerSchema *trigger = NULL;
    if (OB_FAIL(schema_mgr.trigger_mgr_.get_trigger_schema(trigger_id, trigger))) {
    } else if (NULL != trigger) {
      is_exist = true;
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(REPLAY_OP(schema_key, schema_keys.del_trigger_keys_,
          schema_keys.new_trigger_keys_, is_delete, is_exist))) {
      }
    }
  }
  return ret;
}

int ObServerSchemaService::get_increment_db_priv_keys(
  const ObSchemaMgr &schema_mgr,
  const ObSchemaOperation &schema_operation,
  AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;

  if (!(schema_operation.op_type_ > OB_DDL_DB_PRIV_OPERATION_BEGIN
        && schema_operation.op_type_ < OB_DDL_DB_PRIV_OPERATION_END)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(schema_operation.op_type_), KR(ret));
  } else {

    const uint64_t user_id = schema_operation.user_id_;
    const ObString &database_name = schema_operation.database_name_;
    const int64_t schema_version = schema_operation.schema_version_;
    int hash_ret = OB_SUCCESS;
    SchemaKey db_priv_key;

    db_priv_key.user_id_ = user_id;
    db_priv_key.database_name_ = database_name;
    db_priv_key.schema_version_ = schema_version;
    if (OB_DDL_DEL_DB_PRIV == schema_operation.op_type_) { //delete
      hash_ret = schema_keys.new_db_priv_keys_.erase_refactored(db_priv_key);
      if (OB_SUCCESS != hash_ret && OB_HASH_NOT_EXIST != hash_ret) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Failed to del db_priv_key from new_db_priv_keys", KR(ret));
      } else {
        const ObDBPriv *db_priv = NULL;
        if (OB_FAIL(schema_mgr.priv_mgr_.get_db_priv(
            ObOriginalDBKey(user_id, database_name), db_priv, true))) {
        } else if (NULL != db_priv) {
          hash_ret = schema_keys.del_db_priv_keys_.set_refactored_1(db_priv_key, 1);
          if (OB_SUCCESS != hash_ret) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("Failed to add db_priv_key to del_db_priv_keys", K(hash_ret), KR(ret));
          }
        }
      }
    } else {
      hash_ret = schema_keys.new_db_priv_keys_.set_refactored_1(db_priv_key, 1);
      if (OB_SUCCESS != hash_ret) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Failed to add new db_priv_key", K(hash_ret), KR(ret));
      }
    }
  }

  return ret;
}

int ObServerSchemaService::get_increment_db_priv_keys_reversely(
    const ObSchemaMgr &schema_mgr,
    const ObSchemaOperation &schema_operation,
    AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;
  if (!(schema_operation.op_type_ > OB_DDL_DB_PRIV_OPERATION_BEGIN
        && schema_operation.op_type_ < OB_DDL_DB_PRIV_OPERATION_END)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(schema_operation.op_type_), KR(ret));
  } else {

    const uint64_t user_id = schema_operation.user_id_;
    const ObString &database_name = schema_operation.database_name_;
    const int64_t schema_version = schema_operation.schema_version_;
    SchemaKey schema_key;

    schema_key.user_id_ = user_id;
    schema_key.database_name_ = database_name;
    schema_key.schema_version_ = schema_version;
    bool is_delete = (OB_DDL_GRANT_REVOKE_DB == schema_operation.op_type_);
    bool is_exist = false;
    const ObDBPriv *db_priv = NULL;
    if (OB_FAIL(schema_mgr.priv_mgr_.get_db_priv(schema_key.get_db_priv_key(), db_priv, true))) {
    } else if (NULL != db_priv) {
      is_exist = true;
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(REPLAY_OP(schema_key, schema_keys.del_db_priv_keys_,
          schema_keys.new_db_priv_keys_, is_delete, is_exist))) {
      }
    }
  }
  return ret;
}

int ObServerSchemaService::get_increment_sys_priv_keys(
  const ObSchemaMgr &schema_mgr,
  const ObSchemaOperation &schema_operation,
  AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;
  if (!(schema_operation.op_type_ > OB_DDL_SYS_PRIV_OPERATION_BEGIN
        && schema_operation.op_type_ < OB_DDL_SYS_PRIV_OPERATION_END)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(schema_operation.op_type_), KR(ret));
  } else {

    const uint64_t grantee_id = schema_operation.grantee_id_;

    const int64_t schema_version = schema_operation.schema_version_;
    int hash_ret = OB_SUCCESS;
    SchemaKey sys_priv_key;

    sys_priv_key.grantee_id_ = grantee_id;
    sys_priv_key.schema_version_ = schema_version;
    if (OB_DDL_SYS_PRIV_DELETE == schema_operation.op_type_) { //delete
      hash_ret = schema_keys.new_sys_priv_keys_.erase_refactored(sys_priv_key);
      if (OB_SUCCESS != hash_ret && OB_HASH_NOT_EXIST != hash_ret) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Failed to del sys_priv_key from new_sys_priv_keys", KR(ret));
      } else {
        const ObSysPriv *sys_priv = NULL;
        if (OB_FAIL(schema_mgr.priv_mgr_.get_sys_priv(
            ObSysPrivKey(grantee_id), sys_priv))) {
        } else if (NULL != sys_priv) {
          hash_ret = schema_keys.del_sys_priv_keys_.set_refactored_1(sys_priv_key, 1);
          if (OB_SUCCESS != hash_ret) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("Failed to add sys_priv_key to del_sys_priv_keys", K(hash_ret), KR(ret));
          }
        }
      }
    } else {
      hash_ret = schema_keys.new_sys_priv_keys_.set_refactored_1(sys_priv_key, 1);
      if (OB_SUCCESS != hash_ret) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Failed to add new sys_priv_key", K(hash_ret), KR(ret));
      }
    }
  }

  return ret;
}

int ObServerSchemaService::get_increment_sys_priv_keys_reversely(
  const ObSchemaMgr &schema_mgr,
  const ObSchemaOperation &schema_operation,
  AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;
  if (!(schema_operation.op_type_ > OB_DDL_SYS_PRIV_OPERATION_BEGIN
        && schema_operation.op_type_ < OB_DDL_SYS_PRIV_OPERATION_END)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(schema_operation.op_type_), KR(ret));
  } else {

    const uint64_t grantee_id = schema_operation.grantee_id_;
    const int64_t schema_version = schema_operation.schema_version_;
    SchemaKey schema_key;

    schema_key.grantee_id_ = grantee_id;
    schema_key.schema_version_ = schema_version;
    bool is_delete = OB_DDL_SYS_PRIV_GRANT_REVOKE == schema_operation.op_type_;
    bool is_exist = false;
    const ObSysPriv *sys_priv = NULL;
    if (OB_FAIL(schema_mgr.priv_mgr_.get_sys_priv(schema_key.get_sys_priv_key(), sys_priv))) {
    } else if (NULL != sys_priv) {
      is_exist = true;
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(REPLAY_OP(schema_key, schema_keys.del_sys_priv_keys_,
          schema_keys.new_sys_priv_keys_, is_delete, is_exist))) {
      }
    }
  }
  return ret;
}

int ObServerSchemaService::get_increment_table_priv_keys(
  const ObSchemaMgr &schema_mgr,
  const ObSchemaOperation &schema_operation,
  AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;

  if (!(schema_operation.op_type_ > OB_DDL_TABLE_PRIV_OPERATION_BEGIN
        && schema_operation.op_type_ < OB_DDL_TABLE_PRIV_OPERATION_END)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(schema_operation.op_type_), KR(ret));
  } else {

    const uint64_t user_id = schema_operation.user_id_;
    const ObString &database_name = schema_operation.database_name_;
    const ObString &table_name = schema_operation.table_name_;
    const int64_t schema_version = schema_operation.schema_version_;
    int hash_ret = OB_SUCCESS;
    SchemaKey table_priv_key;

    table_priv_key.user_id_ = user_id;
    table_priv_key.database_name_ = database_name;
    table_priv_key.table_name_ = table_name;
    table_priv_key.schema_version_ = schema_version;
    if (OB_DDL_DEL_TABLE_PRIV == schema_operation.op_type_) { //delete
      hash_ret = schema_keys.new_table_priv_keys_.erase_refactored(table_priv_key);
      if (OB_SUCCESS != hash_ret && OB_HASH_NOT_EXIST != hash_ret) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Failed to del table_priv_key from new_table_priv_keys", KR(ret));
      } else {
        const ObTablePriv *table_priv = NULL;
        if (OB_FAIL(schema_mgr.priv_mgr_.get_table_priv(
            ObTablePrivSortKey(user_id, database_name, table_name), table_priv))) {
        } else if (NULL != table_priv) {
          hash_ret = schema_keys.del_table_priv_keys_.set_refactored_1(table_priv_key, 1);
          if (OB_SUCCESS != hash_ret) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("Failed to add table_priv_key to del_table_priv_keys", KR(ret));
          }
        }
      }
    } else {
      hash_ret = schema_keys.new_table_priv_keys_.set_refactored_1(table_priv_key, 1);
      if (OB_SUCCESS != hash_ret) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Failed to add new table_priv_key", KR(ret));
      }
    }
  }

  return ret;
}

int ObServerSchemaService::get_increment_table_priv_keys_reversely(
  const ObSchemaMgr &schema_mgr,
  const ObSchemaOperation &schema_operation,
  AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;
  if (!(schema_operation.op_type_ > OB_DDL_TABLE_PRIV_OPERATION_BEGIN
        && schema_operation.op_type_ < OB_DDL_TABLE_PRIV_OPERATION_END)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(schema_operation.op_type_), KR(ret));
  } else {

    const uint64_t user_id = schema_operation.user_id_;
    const ObString &database_name = schema_operation.database_name_;
    const ObString &table_name = schema_operation.table_name_;
    const int64_t schema_version = schema_operation.schema_version_;
    SchemaKey schema_key;

    schema_key.user_id_ = user_id;
    schema_key.database_name_ = database_name;
    schema_key.table_name_ = table_name;
    schema_key.schema_version_ = schema_version;
    bool is_delete = (OB_DDL_GRANT_REVOKE_TABLE == schema_operation.op_type_);
    bool is_exist = false;
    const ObTablePriv *table_priv = NULL;
    if (OB_FAIL(schema_mgr.priv_mgr_.get_table_priv(schema_key.get_table_priv_key(),
                                                    table_priv))) {
    } else if (NULL != table_priv) {
      is_exist = true;
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(REPLAY_OP(schema_key, schema_keys.del_table_priv_keys_,
          schema_keys.new_table_priv_keys_, is_delete, is_exist))) {
      }
    }
  }
  return ret;
}

int ObServerSchemaService::get_increment_routine_priv_keys(
  const ObSchemaMgr &schema_mgr,
  const ObSchemaOperation &schema_operation,
  AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;

  if (!(schema_operation.op_type_ > OB_DDL_ROUTINE_PRIV_OPERATION_BEGIN
        && schema_operation.op_type_ < OB_DDL_ROUTINE_PRIV_OPERATION_END)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(schema_operation.op_type_), KR(ret));
  } else {

    const uint64_t user_id = schema_operation.user_id_;
    const ObString &database_name = schema_operation.database_name_;
    const ObString &routine_name = schema_operation.routine_name_;
    const int64_t routine_type = schema_operation.routine_type_;
    const int64_t schema_version = schema_operation.schema_version_;
    int hash_ret = OB_SUCCESS;
    SchemaKey routine_priv_key;

    routine_priv_key.user_id_ = user_id;
    routine_priv_key.database_name_ = database_name;
    routine_priv_key.routine_name_ = routine_name;
    routine_priv_key.obj_type_ = routine_type;
    routine_priv_key.schema_version_ = schema_version;
    if (OB_DDL_DEL_ROUTINE_PRIV == schema_operation.op_type_) { //delete
      hash_ret = schema_keys.new_routine_priv_keys_.erase_refactored(routine_priv_key);
      if (OB_SUCCESS != hash_ret && OB_HASH_NOT_EXIST != hash_ret) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Failed to del routine_priv_key from new_routine_priv_keys", KR(ret));
      } else {
        const ObRoutinePriv *routine_priv = NULL;
        if (OB_FAIL(schema_mgr.priv_mgr_.get_routine_priv(
            ObRoutinePrivSortKey(user_id, database_name, routine_name, routine_type), routine_priv))) {
        } else if (NULL != routine_priv) {
          hash_ret = schema_keys.del_routine_priv_keys_.set_refactored_1(routine_priv_key, 1);
          if (OB_SUCCESS != hash_ret) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("Failed to add routine_priv_key to del_routine_priv_keys", KR(ret));
          }
        }
      }
    } else {
      hash_ret = schema_keys.new_routine_priv_keys_.set_refactored_1(routine_priv_key, 1);
      if (OB_SUCCESS != hash_ret) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Failed to add new routine_priv_key", KR(ret));
      }
    }
  }

  return ret;
}

int ObServerSchemaService::get_increment_routine_priv_keys_reversely(
  const ObSchemaMgr &schema_mgr,
  const ObSchemaOperation &schema_operation,
  AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;
  if (!(schema_operation.op_type_ > OB_DDL_ROUTINE_PRIV_OPERATION_BEGIN
        && schema_operation.op_type_ < OB_DDL_ROUTINE_PRIV_OPERATION_END)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(schema_operation.op_type_), KR(ret));
  } else {

    const uint64_t user_id = schema_operation.user_id_;
    const ObString &database_name = schema_operation.database_name_;
    const ObString &routine_name = schema_operation.routine_name_;
    const int64_t routine_type = schema_operation.routine_type_;
    const int64_t schema_version = schema_operation.schema_version_;
    SchemaKey schema_key;

    schema_key.user_id_ = user_id;
    schema_key.database_name_ = database_name;
    schema_key.routine_name_ = routine_name;
    schema_key.obj_type_ = routine_type;
    schema_key.schema_version_ = schema_version;
    bool is_delete = (OB_DDL_GRANT_ROUTINE_PRIV == schema_operation.op_type_);
    bool is_exist = false;
    const ObRoutinePriv *routine_priv = NULL;
    if (OB_FAIL(schema_mgr.priv_mgr_.get_routine_priv(schema_key.get_routine_priv_key(),
                                                    routine_priv))) {
    } else if (NULL != routine_priv) {
      is_exist = true;
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(REPLAY_OP(schema_key, schema_keys.del_routine_priv_keys_,
          schema_keys.new_routine_priv_keys_, is_delete, is_exist))) {
      }
    }
  }
  return ret;
}

int ObServerSchemaService::get_increment_column_priv_keys(
  const ObSchemaMgr &schema_mgr,
  const ObSchemaOperation &schema_operation,
  AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;

  if (!(schema_operation.op_type_ > OB_DDL_COLUMN_PRIV_OPERATION_BEGIN
        && schema_operation.op_type_ < OB_DDL_COLUMN_PRIV_OPERATION_END)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(schema_operation.op_type_), KR(ret));
  } else {

    const uint64_t column_priv_id = schema_operation.column_priv_id_;
    const int64_t schema_version = schema_operation.schema_version_;
    int hash_ret = OB_SUCCESS;
    SchemaKey column_priv_key;

    column_priv_key.column_priv_id_ = column_priv_id;
    column_priv_key.schema_version_ = schema_version;
    const ObColumnPriv *column_priv = NULL;
    if (OB_FAIL(schema_mgr.priv_mgr_.get_column_priv_by_id( column_priv_id, column_priv))) {
    } else if (OB_DDL_DEL_COLUMN_PRIV == schema_operation.op_type_) { //delete
      hash_ret = schema_keys.new_column_priv_keys_.erase_refactored(column_priv_key);
      if (OB_SUCCESS != hash_ret && OB_HASH_NOT_EXIST != hash_ret) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Failed to del column_priv_key from new_column_priv_keys", KR(ret));
      } else {
        if (NULL != column_priv) {
          hash_ret = schema_keys.del_column_priv_keys_.set_refactored_1(column_priv_key, 1);
          if (OB_SUCCESS != hash_ret) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("Failed to add column_priv_key to del_column_priv_keys", KR(ret));
          }
        }
      }
    } else {
      hash_ret = schema_keys.new_column_priv_keys_.set_refactored_1(column_priv_key, 1);
      if (OB_SUCCESS != hash_ret) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Failed to add new column_priv_key", KR(ret));
      }
    }
  }

  return ret;
}

int ObServerSchemaService::get_increment_column_priv_keys_reversely(
  const ObSchemaMgr &schema_mgr,
  const ObSchemaOperation &schema_operation,
  AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;
  if (!(schema_operation.op_type_ > OB_DDL_COLUMN_PRIV_OPERATION_BEGIN
        && schema_operation.op_type_ < OB_DDL_COLUMN_PRIV_OPERATION_END)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(schema_operation.op_type_), KR(ret));
  } else {

    const uint64_t column_priv_id = schema_operation.column_priv_id_;
    const int64_t schema_version = schema_operation.schema_version_;
    SchemaKey schema_key;

    schema_key.column_priv_id_ = column_priv_id;
    schema_key.schema_version_ = schema_version;
    bool is_delete = (OB_DDL_GRANT_COLUMN_PRIV == schema_operation.op_type_);
    bool is_exist = false;
    const ObColumnPriv *column_priv = NULL;
    if (NULL != column_priv) {
      is_exist = true;
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(REPLAY_OP(schema_key, schema_keys.del_column_priv_keys_,
          schema_keys.new_column_priv_keys_, is_delete, is_exist))) {
      }
    }
  }
  return ret;
}

int ObServerSchemaService::get_increment_obj_priv_keys(
  const ObSchemaMgr &schema_mgr,
  const ObSchemaOperation &schema_operation,
  AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;

  if (!(schema_operation.op_type_ > OB_DDL_OBJ_PRIV_OPERATION_BEGIN
        && schema_operation.op_type_ < OB_DDL_OBJ_PRIV_OPERATION_END)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(schema_operation.op_type_), KR(ret));
  } else {

    const uint64_t obj_id = schema_operation.get_obj_id();
    const uint64_t obj_type = schema_operation.get_obj_type();
    const uint64_t col_id = schema_operation.get_col_id();
    const uint64_t grantee_id = schema_operation.get_grantee_id();
    const uint64_t grantor_id = schema_operation.get_grantor_id();
    const int64_t schema_version = schema_operation.schema_version_;
    int hash_ret = OB_SUCCESS;
    SchemaKey obj_priv_key;

    obj_priv_key.table_id_ = obj_id;
    obj_priv_key.obj_type_ = obj_type;
    obj_priv_key.col_id_ = col_id;
    obj_priv_key.grantee_id_ = grantee_id;
    obj_priv_key.grantor_id_ = grantor_id;
    obj_priv_key.schema_version_ = schema_version;
    if (OB_DDL_OBJ_PRIV_DELETE == schema_operation.op_type_) { //delete
      hash_ret = schema_keys.new_obj_priv_keys_.erase_refactored(obj_priv_key);
      if (OB_SUCCESS != hash_ret && OB_HASH_NOT_EXIST != hash_ret) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Failed to del table_priv_key from new_obj_priv_keys", KR(ret));
      } else {
        const ObObjPriv *obj_priv = NULL;
        if (OB_FAIL(schema_mgr.priv_mgr_.get_obj_priv(
            ObObjPrivSortKey(obj_id, obj_type, col_id, grantor_id, grantee_id),
            obj_priv))) {
        } else if (NULL != obj_priv) {
          hash_ret = schema_keys.del_obj_priv_keys_.set_refactored_1(obj_priv_key, 1);
          if (OB_SUCCESS != hash_ret) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("Failed to add obj_priv_key to del_obj_priv_keys", KR(ret));
          }
        }
      }
    } else {
      hash_ret = schema_keys.new_obj_priv_keys_.set_refactored_1(obj_priv_key, 1);
      if (OB_SUCCESS != hash_ret) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Failed to add new obj_priv_key", KR(ret));
      }
    }
  }

  return ret;
}

int ObServerSchemaService::get_increment_obj_priv_keys_reversely(
  const ObSchemaMgr &schema_mgr,
  const ObSchemaOperation &schema_operation,
  AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;
  if (!(schema_operation.op_type_ > OB_DDL_OBJ_PRIV_OPERATION_BEGIN
        && schema_operation.op_type_ < OB_DDL_OBJ_PRIV_OPERATION_END)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(schema_operation.op_type_), KR(ret));
  } else {

    const uint64_t obj_id = schema_operation.get_obj_id();
    const uint64_t obj_type = schema_operation.get_obj_type();
    const uint64_t col_id = schema_operation.get_col_id();
    const uint64_t grantee_id = schema_operation.get_grantee_id();
    const uint64_t grantor_id = schema_operation.get_grantor_id();
    const int64_t schema_version = schema_operation.schema_version_;
    SchemaKey schema_key;

    schema_key.table_id_ = obj_id;
    schema_key.obj_type_ = obj_type;
    schema_key.col_id_ = col_id;
    schema_key.grantee_id_ = grantee_id;
    schema_key.grantor_id_ = grantor_id;
    schema_key.schema_version_ = schema_version;

    bool is_delete = (OB_DDL_OBJ_PRIV_GRANT_REVOKE == schema_operation.op_type_);
    bool is_exist = false;
    const ObObjPriv *obj_priv = NULL;
    if (OB_FAIL(schema_mgr.priv_mgr_.get_obj_priv(schema_key.get_obj_priv_key(),
                                                  obj_priv))) {
    } else if (NULL != obj_priv) {
      is_exist = true;
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(REPLAY_OP(schema_key, schema_keys.del_obj_priv_keys_,
          schema_keys.new_obj_priv_keys_, is_delete, is_exist))) {
      }
    }
  }
  return ret;
}

int ObServerSchemaService::get_increment_mock_fk_parent_table_keys(
    const ObSchemaMgr &schema_mgr,
    const ObSchemaOperation &schema_operation,
    AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;
  if (!(schema_operation.op_type_ > OB_DDL_MOCK_FK_PARENT_TABLE_OPERATION_BEGIN
        && schema_operation.op_type_ < OB_DDL_MOCK_FK_PARENT_TABLE_OPERATION_END)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", KR(ret), K(schema_operation.op_type_));
  } else {

    int64_t schema_version = schema_operation.schema_version_;
    int hash_ret = OB_SUCCESS;
    SchemaKey schema_key;

    schema_key.schema_version_ = schema_version;
    schema_key.database_id_ = schema_operation.database_id_;
    schema_key.mock_fk_parent_table_id_ = schema_operation.mock_fk_parent_table_id_;
    if (OB_DDL_DROP_MOCK_FK_PARENT_TABLE == schema_operation.op_type_) {
      hash_ret = schema_keys.new_mock_fk_parent_table_keys_.erase_refactored(schema_key);
      if (OB_SUCCESS != hash_ret && OB_HASH_NOT_EXIST != hash_ret) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to del dropped mock_fk_parent_table", KR(ret), K(hash_ret));
      } else {
        const ObSimpleMockFKParentTableSchema *mock_fk_parent_table = NULL;
        if (OB_FAIL(schema_mgr.mock_fk_parent_table_mgr_.get_mock_fk_parent_table_schema(
            schema_operation.mock_fk_parent_table_id_,
            mock_fk_parent_table))) {
        } else if (NULL != mock_fk_parent_table) {
          hash_ret = schema_keys.del_mock_fk_parent_table_keys_.set_refactored_1(schema_key, 1);
          if (OB_SUCCESS != hash_ret) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("failed to add del mock_fk_parent_table", K(hash_ret), KR(ret));
          }
        }
      }
    } else {
      hash_ret = schema_keys.new_mock_fk_parent_table_keys_.set_refactored_1(schema_key, 1);
      if (OB_SUCCESS != hash_ret) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to add new mock_fk_parent_table", K(hash_ret), KR(ret));
      }
    }
  }
  return ret;
}

int ObServerSchemaService::get_increment_mock_fk_parent_table_keys_reversely(
    const ObSchemaMgr &schema_mgr,
    const ObSchemaOperation &schema_operation,
    AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;
  if (!(schema_operation.op_type_ > OB_DDL_MOCK_FK_PARENT_TABLE_OPERATION_BEGIN
        && schema_operation.op_type_ < OB_DDL_MOCK_FK_PARENT_TABLE_OPERATION_END)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", KR(ret), K(schema_operation.op_type_));
  } else {

    const int64_t schema_version = schema_operation.schema_version_;
    SchemaKey schema_key;

    schema_key.database_id_ = schema_operation.database_id_;
    schema_key.mock_fk_parent_table_id_ = schema_operation.mock_fk_parent_table_id_;
    schema_key.schema_version_ = schema_version;
    bool is_delete = (OB_DDL_CREATE_MOCK_FK_PARENT_TABLE == schema_operation.op_type_);
    bool is_exist = false;
    const ObSimpleMockFKParentTableSchema *mock_fk_parent_table = NULL;
    if (OB_FAIL(schema_mgr.mock_fk_parent_table_mgr_.get_mock_fk_parent_table_schema(
        schema_operation.mock_fk_parent_table_id_,
        mock_fk_parent_table))) {
    } else if (NULL != mock_fk_parent_table) {
      is_exist = true;
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(REPLAY_OP(schema_key, schema_keys.del_mock_fk_parent_table_keys_,
          schema_keys.new_mock_fk_parent_table_keys_, is_delete, is_exist))) {
      }
    }
  }
  return ret;
}

int ObServerSchemaService::get_increment_ai_model_keys(
    const ObSchemaMgr &schema_mgr,
    const ObSchemaOperation &schema_operation,
    AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;
  if (!(schema_operation.op_type_ > OB_DDL_AI_MODEL_OPERATION_BEGIN &&
        schema_operation.op_type_ < OB_DDL_AI_MODEL_OPERATION_END)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(schema_operation.op_type_), KR(ret));
  } else {

    uint64_t ai_model_id = schema_operation.ai_model_id_;
    int64_t schema_version = schema_operation.schema_version_;
    int hash_ret = OB_SUCCESS;
    SchemaKey schema_key;

    schema_key.ai_model_id_ = ai_model_id;
    schema_key.schema_version_ = schema_version;

    if (OB_DDL_DROP_AI_MODEL == schema_operation.op_type_) {
      hash_ret = schema_keys.new_ai_model_keys_.erase_refactored(schema_key);
      if (OB_SUCCESS != hash_ret && OB_HASH_NOT_EXIST != hash_ret) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to del schema key from new_ai_model_keys_", K(ret), K(schema_key));
      } else {
        const ObAiModelSchema *schema = nullptr;
        if (OB_FAIL(schema_mgr.ai_model_mgr_.get_ai_model_schema(ai_model_id, schema))) {
        } else if (OB_NOT_NULL(schema)) {
          hash_ret = schema_keys.del_ai_model_keys_.set_refactored_1(schema_key, 1);
          if (OB_SUCCESS != hash_ret) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("failed to add del ai_model id", K(hash_ret), K(ret));
          }
        }
      }
    } else {
      hash_ret = schema_keys.new_ai_model_keys_.set_refactored_1(schema_key, 1);
      if (OB_SUCCESS != hash_ret) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to add new ai_model id", K(hash_ret), K(ret));
      }
    }
  }
  return ret;
}

int ObServerSchemaService::get_increment_ai_model_keys_reversely(
    const ObSchemaMgr &schema_mgr,
    const ObSchemaOperation &schema_operation,
    AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;
  if (!(schema_operation.op_type_ > OB_DDL_AI_MODEL_OPERATION_BEGIN &&
        schema_operation.op_type_ < OB_DDL_AI_MODEL_OPERATION_END)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(schema_operation.op_type_), KR(ret));
  } else {

    uint64_t ai_model_id = schema_operation.ai_model_id_;
    int64_t schema_version = schema_operation.schema_version_;
    SchemaKey schema_key;

    schema_key.ai_model_id_ = ai_model_id;
    schema_key.schema_version_ = schema_version;
    bool is_delete = (OB_DDL_CREATE_AI_MODEL == schema_operation.op_type_);
    bool is_exist = false;
    const ObAiModelSchema *schema = nullptr;
    if (OB_FAIL(schema_mgr.ai_model_mgr_.get_ai_model_schema(ai_model_id, schema))) {
    } else if (OB_NOT_NULL(schema)) {
      is_exist = true;
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(REPLAY_OP(schema_key, schema_keys.del_ai_model_keys_,
          schema_keys.new_ai_model_keys_, is_delete, is_exist))) {
      }
    }
  }
  return ret;
}

// Cache the full system-variable schema for the server runtime.
int ObServerSchemaService::add_sys_variable_schemas_to_cache(
    const SysVariableKeys &sys_variable_keys,
    ObISQLClient &sql_client)
{
  int ret = OB_SUCCESS;
  ObArray<SchemaKey> schema_keys;
  if (OB_FAIL(convert_schema_keys_to_array(sys_variable_keys, schema_keys))) {
  } else {
    FOREACH_CNT_X(schema_key, schema_keys, OB_SUCC(ret)) {

      {
         ObRefreshSchemaStatus schema_status;

        if (OB_FAIL(add_sys_variable_schema_to_cache(sql_client,
                                                     schema_status,
                                                     schema_key->schema_version_))) {
        } else {
          LOG_INFO("add sys variable schema to cache success", K(ret), KPC(schema_key));
        }
        break;
      }
    }
  }
  return ret;
}

int ObServerSchemaService::fetch_increment_schemas(
    const ObRefreshSchemaStatus &schema_status,
    const AllSchemaKeys &all_keys,
    const int64_t schema_version,
    ObISQLClient &sql_client,
    AllSimpleIncrementSchema &simple_incre_schemas)
{
  int ret = OB_SUCCESS;
  ObArray<SchemaKey> schema_keys;

#define GET_BATCH_SCHEMAS(SCHEMA, SCHEMA_TYPE, SCHEMA_KEYS)                               \
  if (OB_SUCC(ret)) {                                                                     \
    schema_keys.reset();                                                                  \
    const SCHEMA_KEYS &new_keys = all_keys.new_##SCHEMA##_keys_;                          \
    ObArray<SCHEMA_TYPE> &simple_schemas = simple_incre_schemas.simple_##SCHEMA##_schemas_;\
    if (OB_FAIL(convert_schema_keys_to_array(new_keys, schema_keys))) {                   \
      LOG_WARN("convert set to array failed", KR(ret));                                   \
    } else if (OB_FAIL(schema_service_->get_batch_##SCHEMA##s(schema_status, sql_client,  \
        schema_version, schema_keys, simple_schemas))) {                                  \
      LOG_WARN("get batch "#SCHEMA"s failed", KR(ret), K(schema_keys));                   \
    } else {                                                                              \
      ALLOW_NEXT_LOG();                                                                   \
      LOG_INFO("get batch "#SCHEMA"s success", K(schema_keys));                           \
      if (schema_keys.size() != simple_schemas.size()) {                                  \
        ret = OB_ERR_UNEXPECTED;                                                          \
        LOG_ERROR("unexpected "#SCHEMA" result cnt",                                      \
                  KR(ret), K(schema_keys.size()), K(simple_schemas.size()));              \
      }                                                                                   \
    }                                                                                     \
  }

#define GET_BATCH_SCHEMAS_WITH_ALLOCATOR(SCHEMA, SCHEMA_TYPE, SCHEMA_KEYS)                \
  if (OB_SUCC(ret)) {                                                                     \
    schema_keys.reset();                                                                  \
    const SCHEMA_KEYS &new_keys = all_keys.new_##SCHEMA##_keys_;                          \
    ObArray<SCHEMA_TYPE *> &simple_schemas = simple_incre_schemas.simple_##SCHEMA##_schemas_;\
    if (OB_FAIL(convert_schema_keys_to_array(new_keys, schema_keys))) {                   \
      LOG_WARN("convert set to array failed", KR(ret));                                   \
    } else if (OB_FAIL(schema_service_->get_batch_##SCHEMA##s(schema_status, sql_client,  \
      simple_incre_schemas.allocator_, schema_version, schema_keys, simple_schemas))) {   \
      LOG_WARN("get batch "#SCHEMA"s failed", KR(ret), K(schema_keys));                   \
    } else {                                                                              \
      ALLOW_NEXT_LOG();                                                                   \
      LOG_INFO("get batch "#SCHEMA"s success", K(schema_keys));                           \
      if (schema_keys.size() != simple_schemas.size()) {                                  \
        ret = OB_ERR_UNEXPECTED;                                                          \
        LOG_ERROR("unexpected "#SCHEMA" result cnt",                                      \
                  KR(ret), K(schema_keys.size()), K(simple_schemas.size()));              \
      }                                                                                   \
    }                                                                                     \
  }

#define GET_BATCH_SCHEMAS_WITHOUT_SCHEMA_STATUS(SCHEMA, SCHEMA_TYPE, SCHEMA_KEYS) \
  if (OB_SUCC(ret)) {                          \
    schema_keys.reset();                        \
    const SCHEMA_KEYS &new_keys = all_keys.new_##SCHEMA##_keys_;    \
    ObArray<SCHEMA_TYPE> &simple_schemas = simple_incre_schemas.simple_##SCHEMA##_schemas_;   \
    if (OB_FAIL(convert_schema_keys_to_array(new_keys, schema_keys))) {      \
      LOG_WARN("convert set to array failed", K(ret));                                    \
    } else if (OB_FAIL(schema_service_->get_batch_##SCHEMA##s(sql_client, \
        schema_version, schema_keys, simple_schemas))) {                                  \
      LOG_WARN("get batch "#SCHEMA"s failed", K(ret), K(schema_keys));                    \
    } else {                                                                              \
      ALLOW_NEXT_LOG();                                                                   \
      LOG_INFO("get batch "#SCHEMA"s success", K(schema_keys));                           \
      if (schema_keys.size() != simple_schemas.size()) {                                  \
        ret = OB_ERR_UNEXPECTED;                                                          \
        LOG_ERROR("unexpected "#SCHEMA" result cnt", K(ret), K(schema_version), K(schema_keys.size()), K(simple_schemas.size())); \
      }                                                                                   \
    }\
  }

  GET_BATCH_SCHEMAS(user, ObSimpleUserSchema, UserKeys);
  GET_BATCH_SCHEMAS(database, ObSimpleDatabaseSchema, DatabaseKeys);
  GET_BATCH_SCHEMAS_WITH_ALLOCATOR(table, ObSimpleTableSchemaV2, TableKeys);
  GET_BATCH_SCHEMAS(outline, ObSimpleOutlineSchema, OutlineKeys);
  GET_BATCH_SCHEMAS(routine, ObSimpleRoutineSchema, RoutineKeys);
  GET_BATCH_SCHEMAS(package, ObSimplePackageSchema, PackageKeys);
  GET_BATCH_SCHEMAS(trigger, ObSimpleTriggerSchema, TriggerKeys);
  GET_BATCH_SCHEMAS(db_priv, ObDBPriv, DBPrivKeys);
  GET_BATCH_SCHEMAS(table_priv, ObTablePriv, TablePrivKeys);
  GET_BATCH_SCHEMAS(routine_priv, ObRoutinePriv, RoutinePrivKeys);
  GET_BATCH_SCHEMAS(sys_priv, ObSysPriv, SysPrivKeys);
  GET_BATCH_SCHEMAS(obj_priv, ObObjPriv, ObjPrivKeys);
  GET_BATCH_SCHEMAS(obj_mysql_priv, ObObjMysqlPriv, ObjMysqlPrivKeys);
  GET_BATCH_SCHEMAS(column_priv, ObColumnPriv, ColumnPrivKeys);

  GET_BATCH_SCHEMAS(sys_variable, ObSimpleSysVariableSchema, SysVariableKeys);
  GET_BATCH_SCHEMAS(mock_fk_parent_table, ObSimpleMockFKParentTableSchema, MockFKParentTableKeys);
  GET_BATCH_SCHEMAS(ai_model, ObAiModelSchema, AiModelKeys);

  if (OB_SUCC(ret)) {
    ObArray<uint64_t> non_sys_table_ids;
    if (OB_FAIL(non_sys_table_ids.assign(all_keys.non_sys_table_ids_))) {
    } else if (OB_FAIL(schema_service_->get_batch_table_schema(
        schema_status, schema_version, non_sys_table_ids, sql_client,
        simple_incre_schemas.allocator_, simple_incre_schemas.non_sys_tables_))) {
    }
  }

#undef GET_BATCH_SCHEMAS
  return ret;

}

int ObServerSchemaService::apply_increment_schema_to_cache(
    const AllSchemaKeys &all_keys,
    const AllSimpleIncrementSchema &simple_incre_schemas,
    ObSchemaMgr &schema_mgr)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(apply_runtime_schema_to_cache(
              all_keys, simple_incre_schemas, schema_mgr))) {
  } // Need to ensure that the system variables are added first
  else if (OB_FAIL(apply_sys_variable_schema_to_cache(
              all_keys, simple_incre_schemas, schema_mgr.sys_variable_mgr_))) {
  } else if (OB_FAIL(apply_user_schema_to_cache(
             all_keys, simple_incre_schemas, schema_mgr))) {
  } else if (OB_FAIL(apply_database_schema_to_cache(
             all_keys, simple_incre_schemas, schema_mgr))) {
  } else if (OB_FAIL(apply_table_schema_to_cache(
             all_keys, simple_incre_schemas, schema_mgr))) {
  } else if (OB_FAIL(apply_outline_schema_to_cache(
             all_keys, simple_incre_schemas, schema_mgr.outline_mgr_))) {
  } else if (OB_FAIL(apply_routine_schema_to_cache(
             all_keys, simple_incre_schemas, schema_mgr.routine_mgr_))) {
  } else if (OB_FAIL(apply_package_schema_to_cache(
             all_keys, simple_incre_schemas, schema_mgr.package_mgr_))) {
  } else if (OB_FAIL(apply_trigger_schema_to_cache(
             all_keys, simple_incre_schemas, schema_mgr.trigger_mgr_))) {
  } else if (OB_FAIL(apply_db_priv_schema_to_cache(
             all_keys, simple_incre_schemas, schema_mgr.priv_mgr_))) {
  } else if (OB_FAIL(apply_table_priv_schema_to_cache(
             all_keys, simple_incre_schemas, schema_mgr.priv_mgr_))) {
  } else if (OB_FAIL(apply_routine_priv_schema_to_cache(
             all_keys, simple_incre_schemas, schema_mgr.priv_mgr_))) {
  } else if (OB_FAIL(apply_column_priv_schema_to_cache(
             all_keys, simple_incre_schemas, schema_mgr.priv_mgr_))) {
  } else if (OB_FAIL(apply_sys_priv_schema_to_cache(
             all_keys, simple_incre_schemas, schema_mgr.priv_mgr_))) {
  } else if (OB_FAIL(apply_obj_priv_schema_to_cache(
             all_keys, simple_incre_schemas, schema_mgr.priv_mgr_))) {
  } else if (OB_FAIL(apply_obj_mysql_priv_schema_to_cache(
            all_keys, simple_incre_schemas, schema_mgr.priv_mgr_))) {
  } else if (OB_FAIL(apply_mock_fk_parent_table_schema_to_cache(
             all_keys, simple_incre_schemas, schema_mgr.mock_fk_parent_table_mgr_))) {
  } else if (OB_FAIL(apply_ai_model_schema_to_cache(
             all_keys, simple_incre_schemas, schema_mgr))) {
  }

  return ret;
}

int ObServerSchemaService::apply_runtime_schema_to_cache(
    const AllSchemaKeys &all_keys,
    const AllSimpleIncrementSchema &simple_incre_schemas,
    ObSchemaMgr &schema_mgr)
{
  int ret = OB_SUCCESS;
  if (OB_SUCC(ret)) {
    if (OB_FAIL(schema_mgr.add_runtime_schemas(simple_incre_schemas.simple_runtime_schemas_))) {
    }
    ALLOW_NEXT_LOG();
    LOG_INFO("add runtime schema finish",
             "schemas", simple_incre_schemas.simple_runtime_schemas_, K(ret));
  }
  return ret;
}

#define APPLY_SCHEMA_TO_CACHE_IMPL(SCHEMA_MGR, SCHEMA, SCHEMA_TYPE, SCHEMA_KEYS) \
int ObServerSchemaService::apply_##SCHEMA##_schema_to_cache( \
    const AllSchemaKeys &all_keys, \
    const AllSimpleIncrementSchema &simple_incre_schemas, \
    SCHEMA_MGR &mgr) \
{ \
  int ret = OB_SUCCESS; \
  ObArray<SchemaKey> schema_keys; \
  const SCHEMA_KEYS &del_keys = all_keys.del_##SCHEMA##_keys_;    \
  if (OB_FAIL(convert_schema_keys_to_array(del_keys, schema_keys))) {    \
    LOG_WARN("convert set to array failed", K(ret));                     \
  } else {                                                               \
    FOREACH_CNT_X(schema_key, schema_keys, OB_SUCC(ret)) {               \
      if (OB_FAIL(mgr.del_##SCHEMA(schema_key->get_##SCHEMA##_key()))) { \
        LOG_WARN("del "#SCHEMA" failed", K(ret),                         \
                  #SCHEMA"_key", schema_key->get_##SCHEMA##_key());       \
      }                                                                  \
    }                                                                    \
    ALLOW_NEXT_LOG();                                                    \
    LOG_INFO("del "#SCHEMA"s finish", K(schema_keys), K(ret));           \
  }                                            \
  if (OB_SUCC(ret)) {                          \
    const ObArray<SCHEMA_TYPE> &schemas = simple_incre_schemas.simple_##SCHEMA##_schemas_;   \
    if (OB_FAIL(mgr.add_##SCHEMA##s(schemas))) {                          \
      LOG_WARN("add "#SCHEMA"s failed", K(ret),                           \
               #SCHEMA" schemas", schemas);                               \
    }                                                                     \
    ALLOW_NEXT_LOG();                                                     \
    LOG_INFO("add "#SCHEMA"s finish", K(schemas), K(ret));  \
  }                                                         \
  return ret; \
}

APPLY_SCHEMA_TO_CACHE_IMPL(ObSysVariableMgr, sys_variable, ObSimpleSysVariableSchema, SysVariableKeys);
APPLY_SCHEMA_TO_CACHE_IMPL(ObSchemaMgr, user, ObSimpleUserSchema, UserKeys);
APPLY_SCHEMA_TO_CACHE_IMPL(ObSchemaMgr, database, ObSimpleDatabaseSchema, DatabaseKeys);
APPLY_SCHEMA_TO_CACHE_IMPL(ObSchemaMgr, table, ObSimpleTableSchemaV2*, TableKeys);
APPLY_SCHEMA_TO_CACHE_IMPL(ObOutlineMgr, outline, ObSimpleOutlineSchema, OutlineKeys);
APPLY_SCHEMA_TO_CACHE_IMPL(ObRoutineMgr, routine, ObSimpleRoutineSchema, RoutineKeys);
APPLY_SCHEMA_TO_CACHE_IMPL(ObPackageMgr, package, ObSimplePackageSchema, PackageKeys);
APPLY_SCHEMA_TO_CACHE_IMPL(ObTriggerMgr, trigger, ObSimpleTriggerSchema, TriggerKeys);
APPLY_SCHEMA_TO_CACHE_IMPL(ObPrivMgr, db_priv, ObDBPriv, DBPrivKeys);
APPLY_SCHEMA_TO_CACHE_IMPL(ObPrivMgr, table_priv, ObTablePriv, TablePrivKeys);
APPLY_SCHEMA_TO_CACHE_IMPL(ObPrivMgr, routine_priv, ObRoutinePriv, RoutinePrivKeys);
APPLY_SCHEMA_TO_CACHE_IMPL(ObPrivMgr, column_priv, ObColumnPriv, ColumnPrivKeys);
APPLY_SCHEMA_TO_CACHE_IMPL(ObPrivMgr, sys_priv, ObSysPriv, SysPrivKeys);
APPLY_SCHEMA_TO_CACHE_IMPL(ObPrivMgr, obj_priv, ObObjPriv, ObjPrivKeys);
APPLY_SCHEMA_TO_CACHE_IMPL(ObPrivMgr, obj_mysql_priv, ObObjMysqlPriv, ObjMysqlPrivKeys);
APPLY_SCHEMA_TO_CACHE_IMPL(ObMockFKParentTableMgr, mock_fk_parent_table, ObSimpleMockFKParentTableSchema, MockFKParentTableKeys);
APPLY_SCHEMA_TO_CACHE_IMPL(ObSchemaMgr, ai_model, ObAiModelSchema, AiModelKeys);

int ObServerSchemaService::update_schema_mgr(ObISQLClient &sql_client,
                                             const ObRefreshSchemaStatus &schema_status,
                                             ObSchemaMgr &schema_mgr,
                                             const int64_t schema_version,
                                             AllSchemaKeys &all_keys)
{
  int ret = OB_SUCCESS;


  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", K(ret));
  } else {
    // add sys variable schema to cache
    {
      if (OB_FAIL(add_sys_variable_schemas_to_cache(all_keys.new_sys_variable_keys_, sql_client))) {
      }
    }

    if (OB_SUCC(ret)) {
      AllSimpleIncrementSchema simple_incre_schemas;
      if (OB_FAIL(fetch_increment_schemas(schema_status, all_keys, schema_version, sql_client, simple_incre_schemas))) {
      } else if (OB_FAIL(apply_increment_schema_to_cache(all_keys, simple_incre_schemas, schema_mgr))) {
      } else if (OB_FAIL(update_non_sys_schemas_in_cache_(schema_mgr, simple_incre_schemas.non_sys_tables_))) {
      }
    }
  }

  // check shema consistent at last
  if (FAILEDx(schema_mgr.rebuild_schema_meta_if_not_consistent())) {
    LOG_ERROR("not consistency for schema meta data", KR(ret));
  }

  return ret;
}

// wrapper for add index

int ObServerSchemaService::extract_non_sys_table_ids_(
    const TableKeys &keys,
    ObIArray<uint64_t> &non_sys_table_ids)
{
  int ret = OB_SUCCESS;
  for (TableKeys::const_iterator it = keys.begin();
       OB_SUCC(ret) && it != keys.end(); ++it) {
    const uint64_t table_id = (it->first).table_id_;
    if (is_inner_table(table_id) && !is_sys_table(table_id)) {
      if (OB_FAIL(non_sys_table_ids.push_back(table_id))) {
      }
    }
  }
  return ret;
}

int ObServerSchemaService::update_non_sys_schemas_in_cache_(
    const ObSchemaMgr &schema_mgr,
    ObIArray<ObTableSchema *> &non_sys_tables)
{
  int ret = OB_SUCCESS;
  FOREACH_CNT_X(non_sys_table, non_sys_tables, OB_SUCC(ret)) {
    if (OB_ISNULL(non_sys_table)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("NULL ptr", KP(non_sys_table), KR(ret));
    } else if (OB_ISNULL(*non_sys_table)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("NULL ptr", KP(*non_sys_table), KR(ret));
    } else if (OB_FAIL(add_aux_schema_from_mgr(schema_mgr, **non_sys_table, USER_INDEX))) {
    }
  }
  if (FAILEDx(update_schema_cache(non_sys_tables))) {
    LOG_WARN("failed to update schema cache", KR(ret));
  }
  return ret;
}

int ObServerSchemaService::fallback_schema_mgr(
    const ObRefreshSchemaStatus &schema_status,
    ObSchemaMgr &schema_mgr,
    const int64_t schema_version)
{
  FLOG_INFO("[FALLBACK_SCHEMA] fallback schema mgr start", K(schema_status),
            "from_version", schema_mgr.get_schema_version(),
            "target_version", schema_version);
  const int64_t start = ObTimeUtility::current_time();
  int ret = OB_SUCCESS;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", K(ret));
  } else if (OB_INVALID_VERSION == schema_version) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(schema_version));
  } else {
    ObISQLClient &sql_client = *sql_proxy_;
    SMART_VAR(AllSchemaKeys, all_keys) {
      int64_t start_ver = OB_INVALID_VERSION;
      int64_t end_ver = OB_INVALID_VERSION;
      bool is_fallback = false;
      if (schema_mgr.get_schema_version() < schema_version) {
        start_ver = schema_mgr.get_schema_version();
        end_ver = schema_version;
        is_fallback = false;
      } else {
        start_ver = schema_version;
        end_ver = schema_mgr.get_schema_version();
        is_fallback = true;
      }

      if (start_ver < end_ver) {
        ObSchemaService::SchemaOperationSetWithAlloc schema_operations;
        if (OB_FAIL(schema_service_->get_increment_schema_operations(
            schema_status, start_ver, end_ver, sql_client, schema_operations))) {
        } else if (schema_operations.count() > 0) {
          if (is_fallback) {
            if (OB_FAIL(replay_log_reversely(schema_mgr, schema_operations, all_keys))) {
            }
          } else {
            if (OB_FAIL(replay_log(schema_mgr, schema_operations, all_keys))) {
            }
          }
          if (OB_SUCC(ret)) {
            {
              if (OB_FAIL(update_schema_mgr(sql_client, schema_status, schema_mgr, schema_version, all_keys))) {
              }
            }
          }
        }
      }
    }
  }

  if (OB_SUCC(ret)) {
    schema_mgr.set_schema_version(schema_version);
  }

  FLOG_INFO("[FALLBACK_SCHEMA] fallback schema mgr end",
            KR(ret), K(schema_status),
            "from_version", schema_mgr.get_schema_version(),
            "target_version", schema_version,
            "cost", ObTimeUtility::current_time() - start);
  return ret;
}

int ObServerSchemaService::replay_log(
  const ObSchemaMgr &schema_mgr,
  const ObSchemaService::SchemaOperationSetWithAlloc &schema_operations,
  AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", K(ret));
  } else {
    int64_t bucket_size = schema_operations.count();
    if (OB_FAIL(schema_keys.create(bucket_size))) {
      LOG_WARN("fail to create hashset: ", K(bucket_size), K(ret));
      ret = OB_INNER_STAT_ERROR;
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < schema_operations.count(); ++i) {
        const ObSchemaOperation &schema_operation = schema_operations.at(i);
        LOG_INFO("schema operation", K(schema_operation));
        if (schema_operation.op_type_ > OB_DDL_SYS_VAR_OPERATION_BEGIN
                   && schema_operation.op_type_ < OB_DDL_SYS_VAR_OPERATION_END) {
          if (OB_FAIL(get_increment_sys_variable_keys(schema_mgr,
                                                      schema_operation, schema_keys))) {
          }
        } else if (schema_operation.op_type_ > OB_DDL_USER_OPERATION_BEGIN
                   && schema_operation.op_type_ < OB_DDL_USER_OPERATION_END) {
          if (OB_FAIL(get_increment_user_keys(schema_mgr,
                                              schema_operation, schema_keys))) {
          }
        } else if (schema_operation.op_type_ > OB_DDL_DATABASE_OPERATION_BEGIN
                   && schema_operation.op_type_ < OB_DDL_DATABASE_OPERATION_END) {
          if (OB_FAIL(get_increment_database_keys(schema_mgr,
                                                  schema_operation, schema_keys))) {
          }
        } else if (schema_operation.op_type_ > OB_DDL_TABLE_OPERATION_BEGIN
                   && schema_operation.op_type_ < OB_DDL_TABLE_OPERATION_END) {
          if (OB_FAIL(get_increment_table_keys(schema_mgr,
                                               schema_operation, schema_keys))) {
          }
        } else if (schema_operation.op_type_ > OB_DDL_OUTLINE_OPERATION_BEGIN
                   && schema_operation.op_type_ < OB_DDL_OUTLINE_OPERATION_END) {
          if (OB_FAIL(get_increment_outline_keys(schema_mgr,
                                                 schema_operation, schema_keys))) {
          }
        } else if (schema_operation.op_type_ > OB_DDL_DB_PRIV_OPERATION_BEGIN
                   && schema_operation.op_type_ < OB_DDL_DB_PRIV_OPERATION_END) {
          if (OB_FAIL(get_increment_db_priv_keys(schema_mgr,
                                                 schema_operation, schema_keys))) {
          }
        } else if (schema_operation.op_type_ > OB_DDL_TABLE_PRIV_OPERATION_BEGIN
                   && schema_operation.op_type_ < OB_DDL_TABLE_PRIV_OPERATION_END) {
          if (OB_FAIL(get_increment_table_priv_keys(schema_mgr,
                                                    schema_operation, schema_keys))) {
          }
        } else if (schema_operation.op_type_ > OB_DDL_ROUTINE_OPERATION_BEGIN
                   && schema_operation.op_type_ < OB_DDL_ROUTINE_OPERATION_END) {
          if (OB_FAIL(get_increment_routine_keys(schema_mgr, schema_operation, schema_keys))) {
          }
        } else if (schema_operation.op_type_ > OB_DDL_ROUTINE_PRIV_OPERATION_BEGIN
                   && schema_operation.op_type_ < OB_DDL_ROUTINE_PRIV_OPERATION_END) {
          if (OB_FAIL(get_increment_routine_priv_keys(schema_mgr, schema_operation, schema_keys))) {
          }
        } else if (schema_operation.op_type_ > OB_DDL_PACKAGE_OPERATION_BEGIN
                   && schema_operation.op_type_ < OB_DDL_PACKAGE_OPERATION_END) {
          if (OB_FAIL(get_increment_package_keys(schema_mgr, schema_operation, schema_keys))) {
          }
        } else if (schema_operation.op_type_ > OB_DDL_TRIGGER_OPERATION_BEGIN
                   && schema_operation.op_type_ < OB_DDL_TRIGGER_OPERATION_END) {
          if (OB_FAIL(get_increment_trigger_keys(schema_mgr, schema_operation, schema_keys))) {
          }
        } else if (schema_operation.op_type_ > OB_DDL_SYS_PRIV_OPERATION_BEGIN
                   && schema_operation.op_type_ < OB_DDL_SYS_PRIV_OPERATION_END) {
          if (OB_FAIL(get_increment_sys_priv_keys(schema_mgr,
                                                 schema_operation, schema_keys))) {
          }
        } else if (schema_operation.op_type_ > OB_DDL_OBJ_PRIV_OPERATION_BEGIN
                   && schema_operation.op_type_ < OB_DDL_OBJ_PRIV_OPERATION_END) {
          if (OB_FAIL(get_increment_obj_priv_keys(schema_mgr,
                                                  schema_operation, schema_keys))) {
          }
        } else if (schema_operation.op_type_ > OB_DDL_OBJ_MYSQL_PRIV_OPERATION_BEGIN
                   && schema_operation.op_type_ < OB_DDL_OBJ_MYSQL_PRIV_OPERATION_END) {
            if (OB_FAIL(get_increment_obj_mysql_priv_keys(schema_mgr, schema_operation, schema_keys))) {
            }
        } else if (schema_operation.op_type_ > OB_DDL_MOCK_FK_PARENT_TABLE_OPERATION_BEGIN
                    && schema_operation.op_type_ < OB_DDL_MOCK_FK_PARENT_TABLE_OPERATION_END) {
          if (OB_FAIL(get_increment_mock_fk_parent_table_keys(schema_mgr, schema_operation, schema_keys))) {
          }
        } else if (schema_operation.op_type_ > OB_DDL_COLUMN_PRIV_OPERATION_BEGIN &&
            schema_operation.op_type_ < OB_DDL_COLUMN_PRIV_OPERATION_END) {
          if (OB_FAIL(get_increment_column_priv_keys(schema_mgr, schema_operation, schema_keys))) {
          }
        } else if (schema_operation.op_type_ > OB_DDL_AI_MODEL_OPERATION_BEGIN &&
            schema_operation.op_type_ < OB_DDL_AI_MODEL_OPERATION_END) {
          if (OB_FAIL(get_increment_ai_model_keys(schema_mgr, schema_operation, schema_keys))) {
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(extract_non_sys_table_ids_(schema_keys.new_table_keys_,
                                             schema_keys.non_sys_table_ids_))) {
      }
    }
  }
  return ret;
}

int ObServerSchemaService::replay_log_reversely(
  const ObSchemaMgr &schema_mgr,
  const ObSchemaService::SchemaOperationSetWithAlloc &schema_operations,
  AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;

  int64_t bucket_size = schema_operations.count();
  if (OB_FAIL(schema_keys.create(bucket_size))) {
    LOG_WARN("fail to create hashset: ", K(bucket_size), K(ret));
    ret = OB_INNER_STAT_ERROR;
  } else {
    for (int64_t i = schema_operations.count() - 1; OB_SUCC(ret) && i >= 0; --i) {
      const ObSchemaOperation &schema_operation = schema_operations.at(i);
      LOG_INFO("schema operation", K(schema_operation));
      if (schema_operation.op_type_ > OB_DDL_SYS_VAR_OPERATION_BEGIN
                 && schema_operation.op_type_ < OB_DDL_SYS_VAR_OPERATION_END) {
        if (OB_FAIL(get_increment_sys_variable_keys_reversely(schema_mgr, schema_operation, schema_keys))) {
        }
      } else if (schema_operation.op_type_ > OB_DDL_USER_OPERATION_BEGIN
                 && schema_operation.op_type_ < OB_DDL_USER_OPERATION_END) {
        if (OB_FAIL(get_increment_user_keys_reversely(schema_mgr, schema_operation, schema_keys))) {
        }
      } else if (schema_operation.op_type_ > OB_DDL_DATABASE_OPERATION_BEGIN
                 && schema_operation.op_type_ < OB_DDL_DATABASE_OPERATION_END) {
        if (OB_FAIL(get_increment_database_keys_reversely(schema_mgr, schema_operation, schema_keys))) {
        }
      } else if (schema_operation.op_type_ > OB_DDL_TABLE_OPERATION_BEGIN
                 && schema_operation.op_type_ < OB_DDL_TABLE_OPERATION_END) {
        if (OB_FAIL(get_increment_table_keys_reversely(schema_mgr, schema_operation, schema_keys))) {
        }
      } else if (schema_operation.op_type_ > OB_DDL_OUTLINE_OPERATION_BEGIN
                 && schema_operation.op_type_ < OB_DDL_OUTLINE_OPERATION_END) {
        if (OB_FAIL(get_increment_outline_keys_reversely(schema_mgr, schema_operation, schema_keys))) {
        }
      } else if (schema_operation.op_type_ > OB_DDL_ROUTINE_OPERATION_BEGIN
                 && schema_operation.op_type_ < OB_DDL_ROUTINE_OPERATION_END) {
        if (OB_FAIL(get_increment_routine_keys_reversely(schema_mgr, schema_operation, schema_keys))) {
        }
      } else if (schema_operation.op_type_ > OB_DDL_ROUTINE_PRIV_OPERATION_BEGIN
                 && schema_operation.op_type_ < OB_DDL_ROUTINE_PRIV_OPERATION_END) {
        if (OB_FAIL(get_increment_routine_priv_keys_reversely(schema_mgr, schema_operation, schema_keys))) {
        }
      } else if (schema_operation.op_type_ > OB_DDL_PACKAGE_OPERATION_BEGIN
            && schema_operation.op_type_ < OB_DDL_PACKAGE_OPERATION_END) {
        if (OB_FAIL(get_increment_package_keys_reversely(schema_mgr, schema_operation, schema_keys))) {
        }
      } else if (schema_operation.op_type_ > OB_DDL_TRIGGER_OPERATION_BEGIN
            && schema_operation.op_type_ < OB_DDL_TRIGGER_OPERATION_END) {
        if (OB_FAIL(get_increment_trigger_keys_reversely(schema_mgr, schema_operation, schema_keys))) {
        }
      } else if (schema_operation.op_type_ > OB_DDL_DB_PRIV_OPERATION_BEGIN
                 && schema_operation.op_type_ < OB_DDL_DB_PRIV_OPERATION_END) {
        if (OB_FAIL(get_increment_db_priv_keys_reversely(schema_mgr, schema_operation, schema_keys))) {
        }
      } else if (schema_operation.op_type_ > OB_DDL_TABLE_PRIV_OPERATION_BEGIN
                 && schema_operation.op_type_ < OB_DDL_TABLE_PRIV_OPERATION_END) {
        if (OB_FAIL(get_increment_table_priv_keys_reversely(schema_mgr, schema_operation, schema_keys))) {
        }
      } else if (schema_operation.op_type_ > OB_DDL_SYS_PRIV_OPERATION_BEGIN
                 && schema_operation.op_type_ < OB_DDL_SYS_PRIV_OPERATION_END) {
        if (OB_FAIL(get_increment_sys_priv_keys_reversely(schema_mgr, schema_operation, schema_keys))) {
        }
      } else if (schema_operation.op_type_ > OB_DDL_OBJ_PRIV_OPERATION_BEGIN
                 && schema_operation.op_type_ < OB_DDL_OBJ_PRIV_OPERATION_END) {
        if (OB_FAIL(get_increment_obj_priv_keys_reversely(schema_mgr, schema_operation, schema_keys))) {
        }
      } else if (schema_operation.op_type_ > OB_DDL_OBJ_MYSQL_PRIV_OPERATION_BEGIN
                 && schema_operation.op_type_ < OB_DDL_OBJ_MYSQL_PRIV_OPERATION_END) {
        if (OB_FAIL(get_increment_obj_mysql_priv_keys_reversely(schema_mgr, schema_operation, schema_keys))) {
        }
      } else if (schema_operation.op_type_ > OB_DDL_MOCK_FK_PARENT_TABLE_OPERATION_BEGIN
                 && schema_operation.op_type_ < OB_DDL_MOCK_FK_PARENT_TABLE_OPERATION_END) {
        if (OB_FAIL(get_increment_mock_fk_parent_table_keys_reversely(schema_mgr, schema_operation, schema_keys))) {
        }
      } else if (schema_operation.op_type_ > OB_DDL_COLUMN_PRIV_OPERATION_BEGIN &&
                 schema_operation.op_type_ < OB_DDL_COLUMN_PRIV_OPERATION_END) {
        if (OB_FAIL(get_increment_column_priv_keys_reversely(schema_mgr, schema_operation, schema_keys))) {
        }
      } else if (schema_operation.op_type_ > OB_DDL_AI_MODEL_OPERATION_BEGIN &&
                 schema_operation.op_type_ < OB_DDL_AI_MODEL_OPERATION_END) {
        if (OB_FAIL(get_increment_ai_model_keys_reversely(schema_mgr, schema_operation, schema_keys))) {
        }
      } else {
        // ingore other operaton.
      }
    }
  }

  return ret;
}

bool ObServerSchemaService::need_construct_aux_infos_(
     const ObTableSchema &table_schema)
{
  bool bret = true;
  if (table_schema.is_index_table()
      || table_schema.is_view_table()
      || table_schema.is_aux_lob_table()) {
    bret = false;
  }
  return bret;
}

int ObServerSchemaService::construct_aux_infos_(
    common::ObISQLClient &sql_client,
    const share::schema::ObRefreshSchemaStatus &schema_status,
    ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObAuxTableMetaInfo, 8> aux_table_metas;
  const int64_t schema_version = table_schema.get_schema_version();
  const uint64_t table_id = table_schema.get_table_id();
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_FAIL(schema_service_->fetch_aux_tables(
             schema_status, table_id,
             schema_version, sql_client, aux_table_metas))) {
  } else {
    FOREACH_CNT_X(tmp_aux_table_meta, aux_table_metas, OB_SUCC(ret)) {
      const ObAuxTableMetaInfo &aux_table_meta = *tmp_aux_table_meta;
      if (USER_INDEX == aux_table_meta.table_type_) {
        if (OB_FAIL(table_schema.add_simple_index_info(ObAuxTableMetaInfo(
                           aux_table_meta.table_id_,
                           aux_table_meta.table_type_,
                           aux_table_meta.index_type_)))) {
        }
      } else if (AUX_LOB_META == aux_table_meta.table_type_) {
        table_schema.set_aux_lob_meta_tid(aux_table_meta.table_id_);
      } else if (AUX_LOB_PIECE == aux_table_meta.table_type_) {
        table_schema.set_aux_lob_piece_tid(aux_table_meta.table_id_);
      }
    } // end FOREACH_CNT_X
  }
  return ret;
}

int ObServerSchemaService::convert_to_simple_schema(
    const ObTableSchema &schema,
    ObSimpleTableSchemaV2 &simple_schema)
{
  int ret= OB_SUCCESS;

  if (OB_FAIL(simple_schema.assign(schema))) {
  } else {
    simple_schema.set_part_num(schema.get_first_part_num());
    simple_schema.set_def_sub_part_num(schema.get_def_sub_part_num());
  }

  return ret;
}

int ObServerSchemaService::convert_to_simple_schema(
    common::ObIAllocator &allocator,
    const ObIArray<ObTableSchema> &schemas,
    ObIArray<ObSimpleTableSchemaV2 *> &simple_schemas)
{
  int ret= OB_SUCCESS;
  simple_schemas.reset();

  FOREACH_CNT_X(schema, schemas, OB_SUCC(ret)) {
    ObSimpleTableSchemaV2 *simple_schema = NULL;
    if (OB_ALL_CORE_TABLE_TID == schema->get_table_id()) {
      continue;
    } else if (OB_FAIL(ObSchemaUtils::alloc_schema(allocator, simple_schema))) {
    } else if (OB_FAIL(convert_to_simple_schema(*schema, *simple_schema))) {
    } else if (OB_FAIL(simple_schemas.push_back(simple_schema))) {
    }
  }

  return ret;
}

int ObServerSchemaService::convert_to_simple_schema(
    common::ObIAllocator &allocator,
    const ObIArray<ObTableSchema *> &schemas,
    ObIArray<ObSimpleTableSchemaV2 *> &simple_schemas)
{
  int ret= OB_SUCCESS;
  simple_schemas.reset();

  FOREACH_CNT_X(schema, schemas, OB_SUCC(ret)) {
    const ObTableSchema *table_schema = *schema;
    ObSimpleTableSchemaV2 *simple_schema = NULL;
    if (OB_ISNULL(table_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("NULL ptr", KR(ret), KP(table_schema));
    } else if (OB_FAIL(ObSchemaUtils::alloc_schema(allocator, simple_schema))) {
    } else if (OB_FAIL(convert_to_simple_schema(*table_schema, *simple_schema))) {
    } else if (OB_FAIL(simple_schemas.push_back(simple_schema))) {
    }
  }

  return ret;
}

int ObServerSchemaService::fill_all_core_table_schema(ObSchemaMgr &schema_mgr_for_cache)
{
  int ret = OB_SUCCESS;
  ObTableSchema all_core_table_schema;
  ObSimpleTableSchemaV2 all_core_table_schema_simple;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", K(ret));
  } else if (OB_FAIL(schema_service_->get_all_core_table_schema(all_core_table_schema))) {
  } else if (false
             && OB_FAIL(ObSchemaUtils::construct_runtime_space_full_table(all_core_table_schema))) {
    LOG_WARN("fail to construct __all_core_table schema", KR(ret));
  } else if (OB_FAIL(convert_to_simple_schema(all_core_table_schema, all_core_table_schema_simple))) {
  } else if (OB_FAIL(schema_mgr_for_cache.add_table(all_core_table_schema_simple))) {
  } else {
    schema_mgr_for_cache.set_schema_version(OB_CORE_SCHEMA_VERSION);
  }
  return ret;
}

// new schema refresh
int ObServerSchemaService::refresh_schema(
    const ObRefreshSchemaStatus &schema_status,
    common::ObIArray<share::schema::ObTableSchema> *table_schemas)
{
  int ret = OB_SUCCESS;
  const int64_t start = ObTimeUtility::current_time();

  ObSchemaMgr *schema_mgr_for_cache = NULL;
  bool is_full_schema = true;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", K(ret));
  } else if (FALSE_IT(schema_mgr_for_cache = ATOMIC_LOAD(&schema_mgr_for_cache_))) {
    LOG_WARN("fail to get schema mgr for cache", K(ret));
  } else if (OB_ISNULL(schema_mgr_for_cache)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema mgr for cache is null", K(ret));
  } else if (FALSE_IT(is_full_schema = refresh_full_schema_)) {
  } else if (is_full_schema) {
    FLOG_INFO("[REFRESH_SCHEMA] start to refresh full schema",
              "current schema_version", schema_mgr_for_cache->get_schema_version(), K(schema_status));

    if (OB_FAIL(refresh_full_schema(schema_status, table_schemas))) {
    }

    FLOG_INFO("[REFRESH_SCHEMA] finish refresh full schema", K(ret), K(schema_status),
              "current schema_version", schema_mgr_for_cache->get_schema_version(),
              "cost", ObTimeUtility::current_time() - start);

    if (OB_SUCC(ret)) {
      bool overwrite = true;
      is_full_schema = false;
      UNUSED(overwrite);
      refresh_full_schema_ = is_full_schema;
      refresh_full_schema_present_ = true;
    }
  } else {
    FLOG_INFO("[REFRESH_SCHEMA] start to refresh increment schema",
              "current schema_version", schema_mgr_for_cache->get_schema_version(), K(schema_status));

    if (OB_FAIL(refresh_increment_schema(schema_status))) {
    }

    FLOG_INFO("[REFRESH_SCHEMA] finish refresh increment schema", K(ret), K(schema_status),
              "current schema_version", schema_mgr_for_cache->get_schema_version(),
              "cost", ObTimeUtility::current_time() - start);
  }

  if (OB_SUCC(ret)) {
    const int64_t now = ObTimeUtility::current_time();
    EVENT_INC(REFRESH_SCHEMA_COUNT);
    EVENT_ADD(REFRESH_SCHEMA_TIME, now - start);
  }
  return ret;
}

int ObServerSchemaService::refresh_full_schema(
    const ObRefreshSchemaStatus &schema_status,
    common::ObIArray<share::schema::ObTableSchema> *table_schemas)
{
  int ret = OB_SUCCESS;

  ObSchemaMgr *schema_mgr_for_cache = NULL;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret), K(schema_status));
  } else {
    while (OB_SUCC(ret)) {
      int64_t retry_count = 0;
      bool core_schema_change = true;
      bool sys_schema_change = true;
      int64_t local_schema_version = 0;
      int64_t core_schema_version = 0;
      int64_t schema_version = 0;
      if (FALSE_IT(schema_mgr_for_cache = ATOMIC_LOAD(&schema_mgr_for_cache_))) {
        LOG_WARN("fail to get schema_mgr_for_cache", KR(ret), K(schema_status));
      } else if (OB_ISNULL(schema_mgr_for_cache)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("schema mgr for cache is null", KR(ret), K(schema_status));
      } else {
        local_schema_version = schema_mgr_for_cache->get_schema_version();
      }
      // If refreshing the full amount fails, you need to reset and retry until it succeeds.
      // The outer layer avoids the scenario of failure to refresh the full amount of schema in the bootstrap stage.
      while (OB_SUCC(ret) && (core_schema_change || sys_schema_change)) {
        if (OB_FAIL(check_stop())) {
          LOG_WARN("observer is stopping", KR(ret), K(schema_status));
          break;
        } else if (retry_count > 0) {
          LOG_WARN("refresh_full_schema failed, retry", K(schema_status), K(retry_count));
        }
        ObISQLClient &sql_client = *sql_proxy_;
        // refresh core table schemas
        if (OB_SUCC(ret) && core_schema_change) {
          if (OB_FAIL(schema_service_->get_core_version(
                      sql_client, schema_status, core_schema_version))) {
          } else if (core_schema_version <= OB_CORE_SCHEMA_VERSION + 1) {
            ret = OB_EAGAIN;
            LOG_WARN("schema may be not persisted, try again",
                     KR(ret), K(schema_status), K(core_schema_version));
          } else if (core_schema_version > local_schema_version) {
            // for core table schema, we publish as core_temp_version
            int64_t publish_version = 0;
            if (OB_FAIL(ObSchemaService::gen_core_temp_version(core_schema_version, publish_version))) {
            } else if (OB_FAIL(try_fetch_publish_core_schemas(schema_status, core_schema_version,
                publish_version, sql_client, core_schema_change))) {
            }
          } else {
            core_schema_change = false;
          }
        }

        // refresh sys table schemas
        if (OB_SUCC(ret) && !core_schema_change && sys_schema_change) {
          if (OB_FAIL(get_schema_version_in_inner_table(sql_client, schema_status, schema_version))) {
          } else if (schema_version <= OB_CORE_SCHEMA_VERSION + 1) {
            ret = OB_EAGAIN;
            LOG_WARN("schema may be not persisted, try again",
                     KR(ret), K(schema_status), K(schema_version));
          } else if (core_schema_version > schema_version) {
            ret = OB_ERR_UNEXPECTED;
            LOG_ERROR("schema version fallback, unexpected",
                      KR(ret), K(schema_status), K(core_schema_version), K(schema_version));
          } else if (OB_FAIL(check_core_schema_change_(sql_client, schema_status,
                     core_schema_version, core_schema_change))) {
          } else if (core_schema_change) {
            sys_schema_change = true;
            LOG_WARN("core schema version change, try again",
                     KR(ret), K(schema_status), K(core_schema_version), K(schema_version));
          } else if (OB_FAIL(check_sys_schema_change(sql_client, schema_status,
              local_schema_version, schema_version, sys_schema_change))) {
          } else if (sys_schema_change) {
            // for sys table schema, we publish as sys_temp_version
            const int64_t sys_formal_version = std::max(core_schema_version, schema_version);
            int64_t publish_version = 0;
            if (OB_FAIL(ObSchemaService::gen_sys_temp_version(sys_formal_version, publish_version))) {
            } else if (OB_FAIL(try_fetch_publish_sys_schemas(schema_status,
                                                             schema_version,
                                                             publish_version,
                                                             sql_client,
                                                             sys_schema_change,
                                                             table_schemas))) {
            }
          }

          if (OB_FAIL(ret)) {
            // check whether failed because of core table schema change, go to suitable pos
            int temp_ret = OB_SUCCESS;
            if (OB_SUCCESS != (temp_ret = check_core_schema_change_(
                sql_client, schema_status, core_schema_version, core_schema_change))) {
            } else if (core_schema_change) {
              sys_schema_change = true;
              LOG_WARN("core schema version change, try again",
                       KR(ret), K(schema_status), K(core_schema_version), K(schema_version));
              ret = OB_SUCCESS;
            }
          }
        }

        // refresh full normal schema by schema_version
        if (OB_SUCC(ret) && !core_schema_change && !sys_schema_change) {
          const int64_t fetch_version = std::max(core_schema_version, schema_version);
          if (FALSE_IT(schema_mgr_for_cache = ATOMIC_LOAD(&schema_mgr_for_cache_))) {
            LOG_WARN("fail to get schema_mgr_for_cache", KR(ret), K(schema_status));
          } else if (OB_ISNULL(schema_mgr_for_cache)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("schema mgr for cache is null", KR(ret), K(schema_status));
          } else if (OB_FAIL(refresh_runtime_full_schema(sql_client, schema_status, fetch_version, table_schemas))) {
          } else {
            const int64_t publish_version = std::max(core_schema_version, schema_version);
            schema_mgr_for_cache->set_schema_version(publish_version);
            if (OB_FAIL(publish_schema())) {
            } else {
              LOG_INFO("publish full normal schema by schema_version succeed", K(schema_status),
                       K(publish_version), K(core_schema_version), K(schema_version));
            }
          }
          if (OB_FAIL(ret)) {
            // check whether failed because of sys table schema change, go to suitable pos,
            // if during check core table schema change, go to suitable pos
            int temp_ret = OB_SUCCESS;
            if (OB_SUCCESS != (temp_ret = check_core_or_sys_schema_change(
                sql_client, schema_status, core_schema_version, schema_version,
                core_schema_change, sys_schema_change))) {
            } else if (core_schema_change || sys_schema_change) {
              ret = OB_SUCCESS;
            }
          }
        }
        ++retry_count;
      } // end while

      // It must be reset before each refresh schema to prevent ddl from being in progress,
      // but refresh full may have added some tables
      // And the latter table was deleted again, at this time refresh will not delete this table in the cache
      if (OB_SUCC(ret)) {
        // full runtime schema refresh completes bootstrap optimizations.
        {
          if (nullptr != in_bootstrap_) {
            *in_bootstrap_ = false;
          }
        }
        break;
      } else {
        FLOG_WARN("[REFRESH_SCHEMA] refresh full schema failed, do some clear", KR(ret), K(schema_status));
        int tmp_ret = OB_SUCCESS;
        schema_mgr_for_cache = ATOMIC_LOAD(&schema_mgr_for_cache_);
        if (OB_ISNULL(schema_mgr_for_cache)) {
          tmp_ret = OB_ERR_UNEXPECTED;
          LOG_ERROR("schema mgr for cache is null", KR(ret), K(tmp_ret), K(schema_status));
        } else if (FALSE_IT(schema_mgr_for_cache->reset())) {
        } else if (OB_SUCCESS != (tmp_ret = init_runtime_basic_schema())) {
        }
      }
    }
  }
  return ret;
}

int ObServerSchemaService::init_schema_struct()
{
  int ret = OB_SUCCESS;
  {

#define INIT_RUNTIME_MEM_MGR(member, mem_mgr_label, schema_mgr_label) \
    if (OB_FAIL(ret)) { \
    } else if (OB_ISNULL(member)) { \
      void *buff = ob_malloc(sizeof(ObSchemaMemMgr), lib::ObMemAttr(mem_mgr_label, ObCtxIds::SCHEMA_SERVICE)); \
      ObSchemaMemMgr *schema_mem_mgr = NULL; \
      if (OB_ISNULL(buff)) { \
        ret = OB_ALLOCATE_MEMORY_FAILED; \
        SQL_PC_LOG(ERROR, "alloc schema_mem_mgr failed", K(ret)); \
      } else if (NULL == (schema_mem_mgr = new(buff)ObSchemaMemMgr())) { \
        ret = OB_NOT_INIT; \
        SQL_PC_LOG(WARN, "fail to constructor schema_mem_mgr", K(ret)); \
      } else if (OB_FAIL(schema_mem_mgr->init(schema_mgr_label))) { \
        LOG_WARN("fail to init schema_mem_mgr", K(ret)); \
      } else { \
        member = schema_mem_mgr; \
      } \
      if (OB_FAIL(ret)) { \
        if (NULL != schema_mem_mgr) { \
          schema_mem_mgr->~ObSchemaMemMgr(); \
          ob_free(buff); \
          schema_mem_mgr = NULL; \
          buff = NULL; \
        } else if (NULL != buff) { \
          ob_free(buff); \
          buff = NULL; \
        } \
      } \
    } else { \
      LOG_INFO("schema_mgr_for_cache exist", K(ret)); \
    }

    INIT_RUNTIME_MEM_MGR(mem_mgr_,
                        ObModIds::OB_RUNTIME_SCHEMA_MEM_MGR, ObModIds::OB_RUNTIME_SCHEMA_MGR);

#undef INIT_RUNTIME_MEM_MGR

    if (OB_FAIL(ret)) {
    } else if (!refresh_full_schema_present_) {
      refresh_full_schema_ = true;
      refresh_full_schema_present_ = true;
    } else {
      LOG_INFO("refresh_full_schema exist", K(ret));
    }

    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(schema_mgr_for_cache_)) {
      ObSchemaMgr *schema_mgr_for_cache = NULL;
      ObSchemaMemMgr *mem_mgr = NULL;
      if (FALSE_IT(mem_mgr = mem_mgr_)) {
      } else if (OB_ISNULL(mem_mgr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("mem_mgr is null", K(ret));
      } else if (OB_FAIL(mem_mgr->alloc_schema_mgr(schema_mgr_for_cache))) {
      } else if (OB_FAIL(schema_mgr_for_cache->init())) {
      } else if (FALSE_IT(ATOMIC_STORE(&schema_mgr_for_cache_, schema_mgr_for_cache))) {
        LOG_WARN("fail to set schema_mgr", K(ret));
      }
      if (OB_FAIL(ret) && OB_NOT_NULL(mem_mgr)) {
        int64_t tmp_ret = OB_SUCCESS;
        if (OB_TMP_FAIL(mem_mgr->free_schema_mgr(schema_mgr_for_cache))) {
        }
      }
    } else {
      LOG_INFO("schema_mgr exist", K(ret));
    }
  }
  return ret;
}

int ObServerSchemaService::check_need_refresh_increment_sys_schema_(ObISQLClient &sql_client,
      const int64_t &local_schema_version,
      int64_t &core_schema_version,
      bool &core_schema_change,
      bool &sys_schema_change)
{
  int ret = OB_SUCCESS;
  int64_t sys_schema_version = 0;
  core_schema_change = true;
  sys_schema_change = true;
  if (OB_ISNULL(schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("pointer is null", K(ret), KP(schema_service_));
  } else if (OB_FAIL(schema_service_->get_core_and_sys_version(sql_client,
          core_schema_version, sys_schema_version))) {
  }
  if (OB_FAIL(ret)) {
  } else if (core_schema_version <= local_schema_version) {
    core_schema_change = false;
  }
  if (OB_FAIL(ret)) {
  } else if (0 == sys_schema_version || OB_INVALID_VERSION == sys_schema_version) {
    // The system schema version is not initialized during bootstrap.
    sys_schema_change = true;
  } else if (core_schema_version > sys_schema_version) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sys_schema_version is invalid", KR(ret), K(sys_schema_version), K(core_schema_version));
  } else if (sys_schema_version <= local_schema_version) {
    sys_schema_change = false;
    LOG_INFO("skip refresh core and sys schema", KR(ret), K(sys_schema_version),
        K(core_schema_version), K(local_schema_version));
  }
  return ret;
}

int ObServerSchemaService::refresh_increment_core_schema_(
    const ObRefreshSchemaStatus &schema_status,
    ObISQLClient &sql_client,
    const int64_t &local_schema_version,
    const int64_t &core_schema_version)
{
  int ret = OB_SUCCESS;
  bool core_schema_change = false;
  if (core_schema_version <= OB_CORE_SCHEMA_VERSION + 1) {
    ret = OB_EAGAIN;
    LOG_WARN("schema may be not persisted, try again",
        KR(ret), K(schema_status), K(core_schema_version));
  } else if (core_schema_version <= local_schema_version) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("core_schema_version should be larger than local_schema_version", KR(ret),
        K(core_schema_version), K(local_schema_version));
  } else {
    int64_t publish_version = OB_INVALID_INDEX;
    if (OB_FAIL(ObSchemaService::gen_core_temp_version(
            core_schema_version, publish_version))) {
    } else if (OB_FAIL(try_fetch_publish_core_schemas(schema_status,
            core_schema_version, publish_version, sql_client, core_schema_change))) {
    }
  }
  return ret;
}
int ObServerSchemaService::refresh_increment_sys_schema_(
    const ObRefreshSchemaStatus &schema_status,
    ObISQLClient &sql_client,
    const int64_t &local_schema_version,
    const int64_t &core_schema_version,
    const int64_t &schema_version_in_inner_table)
{
  int ret = OB_SUCCESS;
  bool core_schema_change = false;
  bool sys_schema_change = false;
  if (schema_version_in_inner_table < local_schema_version) {
    if (local_schema_version <= OB_CORE_SCHEMA_VERSION + 1) {
      ret = OB_EAGAIN;
      LOG_WARN("schema may be not persisted, try again",
          KR(ret), K(schema_status), K(schema_version_in_inner_table), K(local_schema_version));
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("schema version fallback, unexpected",
          KR(ret), K(schema_status), K(schema_version_in_inner_table), K(local_schema_version));
    }
  } else if (core_schema_version > schema_version_in_inner_table) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("schema version fallback, unexpected",
        KR(ret), K(schema_status), K(core_schema_version), K(schema_version_in_inner_table));
  } else {
    int64_t publish_version = 0;
    if (OB_FAIL(ObSchemaService::gen_sys_temp_version(schema_version_in_inner_table, publish_version))) {
    } else if (OB_FAIL(try_fetch_publish_sys_schemas(schema_status, schema_version_in_inner_table,
            publish_version, sql_client, sys_schema_change))) {
    }
  }
  return ret;
}

int ObServerSchemaService::refresh_increment_all_schema_(
      const ObRefreshSchemaStatus &schema_status,
      ObISQLClient &sql_client,
      const int64_t &core_schema_version,
      const int64_t &schema_version_in_inner_table,
      const int64_t &local_schema_version,
      ObSchemaMgr *&schema_mgr_for_cache)
{
  int ret = OB_SUCCESS;
  const int64_t fetch_version = std::max(core_schema_version, schema_version_in_inner_table);

  ObSchemaService::SchemaOperationSetWithAlloc schema_operations;
  if (OB_ISNULL(schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("pointer is null", K(ret), KP(schema_service_));
  } else if (FALSE_IT(schema_mgr_for_cache = ATOMIC_LOAD(&schema_mgr_for_cache_))) {
    LOG_WARN("fail to get schema_mgr_for_cache", KR(ret), K(schema_status));
  } else if (OB_ISNULL(schema_mgr_for_cache)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema mgr for cache is null", KR(ret));
  } else if (OB_FAIL(schema_service_->get_increment_schema_operations(schema_status,
          local_schema_version, fetch_version, sql_client, schema_operations))) {
  } else if (schema_operations.count() > 0) {
    // new cache
    SMART_VAR(AllSchemaKeys, all_keys) {
      if (OB_FAIL(replay_log(*schema_mgr_for_cache, schema_operations, all_keys))) {
      } else if (OB_FAIL(update_schema_mgr(sql_client, schema_status,
              *schema_mgr_for_cache, fetch_version, all_keys))){
      }
    }
  }
  if (OB_SUCC(ret)) {
    schema_mgr_for_cache->set_schema_version(fetch_version);
    if (OB_FAIL(publish_schema())) {
    } else {
      LOG_INFO("change schema version", K(schema_status),
          K(schema_version_in_inner_table), K(core_schema_version));
    }
  }
  return ret;
}

int ObServerSchemaService::refresh_increment_schema(
    const ObRefreshSchemaStatus &schema_status)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;

  ObSchemaMgr *schema_mgr_for_cache = NULL;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret), K(schema_status));
  } else if (FALSE_IT(schema_mgr_for_cache = ATOMIC_LOAD(&schema_mgr_for_cache_))) {
    LOG_WARN("fail to get schema_mgr_for_cache", KR(ret), K(schema_status));
  } else if (OB_ISNULL(schema_mgr_for_cache)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema mgr for cache is null", KR(ret), K(schema_status));
  } else {
    bool core_schema_change = true;
    bool sys_schema_change = true;
    int64_t local_schema_version = schema_mgr_for_cache->get_schema_version();
    int64_t core_schema_version = 0;
    int64_t schema_version = OB_INVALID_VERSION;
    int64_t retry_count = 0;
    const int64_t start_ts = ObTimeUtility::current_time();
    int64_t abs_timeout = OB_INVALID_TIMESTAMP;
    if (OB_FAIL(ObShareUtil::get_abs_timeout(MAX_FETCH_SCHEMA_TIMEOUT_US, abs_timeout))) {
    }
    while (OB_SUCC(ret)) {
      if (OB_FAIL(check_stop())) {
        LOG_WARN("observer is stopping", KR(ret), K(schema_status));
        break;
      } else if (retry_count > 0) {
        LOG_WARN("refresh_increment_schema failed", K(retry_count), K(schema_status));
        const int64_t current_ts = ObTimeUtility::current_time();
        if (current_ts >= abs_timeout) {
          // ret will be overwrite when core/system table schemas were changed in the meantime.
          // In such situations, try use timeout remain to retry locally.
          ret = OB_TIMEOUT;
          LOG_WARN("already timeout", KR(ret), K(start_ts), K(abs_timeout), K(current_ts), K(abs_timeout));
          break;
        }
      }
      ObISQLClient &sql_client = *sql_proxy_;
      if (OB_FAIL(check_need_refresh_increment_sys_schema_(sql_client,
              local_schema_version, core_schema_version, core_schema_change, sys_schema_change))) {
      } else if (core_schema_change && OB_FAIL(refresh_increment_core_schema_(schema_status,
              sql_client, local_schema_version, core_schema_version))) {
        LOG_ERROR("failed to refresh core schema", KR(ret), K(schema_status), K(local_schema_version),
              K(core_schema_version), K(core_schema_change));
      } else if (OB_FAIL(get_schema_version_in_inner_table(sql_client, schema_status, schema_version))) {
      } else if ((core_schema_change || sys_schema_change) && OB_FAIL(refresh_increment_sys_schema_(
              schema_status, sql_client, local_schema_version, core_schema_version, schema_version))) {
        LOG_ERROR("failed to refresh sys schema", KR(ret), K(schema_status), K(local_schema_version),
            K(core_schema_version), K(schema_version), K(core_schema_change), K(sys_schema_change));
      } else if (OB_FAIL(check_core_schema_change_(sql_client, schema_status, core_schema_version, core_schema_change))) {
      } else if (core_schema_change) {
        // the first two stage refresh rely on core schema
        // make sure core schema not changed in the first two stage
        ret = OB_EAGAIN;
        LOG_WARN("core schema change", KR(ret), K(schema_status), K(core_schema_version),
            K(core_schema_change));
      } else if (OB_FAIL(refresh_increment_all_schema_(schema_status, sql_client, core_schema_version,
                 schema_version, local_schema_version, schema_mgr_for_cache))) {
      } else {
        break;
      }
      if (OB_FAIL(ret)) {
        // check whether failed because of sys table schema change, go to suitable pos,
        // if during check core table schema change, go to suitable pos
        if (OB_TMP_FAIL(check_core_or_sys_schema_change(sql_client, schema_status,
                core_schema_version, schema_version, core_schema_change, sys_schema_change))) {
        } else if (core_schema_change || sys_schema_change) {
          ret = OB_SUCCESS;
        }
      }
      ++retry_count;
    } // end while

    if (OB_FAIL(ret)) {
      schema_mgr_for_cache = ATOMIC_LOAD(&schema_mgr_for_cache_);
      if (OB_ISNULL(schema_mgr_for_cache)) {
        tmp_ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("schema mgr for cache is null", KR(ret), K(tmp_ret), K(schema_status));
      } else if (schema_mgr_for_cache->get_schema_version() != local_schema_version) {
        // Rrefresh increment schema may success partially and local schema version may be enhanced.
        // To avoid missing increment ddl operations, local schema version should be reset to last schema version
        // before refresh increment schema in the next round.
        schema_mgr_for_cache->set_schema_version(local_schema_version);
        FLOG_WARN("[REFRESH_SCHEMA] refresh increment schema failed, try reset to last schema version",
                  KR(ret), "last_schema_version", local_schema_version,
                  "cur_schema_version", schema_mgr_for_cache->get_schema_version());
      }
    }
  }

  return ret;
}

int ObServerSchemaService::try_fetch_publish_core_schemas(
    const ObRefreshSchemaStatus &schema_status,
    const int64_t core_schema_version,
    const int64_t publish_version,
    ObISQLClient &sql_client,
    bool &core_schema_change)
{
  int ret = OB_SUCCESS;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else {
    ObArray<ObTableSchema> core_schemas;
    ObArray<uint64_t> core_table_ids;
    if (OB_FAIL(schema_service_->get_core_table_schemas(
        sql_client, schema_status, core_schemas))) {
    } else if (OB_FAIL(check_core_schema_change_(sql_client, schema_status,
               core_schema_version, core_schema_change))) {
    } else if (core_schema_change) {
      LOG_WARN("core schema version change",
               KR(ret), K(schema_status), K(core_schema_version));
    } else {
      // core schema don't change, publish core schemas
      ObArray<ObTableSchema *> core_tables;
      for (int64_t i = 0; i < core_schemas.count() && OB_SUCC(ret); ++i) {
        if (OB_FAIL(core_tables.push_back(&core_schemas.at(i)))) {
        }
      }
      if (OB_SUCC(ret)) {
        ObSchemaMgr *schema_mgr_for_cache = NULL;
        auto attr = lib::ObMemAttr("PubCoreSchema", ObCtxIds::SCHEMA_SERVICE);
        ObArenaAllocator allocator(attr);
        ObArray<ObSimpleTableSchemaV2*> simple_core_schemas(
                         common::OB_MALLOC_NORMAL_BLOCK_SIZE,
                         common::ModulePageAllocator(allocator));
        if (FALSE_IT(schema_mgr_for_cache = ATOMIC_LOAD(&schema_mgr_for_cache_))) {
          LOG_WARN("fail to get schema mgr for cache", KR(ret), K(schema_status));
        } else if (OB_ISNULL(schema_mgr_for_cache)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("schema_mgr_for_cache is null", KR(ret), K(schema_status));
        } else if (OB_FAIL(update_schema_cache(core_tables))) {
        } else if (OB_FAIL(convert_to_simple_schema(allocator, core_schemas, simple_core_schemas))) {
        } else if (OB_FAIL(schema_mgr_for_cache->add_tables(simple_core_schemas))) {
        } else if (FALSE_IT(schema_mgr_for_cache->set_schema_version(publish_version))){
        } else if (OB_FAIL(publish_schema())) {
        } else {
          FLOG_INFO("[REFRESH_SCHEMA] refresh core table schema succeed",
                    K(schema_status),
                    K(publish_version),
                    K(core_schema_version),
                    K(schema_mgr_for_cache->get_schema_version()));
        }
      }
    }
  }

  return ret;
}

int ObServerSchemaService::try_fetch_publish_sys_schemas(
    const ObRefreshSchemaStatus &schema_status,
    const int64_t schema_version,
    const int64_t publish_version,
    common::ObISQLClient &sql_client,
    bool &sys_schema_change,
    common::ObIArray<share::schema::ObTableSchema> *table_schemas)
{
  int ret = OB_SUCCESS;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret), K(schema_status));
  } else {
    ObArenaAllocator allocator(ObModIds::OB_SCHEMA_SYS_SCHEMA);
    ObArray<ObTableSchema *> sys_schemas;
    ObArray<uint64_t> sys_table_ids;
    int64_t new_schema_version = 0;
    if (OB_FAIL(get_sys_table_ids(sys_table_ids))) {
    } else if (is_in_bootstrap() && OB_FAIL(construct_related_table_schemas(sys_table_ids, table_schemas, sys_schemas))) {
      LOG_WARN("construct sys table schemas from bootstrap schemas failed", KR(ret), K(schema_status));
    } else if (OB_FAIL(schema_service_->get_sys_table_schemas(
               sql_client, schema_status, sys_table_ids, allocator, sys_schemas))) {
    } else if (OB_FAIL(get_schema_version_in_inner_table(sql_client, schema_status, new_schema_version))) {
    } else if (OB_FAIL(check_sys_schema_change(sql_client,
                                               schema_status,
                                               schema_version,
                                               new_schema_version,
                                               sys_schema_change))) {
    } else if (sys_schema_change) {
      LOG_WARN("sys schema change during refresh full schema",
               K(schema_status), K(schema_version), K(new_schema_version));
    } else if (!sys_schema_change) {
      ObSchemaMgr *schema_mgr_for_cache = NULL;
      auto attr = lib::ObMemAttr("PubSysSchema", ObCtxIds::SCHEMA_SERVICE);
      ObArenaAllocator allocator(attr);
      ObArray<ObSimpleTableSchemaV2*> simple_sys_schemas(
                       common::OB_MALLOC_NORMAL_BLOCK_SIZE,
                       common::ModulePageAllocator(allocator));
      if (FALSE_IT(schema_mgr_for_cache = ATOMIC_LOAD(&schema_mgr_for_cache_))) {
        LOG_WARN("fail to get schema mgr for cache", KR(ret));
      } else if (OB_ISNULL(schema_mgr_for_cache)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("schema_mgr_for_cache is null", KR(ret), K(schema_status));
      } else if (OB_FAIL(update_schema_cache(sys_schemas))) {
      } else if (OB_FAIL(convert_to_simple_schema(allocator, sys_schemas, simple_sys_schemas))) {
      } else if (OB_FAIL(schema_mgr_for_cache->add_tables(simple_sys_schemas))) {
      } else if (FALSE_IT(schema_mgr_for_cache->set_schema_version(publish_version))){
      } else if (OB_FAIL(publish_schema())) {
      } else {
        FLOG_INFO("[REFRESH_SCHEMA] refresh sys table schema succeed",
                  K(schema_status),
                  K(publish_version),
                  K(schema_version),
                  K(schema_mgr_for_cache->get_schema_version()));
      }
    }
  }
  return ret;
}


int ObServerSchemaService::add_runtime_schema_to_cache(
    ObISQLClient &sql_client,
    const int64_t schema_version)
{
  int ret = OB_SUCCESS;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", K(ret));
  } else if (schema_version < 0) {
    ret =  OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(schema_version));
  } else {
    ObSEArray<ObServerRuntimeSchema, 1> runtime_schema_array;
    if (OB_FAIL(schema_service_->get_runtime_schemas(
               sql_client, schema_version, runtime_schema_array))) {
    } else if (OB_FAIL(update_schema_cache(runtime_schema_array))) {
    }
  }

  return ret;
}

int ObServerSchemaService::add_sys_variable_schema_to_cache(
    ObISQLClient &sql_client,
    const ObRefreshSchemaStatus &schema_status,
    const int64_t schema_version)
{
  int ret = OB_SUCCESS;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", K(ret));
  } else if (schema_version < 0) {
    ret =  OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(schema_version));
  } else {
    ObSysVariableSchema new_sys_variable;
    if (OB_FAIL(schema_service_->get_sys_variable_schema(
        sql_client, schema_status, schema_version, new_sys_variable))) {
    } else if (OB_FAIL(update_schema_cache(new_sys_variable))) {
    }
  }

  return ret;
}

// new schema full refresh
int ObServerSchemaService::refresh_runtime_full_schema(
    ObISQLClient &sql_client,
    const ObRefreshSchemaStatus &schema_status,
    const int64_t schema_version,
    common::ObIArray<share::schema::ObTableSchema> *table_schemas)
{
  int ret = OB_SUCCESS;

  ObSchemaMgr *schema_mgr_for_cache = NULL;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", K(ret));
  } else if (schema_version < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(schema_version));
  } else if (FALSE_IT(schema_mgr_for_cache = ATOMIC_LOAD(&schema_mgr_for_cache_))) {
    LOG_WARN("fail to get schema_mgr_for_cache", K(ret), K(schema_status));
  } else if (OB_ISNULL(schema_mgr_for_cache)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema mgr for cache is null", K(ret));
  } else {
    // Publish the server runtime schema before the dependent schemas.
    {
      ObSEArray<ObSimpleServerRuntimeSchema, 1> simple_runtimes;
      if (OB_FAIL(schema_service_->get_runtime_schemas(sql_client,
                                                   schema_version,
                                                   simple_runtimes))) {
      } else if (simple_runtimes.count() != 1) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid runtime schema count", K(ret), K(simple_runtimes.count()));
      } else {
        const ObSimpleServerRuntimeSchema &simple_runtime = simple_runtimes.at(0);
        if (simple_runtime.is_restore()) {
          ObSchemaStatusProxy *schema_status_proxy = schema_status_proxy_;
          if (OB_ISNULL(schema_status_proxy)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("schema_status_proxy is null", KR(ret));
          } else if (OB_FAIL(schema_status_proxy->load_refresh_schema_status())) {
          }
        }
        if (OB_SUCC(ret) && OB_FAIL(schema_mgr_for_cache->add_runtime_schema(simple_runtime))) {
          LOG_WARN("add runtime schema failed", K(ret), K(simple_runtime));
        } else if (OB_SUCC(ret) && OB_FAIL(add_runtime_schema_to_cache(sql_client, schema_version))) {
          LOG_WARN("add runtime schema to cache failed", K(ret), K(schema_version));
        } else if (OB_SUCC(ret) && OB_FAIL(add_sys_variable_schema_to_cache(sql_client, schema_status, schema_version))) {
          LOG_WARN("add sys variable schema to cache failed", K(ret), K(schema_version));
        }
      }
    }

    if (OB_SUCC(ret)) {
      auto attr = lib::ObMemAttr("RefFullSchema", ObCtxIds::SCHEMA_SERVICE);
      ObArenaAllocator allocator(attr);
      #define INIT_ARRAY(TYPE, name) \
        ObArray<TYPE> name(common::OB_MALLOC_NORMAL_BLOCK_SIZE, \
                           common::ModulePageAllocator(allocator));
      INIT_ARRAY(ObSimpleUserSchema, simple_users);
      INIT_ARRAY(ObSimpleDatabaseSchema, simple_databases);
      INIT_ARRAY(ObSimpleTableSchemaV2*, simple_tables);
      INIT_ARRAY(ObSimpleOutlineSchema, simple_outlines);
      INIT_ARRAY(ObSimpleRoutineSchema, simple_routines);
      INIT_ARRAY(ObSimplePackageSchema, simple_packages);
      INIT_ARRAY(ObSimpleTriggerSchema, simple_triggers);
      INIT_ARRAY(ObDBPriv, db_privs);
      INIT_ARRAY(ObSysPriv, sys_privs);
      INIT_ARRAY(ObTablePriv, table_privs);
      INIT_ARRAY(ObRoutinePriv, routine_privs);
      INIT_ARRAY(ObColumnPriv, column_privs);
      INIT_ARRAY(ObObjPriv, obj_privs);
      INIT_ARRAY(ObObjMysqlPriv, obj_mysql_privs);
      INIT_ARRAY(ObSimpleMockFKParentTableSchema, simple_mock_fk_parent_tables);
      INIT_ARRAY(ObAiModelSchema, simple_ai_models);
      #undef INIT_ARRAY
      ObSimpleSysVariableSchema simple_sys_variable;

      if (OB_FAIL(schema_service_->get_sys_variable(sql_client, schema_status,
          schema_version, simple_sys_variable))) {
      } else if (OB_FAIL(schema_service_->get_all_users(
          sql_client, schema_status, schema_version, simple_users))) {
      } else if (OB_FAIL(schema_service_->get_all_databases(
          sql_client, schema_status, schema_version, simple_databases))) {
      } else if (!is_in_bootstrap() && OB_FAIL(schema_service_->get_all_tables(
          sql_client, allocator, schema_status, schema_version, simple_tables))) {
        LOG_WARN("get all table schema failed", KR(ret), K(schema_version));
      } else if (OB_FAIL(schema_service_->get_all_outlines(
          sql_client, schema_status, schema_version, simple_outlines))) {
      } else if (OB_FAIL(schema_service_->get_all_routines(
          sql_client, schema_status, schema_version, simple_routines))) {
      } else if (OB_FAIL(schema_service_->get_all_packages(
          sql_client, schema_status, schema_version, simple_packages))) {
      } else if (OB_FAIL(schema_service_->get_all_triggers(
          sql_client, schema_status, schema_version, simple_triggers))) {
      } else if (OB_FAIL(schema_service_->get_all_db_privs(
          sql_client, schema_status, schema_version, db_privs))) {
      } else if (OB_FAIL(schema_service_->get_all_table_privs(
          sql_client, schema_status, schema_version, table_privs))) {
      } else if (OB_FAIL(schema_service_->get_all_obj_privs(
          sql_client, schema_status, schema_version, obj_privs))) {
      } else if (OB_FAIL(schema_service_->get_all_sys_privs(
          sql_client, schema_status, schema_version, sys_privs))) {
      } else if (OB_FAIL(schema_service_->get_all_mock_fk_parent_tables(
          sql_client, schema_status, schema_version, simple_mock_fk_parent_tables))) {
      } else {
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(schema_service_->get_all_routine_privs(
          sql_client, schema_status, schema_version, routine_privs))) {
        }
      }

      if (OB_SUCC(ret)) {
        if (OB_FAIL(schema_service_->get_all_column_privs(
          sql_client, schema_status, schema_version, column_privs))) {
        }
      }

      if (OB_SUCC(ret)) {
        if (OB_FAIL(schema_service_->get_all_ai_models(
          sql_client, schema_status, schema_version, simple_ai_models))) {
        }
      }

      if (OB_SUCC(ret)) {
        if (OB_FAIL(schema_service_->get_all_obj_mysql_privs(
          sql_client, schema_status, schema_version, obj_mysql_privs))) {
        }
      }

      const bool refresh_full_schema = true;
      // add simple schema for cache
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(schema_mgr_for_cache->sys_variable_mgr_
                         .add_sys_variable(simple_sys_variable))) {
      } else if (OB_FAIL(schema_mgr_for_cache->add_users(simple_users))) {
      } else if (OB_FAIL(schema_mgr_for_cache->add_databases(simple_databases))) {
      } else if (!is_in_bootstrap() && OB_FAIL(schema_mgr_for_cache->add_tables(simple_tables))) {
        LOG_WARN("add tables failed", K(ret));
      } else if (OB_FAIL(schema_mgr_for_cache->outline_mgr_.add_outlines(simple_outlines))) {
      } else if (OB_FAIL(schema_mgr_for_cache->routine_mgr_.add_routines(simple_routines))) {
      } else if (OB_FAIL(schema_mgr_for_cache->package_mgr_.add_packages(simple_packages))) {
      } else if (OB_FAIL(schema_mgr_for_cache->trigger_mgr_.add_triggers(simple_triggers))) {
      } else if (OB_FAIL(schema_mgr_for_cache->priv_mgr_.add_db_privs(db_privs))) {
      } else if (OB_FAIL(schema_mgr_for_cache->priv_mgr_.add_table_privs(table_privs))) {
      } else if (OB_FAIL(schema_mgr_for_cache->priv_mgr_.add_routine_privs(routine_privs))) {
      } else if (OB_FAIL(schema_mgr_for_cache->priv_mgr_.add_obj_privs(obj_privs))) {
      } else if (OB_FAIL(schema_mgr_for_cache->priv_mgr_.add_obj_mysql_privs(obj_mysql_privs))) {
      } else if (OB_FAIL(schema_mgr_for_cache->priv_mgr_.add_sys_privs(sys_privs))) {
      } else if (OB_FAIL(schema_mgr_for_cache->mock_fk_parent_table_mgr_.add_mock_fk_parent_tables(
                         simple_mock_fk_parent_tables))) {
      } else if (OB_FAIL(schema_mgr_for_cache->priv_mgr_.add_column_privs(column_privs))) {
      } else if (OB_FAIL(schema_mgr_for_cache->add_ai_models(simple_ai_models))) {
      }

      LOG_INFO("add runtime schemas finish", K(schema_version), K(schema_status),
               "users", simple_users.count(),
               "databases", simple_databases.count(),
               "outlines", simple_outlines.count(),
               "db_privs", db_privs.count(),
               "table_privs", table_privs.count());
      // the parameters count of previous LOG_INFO has reached maximum,
      // so we need a new LOG_INFO.
      LOG_INFO("add runtime schemas finish", K(schema_version), K(schema_status),
               "sys_privs", sys_privs.count(),
               "obj_mysql_privs", obj_mysql_privs.count());
    }

    if (OB_SUCC(ret)) {
      ObArenaAllocator allocator;
      ObArray<uint64_t> non_sys_table_ids;
      ObArray<ObTableSchema *> non_sys_tables;
      ObArray<ObSimpleTableSchemaV2*> simple_non_sys_schemas(common::OB_MALLOC_NORMAL_BLOCK_SIZE, common::ModulePageAllocator(allocator));
      if (OB_FAIL(schema_mgr_for_cache->get_non_sys_table_ids(non_sys_table_ids))) {
      } else if (is_in_bootstrap() && OB_FAIL(construct_related_table_schemas(non_sys_table_ids, table_schemas, non_sys_tables))) {
        LOG_WARN("construct non sys tables from bootstrap schemas failed", KR(ret), K(schema_status));
      } else if (OB_FAIL(schema_service_->get_batch_table_schema(
                 schema_status, schema_version, non_sys_table_ids, sql_client,
                 allocator, non_sys_tables))) {
      } else if (OB_FAIL(update_non_sys_schemas_in_cache_(*schema_mgr_for_cache, non_sys_tables))) {
      } else if (OB_FAIL(convert_to_simple_schema(allocator, non_sys_tables, simple_non_sys_schemas))) {
      } else if (OB_FAIL(schema_mgr_for_cache->add_tables(simple_non_sys_schemas))) {
      } else {
        LOG_INFO("[REFRESH_SCHEMA] refresh non sys table schema succeed",
                 K(schema_status),
                 K(schema_version),
                 K(schema_mgr_for_cache->get_schema_version()));
      }
    }
  }
  return ret;
}

int ObServerSchemaService::construct_related_table_schemas(
    const common::ObIArray<uint64_t> &table_ids,
    common::ObIArray<share::schema::ObTableSchema> *table_schemas,
    common::ObIArray<share::schema::ObTableSchema *> &tables)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(table_schemas)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("table_schemas is null", KR(ret));
  } else {
    common::hash::ObHashMap<uint64_t, ObTableSchema*> tid_to_schema;
    if (OB_FAIL(tid_to_schema.create(hash::cal_next_prime(table_schemas->count()), "TidToSchema"))) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < table_schemas->count(); ++i) {
        ObTableSchema &table_schema = table_schemas->at(i);
        const uint64_t table_id = table_schema.get_table_id();
        if (OB_FAIL(tid_to_schema.set_refactored(table_id, &table_schema))) {
        }
      }
    }

    if (OB_SUCC(ret)) {
      for (int64_t i = table_ids.count() - 1; OB_SUCC(ret) && i >= 0; --i) {
        ObTableSchema *table_schema = nullptr;
        const uint64_t table_id = table_ids.at(i);
        if (OB_FAIL(tid_to_schema.get_refactored(table_id, table_schema))) {
        } else if (OB_FAIL(tables.push_back(table_schema))) {
        }
      }
    }
  }
  return ret;
}

int ObServerSchemaService::get_schema_version_in_inner_table(
    ObISQLClient &sql_client,
    const ObRefreshSchemaStatus &schema_status,
    int64_t &target_version)
{
  int ret = OB_SUCCESS;

  const bool did_use_weak = (schema_status.snapshot_timestamp_ >= 0);
  if (OB_FAIL(schema_service_->fetch_schema_version(
      schema_status, sql_client, target_version))) {
  }
  return ret;
}

int ObServerSchemaService::check_core_or_sys_schema_change(
    ObISQLClient &sql_client,
    const ObRefreshSchemaStatus &schema_status,
    const int64_t core_schema_version,
    const int64_t schema_version,
    bool &core_schema_change,
    bool &sys_schema_change)
{
  int ret = OB_SUCCESS;
  int64_t new_schema_version = 0;
  // check whether failed because of sys table schema change, go to suitable pos
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret), K(schema_status));
  } else if (OB_FAIL(get_schema_version_in_inner_table(
    sql_client, schema_status, new_schema_version))) {
  } else if (OB_FAIL(check_core_schema_change_(sql_client, schema_status,
             core_schema_version, core_schema_change))) {
  } else if (core_schema_change) {
    sys_schema_change = true;
    LOG_WARN("core schema change", KR(ret), K(schema_status), K(core_schema_version), K(new_schema_version));
  } else if (OB_FAIL(check_sys_schema_change(sql_client, schema_status,
             schema_version, new_schema_version, sys_schema_change))) {
  }
  return ret;
}

int ObServerSchemaService::check_core_schema_change_(
    ObISQLClient &sql_client,
    const ObRefreshSchemaStatus &schema_status,
    const int64_t core_schema_version,
    bool &core_schema_change)
{
  int ret = OB_SUCCESS;
  int64_t new_core_schema_version = OB_INVALID_VERSION;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret), K(schema_status));
  } else if (OB_FAIL(schema_service_->get_core_version(sql_client, schema_status, new_core_schema_version))) {
  } else if (core_schema_version != new_core_schema_version) {
    core_schema_change = true;
    LOG_WARN("core schema change during refresh sys schema", KR(ret),
             K(schema_status), K(core_schema_version), K(new_core_schema_version));
  } else {
    core_schema_change = false;
    LOG_INFO("core schema is not changed", KR(ret),
             K(schema_status), K(core_schema_version), K(new_core_schema_version));
  }
  return ret;
}

int ObServerSchemaService::check_sys_schema_change(
    ObISQLClient &sql_client,
    const ObRefreshSchemaStatus &schema_status,
    const int64_t schema_version,
    const int64_t new_schema_version,
    bool &sys_schema_change)
{
  int ret = OB_SUCCESS;
  ObArray<uint64_t> table_ids;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret), K(schema_status));
  } else if (OB_FAIL(get_sys_table_ids(table_ids))) {
  } else if (OB_FAIL(schema_service_->check_sys_schema_change(sql_client, schema_status,
             table_ids, schema_version, new_schema_version, sys_schema_change))) {
  }
  return ret;
}

int ObServerSchemaService::get_sys_table_ids(ObIArray<uint64_t> &table_ids) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(get_table_ids(sys_table_schema_creators, table_ids))) {
  } else if (OB_FAIL(ObSysTableChecker::add_sys_table_index_ids(table_ids))) {
  } else if (OB_FAIL(add_sys_table_lob_aux_ids(table_ids))) {
  }
  return ret;
}

int ObServerSchemaService::get_table_ids(
    const schema_create_func *schema_creators,
    ObIArray<uint64_t> &table_ids) const
{
  int ret = OB_SUCCESS;
  ObTableSchema schema;
  table_ids.reset();
  if (OB_ISNULL(schema_creators)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("schema creators should not be null", KR(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && NULL != schema_creators[i]; ++i) {
    schema.reset();
    if (OB_FAIL(schema_creators[i](schema))) {
    } else if (OB_FAIL(table_ids.push_back(schema.get_table_id()))) {
    }
  }
  return ret;
}

int ObServerSchemaService::add_sys_table_lob_aux_ids(ObIArray<uint64_t> &table_ids) const
{
  int ret = OB_SUCCESS;
  {
    int64_t tbl_cnt = table_ids.count();
    // add sys table lob aux table id
    for (int64_t i = 0; OB_SUCC(ret) && i < tbl_cnt; i++) {
      uint64_t data_table_id = table_ids.at(i);
      uint64_t lob_meta_table_id = 0;
      uint64_t lob_piece_table_id = 0;
      if (is_system_table(data_table_id)) {
        if (OB_ALL_CORE_TABLE_TID == data_table_id) {
            // do nothing
        } else if (!(get_sys_table_lob_aux_table_id(data_table_id, lob_meta_table_id, lob_piece_table_id))) {
          ret = OB_ENTRY_NOT_EXIST;
          LOG_WARN("get lob aux table id failed.", K(ret), K(data_table_id));
        } else if (lob_meta_table_id == 0 || lob_piece_table_id == 0) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("get lob aux table id failed.", K(ret), K(data_table_id), K(lob_meta_table_id), K(lob_piece_table_id));
        } else if (OB_FAIL(table_ids.push_back(lob_meta_table_id))) {
        } else if (OB_FAIL(table_ids.push_back(lob_piece_table_id))) {
        }
      }
    }
  }
  return ret;
}

int ObServerSchemaService::construct_schema_version_history(
    const ObRefreshSchemaStatus &schema_status,
    const int64_t snapshot_version,
    const VersionHisKey &key,
    VersionHisVal &val)
{
  int ret = OB_SUCCESS;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", K(ret));
  } else if (OB_FAIL(schema_service_->construct_schema_version_history(
             schema_status, *sql_proxy_, snapshot_version, key, val))) {
  }

  return ret;
}

int ObServerSchemaService::get_runtime_schema_version(int64_t &schema_version)
{
  int ret = OB_SUCCESS;
  SpinRLockGuard guard(schema_manager_rwlock_);
  schema_version = OB_INVALID_VERSION;
  // Atomic_refactored held the 1-entry map's bucket lock ACROSS the
  // deref; reproduce that with schema_mgr_for_cache_rwlock_ held across load+deref, otherwise
  // switch_allocator_ can ATOMIC_STORE+free_schema_mgr(old) between our load and deref -> UAF.
  SpinRLockGuard cache_guard(schema_mgr_for_cache_rwlock_);
  ObSchemaMgr *schema_mgr_for_cache = schema_mgr_for_cache_;
  if (OB_ISNULL(schema_mgr_for_cache)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to get schema mgr for cache", K(ret));
  } else {
    schema_version = schema_mgr_for_cache->get_schema_version();
  }
  return ret;
}

int ObServerSchemaService::get_refresh_schema_info(ObRefreshSchemaInfo &schema_info)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is null", K(ret));
  } else if (OB_FAIL(schema_service_->get_refresh_schema_info(schema_info))) {
  }
  return ret;
}

int ObServerSchemaService::get_increment_obj_mysql_priv_keys(
  const ObSchemaMgr &schema_mgr,
  const ObSchemaOperation &schema_operation,
  AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;

  if (!(schema_operation.op_type_ > OB_DDL_OBJ_MYSQL_PRIV_OPERATION_BEGIN
        && schema_operation.op_type_ < OB_DDL_OBJ_MYSQL_PRIV_OPERATION_END)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(schema_operation.op_type_), KR(ret));
  } else {

    const uint64_t user_id = schema_operation.user_id_;
    const ObString &obj_name = schema_operation.obj_name_;
    const int64_t obj_type = schema_operation.obj_type_;
    const int64_t schema_version = schema_operation.schema_version_;
    int hash_ret = OB_SUCCESS;
    SchemaKey obj_mysql_priv_key;

    obj_mysql_priv_key.user_id_ = user_id;
    obj_mysql_priv_key.obj_name_ = obj_name;
    obj_mysql_priv_key.obj_type_ = obj_type;
    obj_mysql_priv_key.schema_version_ = schema_version;
    if (OB_DDL_DEL_OBJ_MYSQL_PRIV == schema_operation.op_type_) { //delete
      hash_ret = schema_keys.new_obj_mysql_priv_keys_.erase_refactored(obj_mysql_priv_key);
      if (OB_SUCCESS != hash_ret && OB_HASH_NOT_EXIST != hash_ret) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Failed to del obj_mysql_priv_key from new_obj_mysql_priv_keys", KR(ret));
      } else {
        const ObObjMysqlPriv *obj_mysql_priv = NULL;
        if (OB_FAIL(schema_mgr.priv_mgr_.get_obj_mysql_priv(
          ObObjMysqlPrivSortKey(user_id, obj_name, obj_type), obj_mysql_priv))) {
        } else if (NULL != obj_mysql_priv) {
          hash_ret = schema_keys.del_obj_mysql_priv_keys_.set_refactored_1(obj_mysql_priv_key, 1);
          if (OB_SUCCESS != hash_ret) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("Failed to add obj_mysql_priv_key to del_obj_mysql_priv_keys", KR(ret));
          }
        }
      }
    } else {
      hash_ret = schema_keys.new_obj_mysql_priv_keys_.set_refactored_1(obj_mysql_priv_key, 1);
      if (OB_SUCCESS != hash_ret) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Failed to add new obj_mysql_priv_key", KR(ret));
      }
    }
  }
  return ret;
}

int ObServerSchemaService::get_increment_obj_mysql_priv_keys_reversely(
  const ObSchemaMgr &schema_mgr,
  const ObSchemaOperation &schema_operation,
  AllSchemaKeys &schema_keys)
{
  int ret = OB_SUCCESS;
  if (!(schema_operation.op_type_ > OB_DDL_OBJ_MYSQL_PRIV_OPERATION_BEGIN
        && schema_operation.op_type_ < OB_DDL_OBJ_MYSQL_PRIV_OPERATION_END)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(schema_operation.op_type_), KR(ret));
  } else {

    const uint64_t user_id = schema_operation.user_id_;
    const ObString &obj_name = schema_operation.obj_name_;
    const int64_t obj_type = schema_operation.obj_type_;
    const int64_t schema_version = schema_operation.schema_version_;
    int hash_ret = OB_SUCCESS;
    SchemaKey obj_mysql_priv_key;

    obj_mysql_priv_key.user_id_ = user_id;
    obj_mysql_priv_key.obj_name_ = obj_name;
    obj_mysql_priv_key.obj_type_ = obj_type;
    obj_mysql_priv_key.schema_version_ = schema_version;

    bool is_delete = (OB_DDL_GRANT_OBJ_MYSQL_PRIV == schema_operation.op_type_);
    bool is_exist = false;
    const ObObjMysqlPriv *obj_mysql_priv = NULL;
    if (OB_FAIL(schema_mgr.priv_mgr_.get_obj_mysql_priv(obj_mysql_priv_key.get_obj_mysql_priv_key(),
                                                    obj_mysql_priv))) {
    } else if (NULL != obj_mysql_priv) {
      is_exist = true;
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(REPLAY_OP(obj_mysql_priv_key, schema_keys.del_obj_mysql_priv_keys_,
          schema_keys.new_obj_mysql_priv_keys_, is_delete, is_exist))) {
      }
    }
  }
  return ret;
}

int ObSchemaVersionGetter::operator() (common::hash::HashMapPair<uint64_t, ObSchemaMgr *> &entry)
{
  int ret = common::OB_SUCCESS;
  if (OB_ISNULL(entry.second)) {
    ret = common::OB_ERR_UNEXPECTED;
  } else {
    schema_version_ = (entry.second)->get_schema_version();
  }
  return ret;
}

}    //end of namespace schema
}    //end of namespace share
}    //end of namespace oceanbase
