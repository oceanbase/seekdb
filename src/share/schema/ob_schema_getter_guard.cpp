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


#include "ob_schema_getter_guard.h"
#include "ob_ai_model_schema_getter_guard.ipp"

#include <string.h>

#include "lib/encrypt/ob_encrypted_helper.h"
#include "lib/net/ob_net_util.h"
#include "share/ob_schema_status_proxy.h"
#include "lib/alloc/alloc_struct.h"
#include "lib/alloc/ob_iallocator.h"
#include "lib/container/ob_iarray.h"
#include "lib/ob_check_macros.h"
#include "lib/oblog/ob_log_level.h"
#include "lib/oblog/ob_log_print_kv.h"
#include "lib/string/ob_string.h"
#include "lib/utility/alloc_assist.h"
#include "lib/utility/ob_backtrace.h"
#include "lib/utility/ob_mod_define.h"
#include "lib/utility/utility.h"
#include "object/ob_object.h"
#include "share/cache/ob_kv_storecache.h"
#include "share/config/ob_server_config.h"
#include "share/config/ob_runtime_config.h"
#include "share/inner_table/ob_inner_table_schema.h"
#include "share/inner_table/ob_inner_table_schema_constants.h"
#include "share/ob_errno.h"
#include "share/ob_force_print_log.h"
#include "share/ob_server_struct.h"
#include "share/schema/ob_column_schema.h"
#include "share/schema/ob_mock_fk_parent_table_mgr.h"
#include "share/schema/ob_multi_version_schema_service.h"
#include "share/schema/ob_outline_mgr.h"
#include "share/schema/ob_package_mgr.h"
#include "share/schema/ob_priv_mgr.h"
#include "share/schema/ob_routine_mgr.h"
#include "share/schema/ob_schema_mgr.h"
#include "share/schema/ob_schema_service.h"
#include "share/schema/ob_sys_variable_mgr.h"
#include "share/schema/ob_table_schema.h"
#include "share/schema/ob_trigger_info.h"
#include "share/schema/ob_trigger_mgr.h"

namespace oceanbase
{
using namespace common;

namespace share
{
namespace schema
{
ObSchemaMgrInfo::~ObSchemaMgrInfo()
{
  mgr_handle_.reset();
}

void ObSchemaMgrInfo::reset()
{
  
  snapshot_version_ = OB_INVALID_VERSION;
  schema_mgr_ = NULL;
  mgr_handle_.reset();
  schema_status_.reset();
}

ObSchemaMgrInfo &ObSchemaMgrInfo::operator=(const ObSchemaMgrInfo &other)
{
  if (this != &other) {
    
    snapshot_version_ = other.snapshot_version_;
    schema_mgr_ = other.schema_mgr_;
    mgr_handle_ = other.mgr_handle_;
    schema_status_ = other.schema_status_;
  }
  return *this;
}

ObSchemaMgrInfo::ObSchemaMgrInfo(const ObSchemaMgrInfo &other)
  : snapshot_version_(common::OB_INVALID_VERSION),
    schema_mgr_(NULL),
    mgr_handle_(),
    schema_status_()
{
  *this = other;
}

ObSchemaGetterGuard::ObSchemaGetterGuard()
  : local_allocator_(lib::ObMemAttr(ObModIds::OB_SCHEMA_MGR_INFO_ARRAY, ObCtxIds::SCHEMA_SERVICE)),
    schema_service_(NULL),
    session_id_(0),
    schema_mgr_infos_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(local_allocator_)),
    schema_objs_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(local_allocator_)),
    mod_(ObSchemaMgrItem::MOD_STACK),
    schema_guard_type_(INVALID_SCHEMA_GUARD_TYPE),
    is_inited_(false),
    pin_cache_size_(0)
{
}

ObSchemaGetterGuard::ObSchemaGetterGuard(const ObSchemaMgrItem::Mod mod)
  : local_allocator_(lib::ObMemAttr(ObModIds::OB_SCHEMA_MGR_INFO_ARRAY, ObCtxIds::SCHEMA_SERVICE)),
    schema_service_(NULL),
    session_id_(0),
    schema_mgr_infos_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(local_allocator_)),
    schema_objs_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(local_allocator_)),
    mod_(mod),
    schema_guard_type_(INVALID_SCHEMA_GUARD_TYPE),
    is_inited_(false),
    pin_cache_size_(0)
{
}

ObSchemaGetterGuard::~ObSchemaGetterGuard()
{
  // Destruct handles_ will reduce reference count automatically.
  if (pin_cache_size_ >= FULL_SCHEMA_MEM_THREHOLD) {
    int ret = OB_SUCCESS;
    FLOG_WARN("hold too much full schema memory", K(pin_cache_size_), K(lbt()));
  }
}

int ObSchemaGetterGuard::init()
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret));
  } else {
    pin_cache_size_ = 0;
    is_inited_ = true;
  }
  return ret;
}

int ObSchemaGetterGuard::reset()
{
  int ret = OB_SUCCESS;
  schema_service_ = NULL;
  schema_objs_.reset();

  if (pin_cache_size_ >= FULL_SCHEMA_MEM_THREHOLD) {
    FLOG_WARN("hold too much full schema memory", K(pin_cache_size_), K(lbt()));
  }
  pin_cache_size_ = 0;
  

  for (int64_t i = 0; i < schema_mgr_infos_.count(); i++) {
    schema_mgr_infos_.at(i).reset();
  }
  schema_mgr_infos_.reset();
  local_allocator_.reuse();

  // mod_ should not be reset

  is_inited_ = false;
  return ret;
}


int ObSchemaGetterGuard::get_schema_version(int64_t &schema_version) const
{
  int ret = OB_SUCCESS;
  const ObSchemaMgrInfo *schema_mgr_info = NULL;
  if (OB_FAIL(get_schema_mgr_info( schema_mgr_info))) {
  } else if (OB_ISNULL(schema_mgr_info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_mgr_info is null", KR(ret));
  } else {
    schema_version = schema_mgr_info->get_snapshot_version();
  }
  return ret;
}

// For SQL only
int ObSchemaGetterGuard::get_can_read_index_array(
    const uint64_t table_id,
    uint64_t *index_tid_array,
    int64_t &size,
    bool with_global_index /* =true */,
    bool with_domain_index /*=true*/,
    bool with_spatial_index /*=true*/,
    bool with_vector_index /*=true*/)
{
  int ret = OB_SUCCESS;
  const ObTableSchema *table_schema = NULL;
  if (OB_FAIL(get_table_schema( table_id, table_schema))
             || OB_ISNULL(table_schema)) {
    //TODO: ignore error even when table doesn't exist ?
    LOG_WARN("cannot get table schema for table  ", K(table_id), KR(ret));
  } else {
    ObSEArray<ObAuxTableMetaInfo, 16> simple_index_infos;
    const ObTableSchema *index_schema = NULL;
    int64_t can_read_count = 0;
    bool is_geo_default_srid = false;
    if (OB_FAIL(table_schema->get_simple_index_infos(simple_index_infos))) {
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < simple_index_infos.count(); ++i) {
      const uint64_t index_id = simple_index_infos.at(i).table_id_;
      if (OB_FAIL(get_table_schema( index_id, index_schema))) {
      } else if (OB_ISNULL(index_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("index schema should not be null", KR(ret), K(index_id));
      } else if (index_schema->is_spatial_index() && !with_spatial_index) {
        uint64_t geo_col_id = UINT64_MAX;
        const ObColumnSchemaV2 *geo_column = NULL;
        is_geo_default_srid = false;
        if (OB_FAIL(index_schema->get_spatial_geo_column_id(geo_col_id))) {
        } else if (OB_ISNULL(geo_column = table_schema->get_column_schema(geo_col_id))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("failed to get geometry column", K(ret), K(geo_col_id));
        } else if (geo_column->is_default_srid()) {
          is_geo_default_srid = true;
        }
      }
      if (OB_SUCC(ret)) {
        if (!with_global_index && index_schema->is_global_index_table()) {
          // skip
        } else if (!with_domain_index && index_schema->is_fts_index()) {
          // does not need domain index, skip it
        } else if (!with_spatial_index && index_schema->is_spatial_index() && is_geo_default_srid) {
          // skip spatial index when geometry column has not specific srid.
        } else if (!with_vector_index && index_schema->is_vec_index()) {
          // skip vector index
        } else if (index_schema->can_read_index() && index_schema->is_index_visible()) {
          index_tid_array[can_read_count++] = simple_index_infos.at(i).table_id_;
        } else {
          // Do nothing.
        }
      }
    }
    size = can_read_count;
  }

  return ret;
}

int ObSchemaGetterGuard::check_has_local_unique_index(const uint64_t table_id,
    bool &has_local_unique_index)
{
  int ret = OB_SUCCESS;
  const ObTableSchema *table_schema = NULL;
  ObSEArray<ObAuxTableMetaInfo, 16> simple_index_infos;
  const ObSimpleTableSchemaV2 *index_schema = NULL;
  has_local_unique_index = false;
  if (OB_FAIL(get_table_schema( table_id, table_schema))) {
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("cannot get table schema for table ", KR(ret), K(table_id));
  } else if (OB_FAIL(table_schema->get_simple_index_infos(simple_index_infos))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < simple_index_infos.count(); ++i) {
    const uint64_t index_id = simple_index_infos.at(i).table_id_;
    if (OB_FAIL(get_simple_table_schema( index_id, index_schema))) {
    } else if (OB_ISNULL(index_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("cannot get index table schema for table ",
               KR(ret), K(index_id));
    } else if (OB_UNLIKELY(index_schema->is_final_invalid_index())) {
      //invalid index status, need ingore
    } else if (index_schema->is_local_unique_index_table()) {
      has_local_unique_index = true;
      break;
    }
  }
  return ret;
}




int ObSchemaGetterGuard::get_sys_variable_schema(
                                                 const ObSysVariableSchema *&sys_variable_schema)
{
  int ret = OB_SUCCESS;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_FAIL(get_schema(SYS_VARIABLE_SCHEMA,
                                1UL,
                                sys_variable_schema))) {
  }
  return ret;
}

int ObSchemaGetterGuard::get_sys_variable_schema(
                                                 const ObSimpleSysVariableSchema *&sys_variable_schema)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->sys_variable_mgr_.get_sys_variable_schema( sys_variable_schema))) {
  }
  return ret;
}

int ObSchemaGetterGuard::get_system_variable(const ObString &var_name, const ObSysVarSchema *&var_schema)
{
  int ret = OB_SUCCESS;
  const ObSysVariableSchema *sys_variable_schema = NULL;
  if (OB_FAIL(get_sys_variable_schema( sys_variable_schema))) {
  } else if (NULL == sys_variable_schema) {
    ret = OB_ENTRY_NOT_EXIST;
    LOG_WARN("system variable schema does not exist");
  } else if (OB_FAIL(sys_variable_schema->get_sysvar_schema(var_name, var_schema))) {
    if (OB_ERR_SYS_VARIABLE_UNKNOWN != ret) {
      LOG_WARN("get sysvar schema failed", K(var_name));
    }
  }
  return ret;
}

int ObSchemaGetterGuard::get_system_variable(ObSysVarClassType var_id, const ObSysVarSchema *&var_schema)
{
  int ret = OB_SUCCESS;
  const ObSysVariableSchema *sys_variable_schema = NULL;
  if (OB_FAIL(get_sys_variable_schema( sys_variable_schema))) {
  } else if (NULL == sys_variable_schema) {
    ret = OB_ENTRY_NOT_EXIST;
    LOG_WARN("system variable schema does not exist");
  } else if (OB_FAIL(sys_variable_schema->get_sysvar_schema(var_id, var_schema))) {
    if (OB_ERR_SYS_VARIABLE_UNKNOWN != ret) {
      LOG_WARN("get sysvar schema failed", K(var_id));
    }
  }
  return ret;
}

int ObSchemaGetterGuard::get_user_id(const ObString &user_name,
                                     const ObString &host_name,
                                     uint64_t &user_id,
                                     const bool is_role /*false*/)
{
  int ret = OB_SUCCESS;
  UNUSED(is_role);
  const ObSchemaMgr *mgr = NULL;
  user_id = OB_INVALID_ID;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else {
    const ObSimpleUserSchema *simple_user = NULL;
    if (0 == user_name.case_compare(OB_SYS_USER_NAME)
               && 0 == host_name.case_compare(OB_SYS_HOST_NAME)) {
      // root maps to the system user id.
      user_id = OB_SYS_USER_ID;
    } else if (OB_FAIL(mgr->get_user_schema(
                                             user_name,
                                             host_name,
                                             simple_user))) {
    } else if (NULL == simple_user) {
      LOG_INFO("user not exist", K(user_name), K(host_name));
    } else {
      user_id = simple_user->get_user_id();
    }
  }

  return ret;
}

int ObSchemaGetterGuard::get_trigger_ids_in_database(const uint64_t database_id,
                                                     ObIArray<uint64_t> &trigger_ids)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  trigger_ids.reset();

  ObArray<const ObSimpleTriggerSchema *> tg_schemas;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == database_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(database_id));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->trigger_mgr_.get_trigger_schemas_in_database(database_id, tg_schemas))) {
  } else if (OB_FAIL(trigger_ids.reserve(tg_schemas.count()))) {
  } else {
    FOREACH_CNT_X(tg, tg_schemas, OB_SUCC(ret)) {
      const ObSimpleTriggerSchema *tmp_tg = *tg;
      if (OB_ISNULL(tmp_tg)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("NULL ptr", KR(ret), KP(tmp_tg));
      } else if (OB_FAIL(trigger_ids.push_back(tmp_tg->get_trigger_id()))) {
      }
    }
  }
  return ret;
}

int ObSchemaGetterGuard::get_routine_ids_in_database(const uint64_t database_id,
                                                     common::ObIArray<uint64_t> &routine_ids)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  routine_ids.reset();

  ObArray<const ObSimpleRoutineSchema *> schemas;
  if (OB_UNLIKELY(!check_inner_stat())) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == database_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(database_id));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->routine_mgr_.get_routine_schemas_in_database(database_id, schemas))) {
  } else if (OB_FAIL(routine_ids.reserve(schemas.count()))) {
  } else {
    FOREACH_CNT_X(schema, schemas, OB_SUCC(ret)) {
      const ObSimpleRoutineSchema *tmp_schema = *schema;
      if (OB_ISNULL(tmp_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("NULL ptr", KR(ret), KP(tmp_schema));
      } else if (OB_FAIL(routine_ids.push_back(tmp_schema->get_routine_id()))) {
      }
    }
  }
  return ret;
}

int ObSchemaGetterGuard::get_routine_info_in_package(const uint64_t package_id,
                                                     const uint64_t subprogram_id,
                                                     const ObRoutineInfo *&routine_info)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  routine_info = NULL;

  ObArray<const ObSimpleRoutineSchema *> schemas;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == package_id || OB_INVALID_ID == subprogram_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(package_id), K(subprogram_id));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->routine_mgr_.get_routine_schemas_in_package(package_id, schemas))) {
  } else {
    bool is_break = false;
    FOREACH_CNT_X(schema, schemas, (OB_SUCC(ret) && !is_break)) {
      const ObSimpleRoutineSchema *tmp_schema = *schema;
      const ObRoutineInfo *sub_routine_info = NULL;
      if (OB_ISNULL(tmp_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("NULL ptr", KR(ret), KP(tmp_schema));
      } else if (OB_FAIL(get_schema(ROUTINE_SCHEMA,
                                    tmp_schema->get_routine_id(),
                                    sub_routine_info,
                                    tmp_schema->get_schema_version()))) {
      } else if (OB_ISNULL(sub_routine_info)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("routine info is null", KR(ret));
      } else if (subprogram_id == sub_routine_info->get_subprogram_id()) {
        routine_info = sub_routine_info;
        is_break = true;
      }
    }
  }

  return ret;
}

int ObSchemaGetterGuard::get_routine_infos_in_package(const uint64_t package_id,
  common::ObIArray<const ObRoutineInfo *> &routine_infos)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  routine_infos.reset();

  ObArray<const ObSimpleRoutineSchema *> schemas;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == package_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(package_id));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->routine_mgr_.get_routine_schemas_in_package(package_id, schemas))) {
  } else {
    FOREACH_CNT_X(schema, schemas, OB_SUCC(ret)) {
      const ObSimpleRoutineSchema *tmp_schema = *schema;
      const ObRoutineInfo *routine_info = NULL;
      if (OB_ISNULL(tmp_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("NULL ptr", KR(ret), KP(tmp_schema));
      } else if (OB_FAIL(get_schema(ROUTINE_SCHEMA,
                                    tmp_schema->get_routine_id(),
                                    routine_info,
                                    tmp_schema->get_schema_version()))) {
      } else if (OB_FAIL(routine_infos.push_back(routine_info))) {
      }
    }
  }

  return ret;
}

// For SQL only
int ObSchemaGetterGuard::get_can_write_index_array(const uint64_t table_id,
    uint64_t *index_tid_array,
    int64_t &size,
    bool only_global)
{
  int ret = OB_SUCCESS;
  const ObTableSchema *table_schema = NULL;
  ObSEArray<ObAuxTableMetaInfo, 16> simple_index_infos;
  int64_t can_write_count = 0;
  const ObSimpleTableSchemaV2 *index_schema = NULL;
  if (OB_FAIL(get_table_schema( table_id, table_schema))) {
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("cannot get table schema for table ", KR(ret), K(table_id));
  } else if (OB_FAIL(table_schema->get_simple_index_infos(simple_index_infos))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < simple_index_infos.count(); ++i) {
    const uint64_t index_id = simple_index_infos.at(i).table_id_;
    if (OB_FAIL(get_simple_table_schema( index_id, index_schema))) {
    } else if (OB_ISNULL(index_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("cannot get index table schema for table ", KR(ret), K(index_id));
    } else if (OB_UNLIKELY(index_schema->is_final_invalid_index())) {
      //invalid index status, need ingore
    } else if (OB_MAX_AUX_TABLE_PER_MAIN_TABLE <= can_write_count) {
      ret = OB_ERR_TOO_MANY_KEYS;
      LOG_USER_ERROR(OB_ERR_TOO_MANY_KEYS, OB_MAX_INDEX_PER_TABLE);
      LOG_WARN("too many indexes or index auxiliary tables", K(can_write_count), K(OB_MAX_AUX_TABLE_PER_MAIN_TABLE));
    } else if (!only_global) {
      index_tid_array[can_write_count] = simple_index_infos.at(i).table_id_;
      ++can_write_count;
    } else if (index_schema->is_global_index_table()) {
      index_tid_array[can_write_count] = simple_index_infos.at(i).table_id_;
      ++can_write_count;
    }
  }
  size = can_write_count;

  return ret;
}

// check if column is included in primary key/partition key/foreign key/index columns.

int ObSchemaGetterGuard::get_database_id(const ObString &database_name,
                                         uint64_t &database_id)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  database_id = OB_INVALID_ID;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (database_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(database_name), KR(ret));
  } else {
    const ObSimpleDatabaseSchema *simple_database = NULL;
    if ((database_name.length() == static_cast<int32_t> (strlen(OB_SYS_DATABASE_NAME)))
        && (0 == STRNCASECMP(database_name.ptr(), OB_SYS_DATABASE_NAME, strlen(OB_SYS_DATABASE_NAME)))) {
      // Avoid cyclic dependencies while initializing the server runtime.
      database_id = OB_SYS_DATABASE_ID;
    } else {
      if (OB_FAIL(check_lazy_guard( mgr))) {
      } else if (OB_FAIL(mgr->get_database_schema(
                                            database_name,
                                            simple_database))) {
      } else if (NULL == simple_database) {
        LOG_INFO("database not exist", K(database_name));
      } else {
        database_id = simple_database->get_database_id();
      }
    }
  }

  return ret;
}

int ObSchemaGetterGuard::get_table_id(uint64_t database_id,
                                      const ObString &table_name,
                                      const bool is_index,
                                      const CheckTableType check_type, // check if temporary table is visable
                                      uint64_t &table_id,
                                      const bool is_built_in_index/* = false*/)
{
  int ret = OB_SUCCESS;
  uint64_t session_id = session_id_;
  const ObSchemaMgr *mgr = NULL;
  const ObSimpleTableSchemaV2 *simple_table = NULL;
  table_id = OB_INVALID_ID;

  if (NON_TEMP_WITH_NON_HIDDEN_TABLE_TYPE == check_type) {
    session_id = 0;
  } else { /* do nothing */ }
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == database_id
             || table_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(database_id), K(table_name), KR(ret));
  } else {
    if (OB_FAIL(check_lazy_guard( mgr))) {
    } else if (OB_FAIL(mgr->get_table_schema(
                       database_id,
                       session_id,
                       table_name,
                       is_index,
                       simple_table,
                       USER_HIDDEN_TABLE_TYPE == check_type ? true : false,
                       is_built_in_index))) {
    } else if (NULL == simple_table) {
      if (OB_CORE_SCHEMA_VERSION != mgr->get_schema_version()) {
        // this log is useless when observer restarts.
        LOG_INFO("table not exist", K(database_id),
                 K(session_id), K(table_name), K(is_index),
                 "schema_version", mgr->get_schema_version());
      }
    } else {
      if (TEMP_TABLE_TYPE == check_type
          && !is_inner_table(simple_table->get_table_id())
          && false == simple_table->is_tmp_table()) {
        // temporary table is not finded.
      } else {
        table_id = simple_table->get_table_id();
      }
    }
  }
  return ret;
}

int ObSchemaGetterGuard::get_table_id(const ObString &database_name,
                                     const ObString &table_name,
                                     const bool is_index,
                                     const CheckTableType check_type,  // check if temporary table is visable
                                     uint64_t &table_id,
                                     const bool is_built_in_index/* = false*/)
{
  int ret = OB_SUCCESS;
  table_id = OB_INVALID_ID;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (database_name.empty()
             || table_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(database_name), K(table_name), KR(ret));
  } else {
    uint64_t database_id = OB_INVALID_ID;
    if (OB_FAIL(get_database_id(database_name, database_id))) {
    } else if (OB_INVALID_ID == database_id) {
      // do-nothing
    } else if (OB_FAIL(get_table_id(database_id, table_name, is_index,
                                    check_type, table_id, is_built_in_index))){
    }
  }

  return ret;
}

int ObSchemaGetterGuard::get_foreign_key_id(const uint64_t database_id,
                                            const ObString &foreign_key_name,
                                            uint64_t &foreign_key_id)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  foreign_key_id = OB_INVALID_ID;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == database_id
             || foreign_key_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(database_id), K(foreign_key_name));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->get_foreign_key_id(database_id, foreign_key_name, foreign_key_id))) {
  } else if (OB_INVALID_ID == foreign_key_id) {
    LOG_INFO("foreign key not exist", K(database_id), K(foreign_key_name));
  }

  return ret;
}

int ObSchemaGetterGuard::get_foreign_key_info(
                                            const uint64_t database_id,
                                            const ObString &foreign_key_name,
                                            ObSimpleForeignKeyInfo &foreign_key_info)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  foreign_key_info.reset();
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == database_id
             || foreign_key_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(database_id), K(foreign_key_name));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->get_foreign_key_info( database_id,
                                              foreign_key_name, foreign_key_info))) {
  } else if (OB_INVALID_ID == foreign_key_info.foreign_key_id_) {
    LOG_INFO("foreign key not exist", K(database_id), K(foreign_key_name));
  }

  return ret;
}

int ObSchemaGetterGuard::get_constraint_id(const uint64_t database_id,
                                           const ObString &constraint_name,
                                           uint64_t &constraint_id)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  constraint_id = OB_INVALID_ID;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == database_id ||
             constraint_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(database_id), K(constraint_name));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->get_constraint_id(database_id, constraint_name, constraint_id))) {
  } else if (OB_INVALID_ID == constraint_id) {
    LOG_INFO("constraint not exist", K(database_id), K(constraint_name));
  }

  return ret;
}

int ObSchemaGetterGuard::get_constraint_info(
                                            const uint64_t database_id,
                                            const common::ObString &constraint_name,
                                            ObSimpleConstraintInfo &constraint_info) const
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  constraint_info.reset();
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == database_id ||
             constraint_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(database_id), K(constraint_name));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->get_constraint_info( database_id,
                                              constraint_name, constraint_info))) {
  } else if (OB_INVALID_ID == constraint_info.constraint_id_) {
    LOG_INFO("constraint not exist", K(database_id), K(constraint_name));
  }

  return ret;
}

// basic interface
int ObSchemaGetterGuard::get_server_runtime_info(const ObServerRuntimeSchema *&runtime_schema)
{
  int ret = OB_SUCCESS;
  runtime_schema = NULL;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_FAIL(get_schema(SERVER_RUNTIME_SCHEMA,
                                1UL,
                                runtime_schema))) {
  }

  return ret;
}

int ObSchemaGetterGuard::get_server_runtime_info(const ObSimpleServerRuntimeSchema *&runtime_schema)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  runtime_schema = NULL;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else {
    ret = mgr->get_server_runtime_schema( runtime_schema);
  }

  return ret;
}

int ObSchemaGetterGuard::get_user_info(const uint64_t user_id,
    const ObUserInfo *&user_info)
{
  int ret = OB_SUCCESS;
  user_info = NULL;


  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == user_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(user_id), KR(ret));
  } else if (OB_FAIL(get_schema(USER_SCHEMA,
                                user_id,
                                user_info))) {
  }

  return ret;
}

int ObSchemaGetterGuard::get_database_schema(
                                             const uint64_t database_id,
                                             const ObDatabaseSchema *&database_schema)
{
  int ret = OB_SUCCESS;
  database_schema = NULL;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == database_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(database_id), KR(ret));
  } else if (OB_FAIL(get_schema(DATABASE_SCHEMA,
                                database_id,
                                database_schema))) {
  }

  return ret;
}

int ObSchemaGetterGuard::get_database_schema(
                                             const uint64_t database_id,
                                             const ObSimpleDatabaseSchema *&database_schema)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  database_schema = NULL;


  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == database_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(database_id), KR(ret));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else {
    ret = mgr->get_database_schema( database_id, database_schema);
  }

  return ret;
}

int ObSchemaGetterGuard::get_table_schema(
    const uint64_t table_id,
    const ObTableSchema *&table_schema)
{
  int ret = OB_SUCCESS;
  table_schema = NULL;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == table_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(table_id), K(ret));
  } else if (is_cte_table(table_id)) {
    // fake table is only used in sql execution process and doesn't have schema.
    // We should avoid error in such situation.
  } else if (OB_FAIL(get_schema(TABLE_SCHEMA,
                                table_id,
                                table_schema))) {
  }
  return ret;
}

int ObSchemaGetterGuard::get_server_runtime_info(const ObString &runtime_name,
                                         const ObServerRuntimeSchema *&runtime_info)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  runtime_info = NULL;

  const ObSimpleServerRuntimeSchema *simple_runtime = NULL;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (runtime_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(runtime_name), KR(ret));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->get_server_runtime_schema(runtime_name, simple_runtime))) {
  } else if (NULL == simple_runtime) {
    LOG_INFO("runtime schema does not exist", K(runtime_name));
  } else if (OB_FAIL(get_schema(SERVER_RUNTIME_SCHEMA,
                                1UL,
                                runtime_info,
                                simple_runtime->get_schema_version()))) {
  } else if (OB_ISNULL(runtime_info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", KR(ret), K(runtime_name));
  }

  return ret;
}

int ObSchemaGetterGuard::get_user_info(const ObString &user_name,
                                       const ObString &host_name,
                                       const ObUserInfo *&user_info)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  user_info = NULL;

  const ObSimpleUserSchema *simple_user = NULL;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->get_user_schema(
                                          user_name,
                                          host_name,
                                          simple_user))) {
  } else if (NULL == simple_user) {
    LOG_INFO("user not exist", K(user_name));
  } else if (OB_FAIL(get_schema(USER_SCHEMA,
                                simple_user->get_user_id(),
                                user_info,
                                simple_user->get_schema_version()))) {
  } else if (OB_ISNULL(user_info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", KR(ret), K(user_name));
  }
  return ret;
}

int ObSchemaGetterGuard::get_user_info(const ObString &user_name,
                                       ObIArray<const ObUserInfo *> &users_info)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else {
    const int64_t DEFAULT_SAME_USERNAME_COUNT = 4;
    ObSEArray<const ObSimpleUserSchema *, DEFAULT_SAME_USERNAME_COUNT> simple_users;
    if (OB_FAIL(mgr->get_user_schema( user_name, simple_users))) {
    } else if (simple_users.empty()) {
      LOG_INFO("user not exist", K(user_name));
    } else {
      const ObUserInfo *user_info = NULL;
      for (int64_t i = 0; i < simple_users.count() && OB_SUCC(ret); ++i) {
        const ObSimpleUserSchema *&simple_user = simple_users.at(i);
        if (OB_FAIL(get_schema(USER_SCHEMA,
                               simple_user->get_user_id(),
                               user_info,
                               simple_user->get_schema_version()))) {
        } else if (OB_ISNULL(user_info)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("NULL ptr", KR(ret), KP(user_info));
        } else if (OB_FAIL(users_info.push_back(user_info))) {
        } else {
          user_info = NULL;
        }
      }
    }
  }
  return ret;
}

int ObSchemaGetterGuard::get_database_schema(
                                             const ObString &database_name,
                                             const ObDatabaseSchema *&database_schema)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  database_schema = NULL;

  const ObSimpleDatabaseSchema *simple_database = NULL;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (database_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(database_name), KR(ret));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->get_database_schema(
                                               database_name,
                                               simple_database))) {
  } else if (NULL == simple_database) {
    LOG_INFO("database not exist", K(database_name));
  } else if (OB_FAIL(get_schema(DATABASE_SCHEMA,
                                simple_database->get_database_id(),
                                database_schema,
                                simple_database->get_schema_version()))) {
  } else if (OB_ISNULL(database_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", KR(ret), KP(database_schema));
  }

  return ret;
}

int ObSchemaGetterGuard::get_simple_table_schema(
    const uint64_t database_id,
    const ObString &table_name,
    const bool is_index,
    const ObSimpleTableSchemaV2 *&simple_table_schema,
    const bool with_hidden_flag/*false*/,
    const bool is_built_in_index/*false*/)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  simple_table_schema = NULL;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == database_id
             || table_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(database_id), K(table_name), KR(ret));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->get_table_schema(
                                           database_id,
                                           session_id_,
                                           table_name,
                                           is_index,
                                           simple_table_schema,
                                           with_hidden_flag,
                                           is_built_in_index))) {
  }
  return ret;
}

int ObSchemaGetterGuard::get_table_schema(
    const uint64_t database_id,
    const ObString &table_name,
    const bool is_index,
    const ObTableSchema *&table_schema,
    const bool with_hidden_flag/*false*/,
    const bool is_built_in_index/*false*/)
{
  int ret = OB_SUCCESS;
  const ObSimpleTableSchemaV2 *simple_table = NULL;
  table_schema = NULL;
  if (OB_FAIL(get_simple_table_schema(
                                      database_id,
                                      table_name,
                                      is_index,
                                      simple_table,
                                      with_hidden_flag,
                                      is_built_in_index))) {
  } else if (NULL == simple_table) {
    LOG_INFO("table not exist",
             K(database_id), K(table_name), K(is_index));
  } else if (OB_FAIL(get_schema(TABLE_SCHEMA,
                                simple_table->get_table_id(),
                                table_schema,
                                simple_table->get_schema_version()))) {
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", KR(ret),
             "table_id", simple_table->get_table_id());
  }
  return ret;
}

int ObSchemaGetterGuard::get_table_schema(
    const ObString &database_name,
    const ObString &table_name,
    const bool is_index,
    const ObTableSchema *&table_schema,
    const bool with_hidden_flag/*false*/,
    const bool is_built_in_index/*false*/)
{
  int ret = OB_SUCCESS;
  uint64_t database_id = OB_INVALID_ID;
  table_schema = NULL;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (database_name.empty()
             || table_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(database_name), K(table_name), KR(ret));
  } else if (OB_FAIL(get_database_id(database_name, database_id)))  {
  } else if (OB_INVALID_ID == database_id) {
    // do-nothing
  } else {
    ret = get_table_schema( database_id, table_name, is_index, table_schema, with_hidden_flag, is_built_in_index);
  }

  return ret;
}

int ObSchemaGetterGuard::get_index_schemas_with_data_table_id(const uint64_t data_table_id,
  ObIArray<const ObSimpleTableSchemaV2 *> &index_schemas)
{
  int ret = OB_SUCCESS;
  index_schemas.reset();
  const ObSchemaMgr *mgr = NULL; 
  const ObSimpleTableSchemaV2 *table_schema = NULL;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (false
            || OB_INVALID_ID == data_table_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(data_table_id));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->get_table_schema( data_table_id, table_schema))) {
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("table not exist", KR(ret), K(data_table_id));
  } else if (table_schema->is_table() || table_schema->is_tmp_table()) {
    if (OB_FAIL(mgr->get_aux_schemas( data_table_id, index_schemas, USER_INDEX))) {
    }
  } 
  return ret;
}

int ObSchemaGetterGuard::get_column_schema(
  const uint64_t table_id,
  const uint64_t column_id,
  const ObColumnSchemaV2 *&column_schema)
{
  int ret = OB_SUCCESS;
  column_schema = NULL;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == table_id
             || OB_INVALID_ID == column_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", KR(ret), K(table_id), K(column_id));
  } else if (is_cte_table(table_id)) {
    // fake table is only used in sql execution process and doesn't have schema.
    // We should avoid error in such situation.
  } else {
    const ObTableSchema *table_schema = NULL;
    if (OB_FAIL(get_table_schema( table_id, table_schema))) {
    } else if (NULL == table_schema) {
      // do-nothing
    } else {
      column_schema = table_schema->get_column_schema(column_id);
    }
  }

  return ret;
}

int ObSchemaGetterGuard::get_column_schema(
  const uint64_t table_id,
  const ObString &column_name,
  const ObColumnSchemaV2 *&column_schema)
{
  int ret = OB_SUCCESS;
  column_schema = NULL;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == table_id
             || column_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", KR(ret), K(table_id), K(column_name));
  } else if (is_cte_table(table_id)) {
    // fake table is only used in sql execution process and doesn't have schema.
    // We should avoid error in such situation.
  } else {
    const ObTableSchema *table_schema = NULL;
    if (OB_FAIL(get_table_schema( table_id, table_schema))) {
    } else if (NULL == table_schema) {
      // do-nothing
    } else {
      column_schema = table_schema->get_column_schema(column_name);
    }
  }

  return ret;
}


// for readonly
int ObSchemaGetterGuard::verify_read_only(const ObStmtNeedPrivs &stmt_need_privs)
{
  int ret = OB_SUCCESS;
  const ObStmtNeedPrivs::NeedPrivs &need_privs = stmt_need_privs.need_privs_;
  {
    for (int i = 0; OB_SUCC(ret) && i < need_privs.count(); ++i) {
      const ObNeedPriv &need_priv = need_privs.at(i);
      switch (need_priv.priv_level_) {
        case OB_PRIV_USER_LEVEL: {
          //we do not check user priv level only check table and db
          break;
        }
        case OB_PRIV_DB_LEVEL: {
          if (OB_FAIL(verify_db_read_only( need_priv))) {
          }
          break;
        }
        case OB_PRIV_TABLE_LEVEL: {
          if (OB_FAIL(verify_db_read_only( need_priv))) {
          } else if (OB_FAIL(verify_table_read_only( need_priv))) {
          }
          break;
        }
        case OB_PRIV_ROUTINE_LEVEL: {
          if (OB_FAIL(verify_db_read_only( need_priv))) {
          }
          break;
        }
        case OB_PRIV_OBJECT_LEVEL: {
          if (OB_FAIL(verify_db_read_only( need_priv))) {
          }
          break;
        }
        default:{
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unknown privilege level", K(need_priv), KR(ret));
        }
      }
    }
  }
  return ret;
}

int ObSchemaGetterGuard::verify_db_read_only(const ObNeedPriv &need_priv)
{
  int ret = OB_SUCCESS;
  const ObString &db_name = need_priv.db_;
  const ObPrivSet &priv_set = need_priv.priv_set_;
  const ObDatabaseSchema *db_schema =  NULL;
  const ObPrivSet &read_only_privs = OB_PRIV_SELECT | OB_PRIV_SHOW_VIEW | OB_PRIV_SHOW_DB |
                                     OB_PRIV_READ;
  if (OB_FAIL(get_database_schema( db_name, db_schema))) {
  } else if (NULL != db_schema) {
    if (db_schema->is_read_only() && OB_PRIV_HAS_OTHER(priv_set, read_only_privs)) {
      ret = OB_ERR_DB_READ_ONLY;
      LOG_USER_ERROR(OB_ERR_DB_READ_ONLY, db_name.length(), db_name.ptr());
      LOG_WARN("database is read only, can't not execute this statment",
               K(need_priv), KR(ret));
    }
  }
  return ret;
}

int ObSchemaGetterGuard::verify_table_read_only(const ObNeedPriv &need_priv)
{
  int ret = OB_SUCCESS;
  const ObString &db_name = need_priv.db_;
  const ObString &table_name = need_priv.table_;
  const ObPrivSet &priv_set = need_priv.priv_set_;
  const ObTableSchema *table_schema = NULL;
  const ObPrivSet &read_only_privs = OB_PRIV_SELECT | OB_PRIV_SHOW_VIEW | OB_PRIV_SHOW_DB |
                                     OB_PRIV_READ;
  // FIXME: is it right?
  const bool is_index = false;
  if (OB_FAIL(get_table_schema( db_name, table_name, is_index, table_schema))) {
  } else if (NULL != table_schema) {
    if (table_schema->is_read_only() && OB_PRIV_HAS_OTHER(priv_set, read_only_privs)) {
      ret = OB_ERR_TABLE_READ_ONLY;
      LOG_USER_ERROR(OB_ERR_TABLE_READ_ONLY, db_name.length(), db_name.ptr(),
                     table_name.length(), table_name.ptr());
      LOG_WARN("table is read only, can't not execute this statment",
               K(need_priv), KR(ret));
    }
  }
  return ret;
}

int ObSchemaGetterGuard::add_role_id_recursively(uint64_t role_id,
  ObSessionPrivInfo &s_priv,
  common::ObIArray<uint64_t> &enable_role_id_array)
{
  int ret = OB_SUCCESS;
  const ObUserInfo *role_info = NULL;

  if (!has_exist_in_array(enable_role_id_array, role_id)) {
    /* 1. put itself */
    OZ (enable_role_id_array.push_back(role_id));
    /* 2. get role recursively */
    OZ (get_user_info(role_id, role_info));
    if (OB_SUCC(ret) && role_info != NULL) {
      const ObSEArray<uint64_t, 8> &role_id_array = role_info->get_role_id_array();
      for (int i = 0; OB_SUCC(ret) && i < role_id_array.count(); ++i) {
        OZ (add_role_id_recursively(role_info->get_role_id_array().at(i), s_priv, enable_role_id_array));
      }
    }
  }
  return ret;
}

int ObSchemaGetterGuard::check_activate_all_role_var(bool &activate_all_role) {
  int ret = OB_SUCCESS;
  const ObSysVarSchema *session_var = NULL;
  ObObj session_obj;
  ObArenaAllocator alloc(ObModIds::OB_TEMP_VARIABLES);
  activate_all_role = false;
  if (OB_FAIL(get_system_variable(SYS_VAR_ACTIVATE_ALL_ROLES_ON_LOGIN,
                                         session_var))) {
  } else if (OB_ISNULL(session_var)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to get charset_var or collation_var", K(ret));
  } else if (OB_FAIL(session_var->get_value(&alloc, NULL, session_obj))) {
  } else {
    activate_all_role = !!(session_obj.get_int());
  }
  return ret;
}

int ObSchemaGetterGuard::is_user_empty_passwd(const ObUserLoginInfo &login_info, bool &is_empty_passwd_account) {
  int ret = OB_SUCCESS;
  is_empty_passwd_account = false;
  {
    const int64_t DEFAULT_SAME_USERNAME_COUNT = 4;
    ObSEArray<const ObUserInfo *, DEFAULT_SAME_USERNAME_COUNT> users_info;
    if (OB_FAIL(get_user_info(login_info.user_name_, users_info))) {
    } else if (users_info.empty()) {
      ret = OB_PASSWORD_WRONG;
      LOG_WARN("no matching runtime user", K(login_info), KR(ret));
    } else {
      const ObUserInfo *user_info = NULL;
      const ObUserInfo *matched_user_info = NULL;
      for (int64_t i = 0; i < users_info.count() && OB_SUCC(ret); ++i) {
        user_info = users_info.at(i);
        if (NULL == user_info) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("user info is null", K(login_info), KR(ret));
        } else if (!obsys::ObNetUtil::is_match(login_info.client_ip_, user_info->get_host_name_str())) {
        } else {
          matched_user_info = user_info;
          if (0 == login_info.passwd_.length() && 0 == user_info->get_passwd_str().length()) {
            is_empty_passwd_account = true;
            break;
          }
        }
      }
    }
  }
  return ret;
}

// for privilege
int ObSchemaGetterGuard::check_user_access(
    const ObUserLoginInfo &login_info,
    ObSessionPrivInfo &s_priv,
    common::ObIArray<uint64_t> &enable_role_id_array,
    const common::ObSqlTlsInfo *tls_info,
    const ObUserInfo *&sel_user_info)
{
  int ret = OB_SUCCESS;
  sel_user_info = NULL;
  {
    const int64_t DEFAULT_SAME_USERNAME_COUNT = 4;
    ObSEArray<const ObUserInfo *, DEFAULT_SAME_USERNAME_COUNT> users_info;
    if (OB_FAIL(get_user_info(login_info.user_name_, users_info))) {
    } else if (users_info.empty()) {
      ret = OB_PASSWORD_WRONG;
      LOG_WARN("no matching runtime user", K(login_info), KR(ret));
    } else {
      bool is_found = false;
      const ObUserInfo *user_info = NULL;
      const ObUserInfo *matched_user_info = NULL;
      for (int64_t i = 0; i < users_info.count() && OB_SUCC(ret) && !is_found; ++i) {
        user_info = users_info.at(i);
        if (NULL == user_info) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("user info is null", K(login_info), KR(ret));
        } else if (!obsys::ObNetUtil::is_match(login_info.client_ip_, user_info->get_host_name_str())) {
        } else {
          matched_user_info = user_info;
          if (0 == login_info.passwd_.length() && 0 == user_info->get_passwd_str().length()) {
            //passed
            is_found = true;
          } else if (0 == login_info.passwd_.length() || 0 == user_info->get_passwd_str().length()) {
            ret = OB_PASSWORD_WRONG;
            LOG_WARN("password error", KR(ret), K(login_info.passwd_.length()),
                     K(user_info->get_passwd_str().length()));
          } else {
            char stored_stage2_hex[SCRAMBLE_LENGTH] = {0};
            ObString stored_stage2_trimed;
            ObString stored_stage2_hex_str;
            if (user_info->get_passwd_str().length() < SCRAMBLE_LENGTH *2 + 1) {
              ret = OB_NOT_IMPLEMENT;
              LOG_WARN("Currently hash method other than MySQL 4.1 hash is not implemented.",
                       "hash str length", user_info->get_passwd_str().length());
            } else {
              //trim the leading '*'
              stored_stage2_trimed.assign_ptr(user_info->get_passwd_str().ptr() + 1,
                                              user_info->get_passwd_str().length() - 1);
              stored_stage2_hex_str.assign_buffer(stored_stage2_hex, SCRAMBLE_LENGTH);
              stored_stage2_hex_str.set_length(SCRAMBLE_LENGTH);
              //first, we restore the stored, displayable stage2 hash to its hex form
              ObEncryptedHelper::displayable_to_hex(stored_stage2_trimed, stored_stage2_hex_str);
              //then, we call the mysql validation logic.
              if (OB_FAIL(ObEncryptedHelper::check_login(login_info.passwd_,
                                                         login_info.scramble_str_,
                                                         stored_stage2_hex_str,
                                                         is_found))) {
              } else if (!is_found) {
                LOG_INFO("password error", "runtime_name", login_info.runtime_name_,
                         "user_name", login_info.user_name_,
                         "client_ip", login_info.client_ip_,
                         "host_name", user_info->get_host_name_str());
              } else {
                //found it
              }
            }
          }
        }
      }
      if (OB_SUCC(ret)) {
        if (matched_user_info != NULL
            && matched_user_info->get_is_locked()) {
          if (is_found) {
            s_priv.user_id_ = matched_user_info->get_user_id();
          }
          ret = OB_ERR_USER_IS_LOCKED;
          LOG_WARN("User is locked", KR(ret));
        } else if (!is_found) {
          user_info = NULL;
          ret = OB_PASSWORD_WRONG;
          LOG_INFO("password error", "runtime_name", login_info.runtime_name_,
                   "user_name", login_info.user_name_,
                   "client_ip_", login_info.client_ip_, KR(ret));
        } else if (OB_FAIL(check_ssl_access(*user_info, tls_info))) {
        } else if (OB_FAIL(check_ssl_invited_cn(tls_info))) {
        }
      }

      if (OB_SUCC(ret)) {
        
        s_priv.user_id_ = user_info->get_user_id();
        s_priv.user_name_ = user_info->get_user_name_str();
        s_priv.host_name_ = user_info->get_host_name_str();
        s_priv.user_priv_set_ = user_info->get_priv_set();
        s_priv.db_ = login_info.db_;
        sel_user_info = user_info;
        // load role priv
        if (OB_SUCC(ret)) {
          const ObSEArray<uint64_t, 8> &role_id_array = user_info->get_role_id_array();
          bool activate_all_role = false;
          CK (user_info->get_role_id_array().count() ==
              user_info->get_role_id_option_array().count());

          if (OB_SUCC(ret) && OB_FAIL(check_activate_all_role_var(activate_all_role))) {
            LOG_WARN("fail to check activate all role", K(ret));
          }
          
          for (int i = 0; OB_SUCC(ret) && i < role_id_array.count(); ++i) {
            const ObUserInfo *role_info = NULL;
            if (OB_FAIL(get_user_info(role_id_array.at(i), role_info))) {
            } else if (NULL == role_info) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("role info is null", KR(ret), K(role_id_array.at(i)));
            } else {
              if (activate_all_role
                  || user_info->get_disable_option(user_info->get_role_id_option_array().at(i)) == 0) {
                OZ (enable_role_id_array.push_back(role_id_array.at(i)));
              }
            }
          }
        }

        //check db access and db existence
        if (!login_info.db_.empty()
            && OB_FAIL(check_db_access(s_priv, enable_role_id_array, login_info.db_, s_priv.db_priv_set_))) {
          LOG_WARN("Database access deined", K(login_info), KR(ret));
        } else { }
      }
    }
  }
  return ret;
}

namespace
{

common::ObString tls_string(const char *data, const int64_t len)
{
  if (NULL == data || len <= 0) {
    return common::ObString();
  } else {
    return common::ObString(len, data);
  }
}

bool contains_string(const common::ObString &haystack, const common::ObString &needle)
{
  bool found = false;
  if (needle.empty()) {
    found = true;
  } else if (!haystack.empty() && needle.length() <= haystack.length()) {
    for (int64_t i = 0; !found && i <= haystack.length() - needle.length(); ++i) {
      found = (0 == MEMCMP(haystack.ptr() + i, needle.ptr(), needle.length()));
    }
  }
  return found;
}

} // namespace

int ObSchemaGetterGuard::check_ssl_access(
    const ObUserInfo &user_info, const common::ObSqlTlsInfo *tls_info)
{
  int ret = OB_SUCCESS;
  switch (user_info.get_ssl_type()) {
    case ObSSLType::SSL_TYPE_NOT_SPECIFIED:
    case ObSSLType::SSL_TYPE_NONE: {
      //do nothing
      break;
    }
    case ObSSLType::SSL_TYPE_ANY: {
      if (NULL == tls_info || !tls_info->tls_active_) {
        ret = OB_PASSWORD_WRONG;
        LOG_WARN("not use ssl", KR(ret));
      }
      break;
    }
    case ObSSLType::SSL_TYPE_X509: {
      if (NULL == tls_info || !tls_info->tls_active_
          || !tls_info->peer_cert_present_ || !tls_info->peer_cert_verified_) {
        ret = OB_PASSWORD_WRONG;
        LOG_WARN("X509 check failed", KP(tls_info), KR(ret));
      }
      break;
    }
    case ObSSLType::SSL_TYPE_SPECIFIED: {
      const common::ObString cipher_name = NULL == tls_info
          ? common::ObString()
          : tls_string(tls_info->cipher_name_, tls_info->cipher_name_len_);
      const common::ObString x509_issuer = NULL == tls_info
          ? common::ObString()
          : tls_string(tls_info->peer_cert_issuer_, tls_info->peer_cert_issuer_len_);
      const common::ObString x509_subject = NULL == tls_info
          ? common::ObString()
          : tls_string(tls_info->peer_cert_subject_, tls_info->peer_cert_subject_len_);
      if (NULL == tls_info || !tls_info->tls_active_
          || !tls_info->peer_cert_present_ || !tls_info->peer_cert_verified_) {
        ret = OB_PASSWORD_WRONG;
        LOG_WARN("X509 check failed", KP(tls_info), KR(ret));
      }

      if (OB_SUCC(ret)
          && !user_info.get_ssl_cipher_str().empty()
          && user_info.get_ssl_cipher_str().compare(cipher_name) != 0) {
        ret = OB_PASSWORD_WRONG;
        LOG_WARN("X509 cipher check failed", "expect", user_info.get_ssl_cipher_str(),
                 "receive", cipher_name, KR(ret));
      }

      if (OB_SUCC(ret) && !user_info.get_x509_issuer_str().empty()) {
        if (!tls_info->peer_cert_info_valid_
            || user_info.get_x509_issuer_str().compare(x509_issuer) != 0) {
          ret = OB_PASSWORD_WRONG;
          LOG_WARN("x509 issue check failed", "expect", user_info.get_x509_issuer_str(),
                   "receive", x509_issuer, KR(ret));
        }
      }

      if (OB_SUCC(ret) && !user_info.get_x509_subject_str().empty()) {
        if (!tls_info->peer_cert_info_valid_
            || user_info.get_x509_subject_str().compare(x509_subject) != 0) {
          ret = OB_PASSWORD_WRONG;
          LOG_WARN("x509 subject check failed", "expect", user_info.get_x509_subject_str(),
                   "receive", x509_subject, KR(ret));
        }
      }
      break;
    }
    default: {
      ret = OB_PASSWORD_WRONG;
      LOG_WARN("unknonw type", K(user_info), KR(ret));
      break;
    }
  }

  if (OB_FAIL(ret)) {
  }
  return ret;
}


int ObSchemaGetterGuard::check_ssl_invited_cn(
    const common::ObSqlTlsInfo *tls_info)
{
  int ret = OB_SUCCESS;
  if (NULL == tls_info || !tls_info->tls_active_) {
  } else {
    ObString ob_ssl_invited_common_names(GCONF.ob_ssl_invited_common_names.str());
    if (ob_ssl_invited_common_names.empty()) {
      ret = OB_PASSWORD_WRONG;
      LOG_WARN("ob_ssl_invited_common_names not match", "expect", ob_ssl_invited_common_names, KR(ret));
    } else if (!tls_info->peer_cert_present_) {
      // Keep the historical behavior for a TLS connection without a client
      // certificate: the CN allowlist only constrains presented certificates.
    } else if (!tls_info->peer_cert_verified_ || !tls_info->peer_cert_info_valid_) {
      ret = OB_PASSWORD_WRONG;
      LOG_WARN("X509 check failed", KR(ret));
    } else {
      const common::ObString cn_used = tls_string(
          tls_info->peer_cert_common_name_, tls_info->peer_cert_common_name_len_);
      if (cn_used.empty()) {
        ret = OB_PASSWORD_WRONG;
        LOG_WARN("failed to found cn", KR(ret));
      } else if (!contains_string(ob_ssl_invited_common_names, cn_used)) {
        ret = OB_PASSWORD_WRONG;
        LOG_WARN("ob_ssl_invited_common_names not match", "expect", ob_ssl_invited_common_names,
                 "curr", cn_used, KR(ret));
      } else {
      }
    }
  }
  return ret;
}


int ObSchemaGetterGuard::check_db_access(ObSessionPrivInfo &s_priv,
                                         const common::ObIArray<uint64_t> &enable_role_id_array,
                                         const ObString& database_name)
{
  int ret = OB_SUCCESS;

  uint64_t database_id = OB_INVALID_ID;
  
  if (OB_FAIL(get_database_id(database_name, database_id))) {
  } else if (OB_INVALID_ID != database_id) {
    if (OB_FAIL(check_db_access(s_priv, enable_role_id_array, database_name, s_priv.db_priv_set_))) {
    }
  } else {
    ret = OB_ERR_BAD_DATABASE;
    OB_LOG(WARN, "database not exist", KR(ret), K(database_name), K(s_priv));
    LOG_USER_ERROR(OB_ERR_BAD_DATABASE, database_name.length(), database_name.ptr());
  }
  return ret;
}

int ObSchemaGetterGuard::get_session_priv_info(
                                               const uint64_t user_id,
                                               const ObString &database_name,
                                               ObSessionPrivInfo &session_priv)
{
  int ret = OB_SUCCESS;
  const ObUserInfo *user_info = NULL;
  if (OB_FAIL(get_user_info(user_id,
                            user_info))) {
  } else if (NULL == user_info) {
    ret = OB_USER_NOT_EXIST;
    LOG_WARN("user info is null", KR(ret), K(user_id));
  } else {
    const ObSchemaMgr *mgr = NULL;
    ObOriginalDBKey db_priv_key(user_info->get_user_id(),
                                database_name);
    ObPrivSet db_priv_set = OB_PRIV_SET_EMPTY;
    if (OB_FAIL(check_lazy_guard( mgr))) {
    } else if (OB_FAIL(mgr->priv_mgr_.get_db_priv_set(db_priv_key, db_priv_set))) {
    } else {
      
      session_priv.user_id_ = user_info->get_user_id();
      session_priv.user_name_ = user_info->get_user_name_str();
      session_priv.host_name_ = user_info->get_host_name_str();
      session_priv.db_ = database_name;
      session_priv.user_priv_set_ = user_info->get_priv_set();
      session_priv.db_priv_set_ = db_priv_set;
    }
  }
  return ret;
}

//If column or table or db or user not existed, or correspanding column priv is not existed
//Then priv_id will return OB_INVALID_ID.
int ObSchemaGetterGuard::get_column_priv_id(const uint64_t user_id,
    const ObString &db,
    const ObString &table,
    const ObString &column,
    uint64_t &priv_id)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  priv_id = OB_INVALID_ID;
  if (0 == db.length() || 0 == table.length() || 0 == column.length() 
      || OB_INVALID_ID == user_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid arguments", KR(ret));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else {
    const ObPrivMgr &priv_mgr = mgr->priv_mgr_;
    if (OB_FAIL(priv_mgr.get_column_priv_id(user_id, db, table, column, priv_id))) {
    }
  }
  return ret;
}

int ObSchemaGetterGuard::check_db_access(
    const ObSessionPrivInfo &session_priv,
    const common::ObIArray<uint64_t> &enable_role_id_array,
    const ObString &db,
    ObPrivSet &db_priv_set,
    bool print_warn)
{
  int ret = OB_SUCCESS;
  
  const ObSchemaMgr *mgr = NULL;

  if (!session_priv.is_valid() || 0 == db.length()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid arguments", K(session_priv), KR(ret));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else {
    const ObPrivMgr &priv_mgr = mgr->priv_mgr_;
    ObOriginalDBKey db_priv_key(session_priv.user_id_,
                                db);
    db_priv_set = OB_PRIV_SET_EMPTY;
    if (OB_FAIL(priv_mgr.get_db_priv_set(db_priv_key, db_priv_set))) {
    } else {
      bool is_grant = false;
      bool is_grant_table = false;
      bool is_grant_routine = false;
      ObSEArray<const ObColumnPriv *, 4> column_privs;
      if (OB_FAIL(priv_mgr.table_grant_in_db(db_priv_key.user_id_,
                                            db_priv_key.db_,
                                            is_grant_table))) {
      } else if (OB_FAIL(priv_mgr.routine_grant_in_db(
                                            db_priv_key.user_id_,
                                            db_priv_key.db_,
                                            is_grant_routine))) {
      } else if (is_grant) {
      } else if (OB_FAIL(priv_mgr.get_column_priv_in_db(
                                            db_priv_key.user_id_,
                                            db_priv_key.db_,
                                            column_privs))) {
      } else if (!column_privs.empty()) {
        is_grant = true;
      } else {
        is_grant = (is_grant_table || is_grant_routine);
        // load db level prvilege from roles
        const ObUserInfo *user_info = NULL;
        if (OB_FAIL(get_user_info(session_priv.user_id_, user_info))) {
        } else if (NULL == user_info) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("user info is null", KR(ret), K(session_priv.user_id_));
        } else {
          bool is_grant_role = false;
          ObPrivSet total_db_priv_set_role = OB_PRIV_SET_EMPTY;
          ObArray<uint64_t> role_id_array;

          if (OB_FAIL(role_id_array.assign(enable_role_id_array))) {
          }
          for (int i = 0; OB_SUCC(ret) && i < role_id_array.count(); ++i) {
            const ObUserInfo *role_info = NULL;
            if (OB_FAIL(get_user_info(role_id_array.at(i), role_info))) {
            } else if (NULL == role_info) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("role info is null", KR(ret), K(role_id_array.at(i)));
            } else {
              ObPrivSet db_priv_set_role = OB_PRIV_SET_EMPTY;
              ObOriginalDBKey db_priv_key_role(role_info->get_user_id(),
                  db);
              if (OB_FAIL(priv_mgr.get_db_priv_set(db_priv_key_role, db_priv_set_role))) {
              } else if (!is_grant_role && OB_FAIL(priv_mgr.table_grant_in_db(db_priv_key_role.user_id_,
                        db_priv_key_role.db_,
                        is_grant_role))) {
                LOG_WARN("check table grant in db failed", K(db_priv_key_role), KR(ret));
              } else {
                // append db level privilege
                total_db_priv_set_role |= db_priv_set_role;
              }
              if (OB_SUCC(ret)) {
                column_privs.reuse();
                if (!is_grant_role && OB_FAIL(priv_mgr.get_column_priv_in_db(
                                                                            db_priv_key_role.user_id_,
                                                                            db_priv_key_role.db_,
                                                                            column_privs))) {
                  LOG_WARN("check column grant in db failed", K(db_priv_key), KR(ret));
                } else if (!column_privs.empty()) {
                  is_grant_role = true;
                }
                if (OB_SUCC(ret) && !is_grant_role) {
                  is_grant_role = !!(role_info->get_priv_set() & OB_PRIV_DB_ACC);
                }
                if (OB_SUCC(ret) && !is_grant_role
                    && !((session_priv.user_priv_set_ | db_priv_set | total_db_priv_set_role) & OB_PRIV_DB_ACC)) {
                  //continue for roles recursively
                  if (OB_FAIL(common::append(role_id_array, role_info->get_role_id_array()))) {
                  }
                }
              }
            }
          }
          if (OB_SUCC(ret)) {
              // append db privilege from all roles
              db_priv_set |= total_db_priv_set_role;
              is_grant = is_grant || is_grant_role;
          }
        }
      }
      if (OB_FAIL(ret)) {
      } else if (((session_priv.user_priv_set_ | db_priv_set) & OB_PRIV_DB_ACC)
          || is_grant) {
      } else {
        ret = OB_ERR_NO_DB_PRIVILEGE;
        if (print_warn) {
          LOG_WARN("No privilege to access database", K(session_priv), K(db), KR(ret));
          LOG_USER_ERROR(OB_ERR_NO_DB_PRIVILEGE, session_priv.user_name_.length(), session_priv.user_name_.ptr(),
                        session_priv.host_name_.length(), session_priv.host_name_.ptr(),
                        db.length(), db.ptr());
        }
      }
    }
  }
  return ret;
}

int ObSchemaGetterGuard::get_db_priv_set(const uint64_t user_id,
                                         const ObString &db,
                                         ObPrivSet &priv_set)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->priv_mgr_.get_db_priv_set(
                     ObOriginalDBKey(user_id, db), priv_set))) {
  }
  return ret;
}

int ObSchemaGetterGuard::get_db_priv_set(const ObOriginalDBKey &db_priv_key, ObPrivSet &priv_set, bool is_pattern)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  
  if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->priv_mgr_.get_db_priv_set(db_priv_key, priv_set, is_pattern))) {
  }
  return ret;
}

int ObSchemaGetterGuard::get_table_priv_set(const ObTablePrivSortKey &table_priv_key,
        ObPrivSet &priv_set)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  
  if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->priv_mgr_.get_table_priv_set(table_priv_key, priv_set))) {
  }
  return ret;
}

int ObSchemaGetterGuard::get_routine_priv_set(const ObRoutinePrivSortKey &routine_priv_key,
        ObPrivSet &priv_set)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  
  if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->priv_mgr_.get_routine_priv_set(routine_priv_key, priv_set))) {
  }
  return ret;
}

int ObSchemaGetterGuard::get_column_priv(const ObColumnPrivSortKey &column_priv_key,
        const ObColumnPriv *&column_priv)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  column_priv = NULL;
  
  if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->priv_mgr_.get_column_priv(column_priv_key, column_priv))) {
  }
  return ret;
}

int ObSchemaGetterGuard::get_column_priv_set(const ObColumnPrivSortKey &column_priv_key,
        ObPrivSet &priv_set)
{
  int ret = OB_SUCCESS;
  priv_set = 0;
  const ObSchemaMgr *mgr = NULL;
  
  if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->priv_mgr_.get_column_priv_set(column_priv_key, priv_set))) {
  }
  return ret;
}

int ObSchemaGetterGuard::get_obj_privs(
    const ObObjPrivSortKey &obj_priv_key,
    ObPackedObjPriv &obj_privs)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  
  const ObObjPriv *obj_priv = NULL;
  obj_privs = 0;
  if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->priv_mgr_.get_obj_priv(obj_priv_key, obj_priv))) {
  } else if (obj_priv != NULL) {
    obj_privs = obj_priv->get_obj_privs();
  }
  return ret;
}

int ObSchemaGetterGuard::get_user_infos_by_id(common::ObIArray<const ObUserInfo *> &user_infos)
{
  int ret = OB_SUCCESS;
  user_infos.reset();

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_FAIL(get_user_schemas_in_runtime(user_infos))) {
  }
  return ret;
}

int ObSchemaGetterGuard::get_db_priv_by_id(ObIArray<const ObDBPriv *> &db_privs)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  db_privs.reset();

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->priv_mgr_.get_db_privs_in_runtime(db_privs))) {
  }

  return ret;
}

int ObSchemaGetterGuard::get_column_priv_in_table(const ObTablePrivSortKey &table_priv_key,
                              ObIArray<const ObColumnPriv *> &column_privs)
{
  return get_column_priv_in_table(table_priv_key.user_id_,
                                  table_priv_key.db_, table_priv_key.table_, column_privs);
}

int ObSchemaGetterGuard::get_column_priv_in_table(const uint64_t user_id,
                                                  const ObString &db,
                                                  const ObString &table,
                                                  ObIArray<const ObColumnPriv *> &column_privs)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  column_privs.reset();

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->priv_mgr_.get_column_priv_in_table(user_id, db, table, column_privs))) {
  }

  return ret;
}

int ObSchemaGetterGuard::get_db_priv_with_user_id(const uint64_t user_id,
                                                  ObIArray<const ObDBPriv *> &db_privs)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  db_privs.reset();

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == user_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(user_id));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->priv_mgr_.get_db_privs_in_user(user_id, db_privs))) {
  }

  return ret;
}

// System-table privileges are evaluated through the server runtime privilege set.
int ObSchemaGetterGuard::get_table_priv_by_id(ObIArray<const ObTablePriv *> &table_privs)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  table_privs.reset();

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->priv_mgr_.get_table_privs_in_runtime(table_privs))) {
  }

  return ret;
}

// System-table privileges are evaluated through the server runtime privilege set.
int ObSchemaGetterGuard::get_table_priv_with_user_id(const uint64_t user_id,
                                                     ObIArray<const ObTablePriv *> &table_privs)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  table_privs.reset();

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == user_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(user_id));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->priv_mgr_.get_table_privs_in_user(user_id, table_privs))) {
  }

  return ret;
}

int ObSchemaGetterGuard::get_routine_priv_with_user_id(const uint64_t user_id,
                                                      ObIArray<const ObRoutinePriv *> &routine_privs)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  routine_privs.reset();

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == user_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(user_id));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->priv_mgr_.get_routine_privs_in_user( user_id, routine_privs))) {
  }

  return ret;
}

int ObSchemaGetterGuard::get_column_priv_with_user_id(const uint64_t user_id,
                                                      ObIArray<const ObColumnPriv *> &column_privs)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  column_privs.reset();

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == user_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(user_id));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->priv_mgr_.get_column_privs_in_user( user_id, column_privs))) {
  }

  return ret;
}

int ObSchemaGetterGuard::get_obj_priv_with_grantee_id(const uint64_t grantee_id,
    ObIArray<const ObObjPriv *> &obj_privs)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  obj_privs.reset();

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == grantee_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(grantee_id));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->priv_mgr_.get_obj_privs_in_grantee(grantee_id, obj_privs))) {
  }

  return ret;
}

int ObSchemaGetterGuard::get_obj_priv_with_grantor_id(const uint64_t grantor_id,
    ObIArray<const ObObjPriv *> &obj_privs,
    bool reset_flag)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  if (reset_flag) {
    obj_privs.reset();
  }

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == grantor_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(grantor_id));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->priv_mgr_.get_obj_privs_in_grantor(grantor_id,
                     obj_privs, reset_flag))) {
  }

  return ret;
}

int ObSchemaGetterGuard::get_obj_priv_with_obj_id(
    const uint64_t obj_id,
    const uint64_t obj_type,
    ObIArray<const ObObjPriv *> &obj_privs,
    bool reset_flag)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  if (reset_flag) {
    obj_privs.reset();
  }

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == obj_id
             || OB_INVALID_ID == obj_type) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(obj_id), K(obj_type));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->priv_mgr_.get_obj_privs_in_obj(obj_id, obj_type,
                     obj_privs, reset_flag))) {
  }

  return ret;
}


int ObSchemaGetterGuard::get_obj_privs_in_grantor_ur_obj_id(const ObObjPrivSortKey &obj_key,
    common::ObIArray<const ObObjPriv *> &obj_privs)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (!obj_key.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(obj_key));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->priv_mgr_.get_obj_privs_in_grantor_ur_obj_id(obj_key, obj_privs))) {
  }

  return ret;
}

int ObSchemaGetterGuard::get_obj_privs_in_grantor_obj_id(const ObObjPrivSortKey &obj_key,
    common::ObIArray<const ObObjPriv *> &obj_privs)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (!obj_key.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(obj_key));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->priv_mgr_.get_obj_privs_in_grantor_obj_id(obj_key, obj_privs))) {
  }

  return ret;
}

inline bool ObSchemaGetterGuard::check_inner_stat() const
{
  bool ret = true;
  if (!is_inited_) {
    ret = false;
    LOG_WARN("schema guard not inited", KR(ret));
  } else if (NULL == schema_service_
      || INVALID_SCHEMA_GUARD_TYPE == schema_guard_type_) {
    ret = false;
    LOG_WARN("invalid inner stat", K(schema_service_), K_(schema_guard_type));
  }
  return ret;
}

// OB_INVALID_VERSION means schema doesn't exist.
// bugfix: 
int ObSchemaGetterGuard::get_schema_version(
    const ObSchemaType schema_type,
    const uint64_t schema_id,
    int64_t &schema_version,
    uint64_t *schema_belong_db_id)
{
  int ret = OB_SUCCESS;
  schema_version = OB_INVALID_VERSION;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (!is_normal_schema(schema_type)
             || OB_INVALID_ID == schema_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(schema_type), K(schema_id));
  } else {
#define GET_TABLE_SCHEMA_VERSION_DIRECT() \
      const ObSimpleTableSchemaV2 *schema = NULL;             \
      const ObSchemaMgr *mgr = NULL; \
      if (OB_FAIL(check_lazy_guard( mgr))) { \
        LOG_WARN("fail to check lazy guard", KR(ret)); \
      } else if (OB_FAIL(mgr->get_table_schema(schema_id, schema))) {       \
        LOG_WARN("get table schema failed", KR(ret), K(schema_id));     \
      } else if (OB_NOT_NULL(schema)) {                                         \
        schema_version = schema->get_schema_version();                     \
      }
#define GET_SCHEMA_VERSION_NT(SCHEMA, SCHEMA_TYPE) \
      const SCHEMA_TYPE *schema = NULL;             \
      const ObSchemaMgr *mgr = NULL; \
      if (OB_FAIL(check_lazy_guard( mgr))) { \
        LOG_WARN("fail to check lazy guard", KR(ret)); \
      } else if (OB_FAIL(mgr->get_##SCHEMA##_schema(schema_id, schema))) {       \
        LOG_WARN("get "#SCHEMA" schema failed", KR(ret), K(schema_id));     \
      } else if (OB_NOT_NULL(schema)) {                                         \
        schema_version = schema->get_schema_version();                     \
      }
#define GET_DATABASE_ID()  \
      if (OB_SUCC(ret) && OB_NOT_NULL(schema_belong_db_id) && OB_NOT_NULL(schema)) {  \
        *schema_belong_db_id = schema->get_database_id();   \
      }
    switch (schema_type) {
    case SERVER_RUNTIME_SCHEMA : {
        const ObSimpleServerRuntimeSchema *schema = NULL;
        const ObSchemaMgr *mgr = NULL;
        if (OB_FAIL(check_lazy_guard( mgr))) {
        } else if (OB_FAIL(mgr->get_server_runtime_schema(schema))) {
        } else if (OB_NOT_NULL(schema)) {
          schema_version = schema->get_schema_version();
        }
        break;
      }
    case USER_SCHEMA : {
        GET_SCHEMA_VERSION_NT(user, ObSimpleUserSchema);
        break;
      }
    case DATABASE_SCHEMA : {
        GET_SCHEMA_VERSION_NT(database, ObSimpleDatabaseSchema);
        break;
      }
    case TABLE_SCHEMA : {
        if (is_cte_table(schema_id)) {
          // fake table, we should avoid error in such situation.
          schema_version = OB_INVALID_VERSION;
        } else {
          GET_TABLE_SCHEMA_VERSION_DIRECT();
          GET_DATABASE_ID();
        }
        break;
      }
    case PACKAGE_SCHEMA : {
        if (ObTriggerInfo::is_trigger_package_id(schema_id)) {
          const ObSimpleTriggerSchema *schema = NULL;
          const ObSchemaMgr *mgr = NULL;
          const uint64_t trigger_id = ObTriggerInfo::get_package_trigger_id(schema_id);
          if (OB_FAIL(check_lazy_guard( mgr))) {
          } else if (OB_FAIL(mgr->get_trigger_schema( trigger_id, schema))) {
          } else if (OB_NOT_NULL(schema)) {
            schema_version = schema->get_schema_version();
            GET_DATABASE_ID();
          }
        } else {
          GET_SCHEMA_VERSION_NT(package, ObSimplePackageSchema);
          GET_DATABASE_ID();
        }
        break;
      }
    case ROUTINE_SCHEMA : {
        GET_SCHEMA_VERSION_NT(routine, ObSimpleRoutineSchema);
        GET_DATABASE_ID();
        break;
      }
    case SYS_VARIABLE_SCHEMA : {
        const ObSimpleSysVariableSchema *schema = NULL;
        const ObSchemaMgr *mgr = NULL;
        
        if (1UL != schema_id) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("id is not match with schema_id", KR(ret), K(schema_id));
        } else if (OB_FAIL(check_lazy_guard( mgr))) {
        } else if (OB_FAIL(mgr->sys_variable_mgr_.get_sys_variable_schema(schema))) {
        } else if (OB_NOT_NULL(schema)) {
          schema_version = schema->get_schema_version();
          LOG_TRACE("get sys variable schema", KR(ret), K(schema_id), K(*schema),
                    "snapshot_version", mgr->get_schema_version());
        }
        break;
      }
    case TRIGGER_SCHEMA: {
      GET_SCHEMA_VERSION_NT(trigger, ObSimpleTriggerSchema);
      GET_DATABASE_ID();
      break;
    }
    case MOCK_FK_PARENT_TABLE_SCHEMA : {
        const ObSimpleMockFKParentTableSchema *schema = NULL;
        const ObSchemaMgr *mgr = NULL;
        if (OB_FAIL(check_lazy_guard( mgr))) {
        } else if (OB_FAIL(mgr->mock_fk_parent_table_mgr_.get_mock_fk_parent_table_schema(schema_id, schema))) {
        } else if (OB_NOT_NULL(schema)) {
          schema_version = schema->get_schema_version();
        }
        break;
      }
    default : {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("should not reach here", KR(ret));
        break;
      }
    }
#undef GET_TABLE_SCHEMA_VERSION_DIRECT
#undef GET_SCHEMA_VERSION_NT
#undef GET_DATABASE_ID
  }
  return ret;
}

template<typename T>
int ObSchemaGetterGuard::get_from_local_cache(
    const ObSchemaType schema_type,
    const uint64_t schema_id,
    const T *&schema)
{
  int ret = OB_SUCCESS;
  schema = NULL;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == schema_id
             || !is_normal_schema(schema_type)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(schema_id), K(schema_type));
  } else {
    const ObSchema *tmp_schema = NULL;
    bool found = false;
    FOREACH_CNT_X(id_schema, schema_objs_, !found) {
      if (id_schema->schema_type_ == schema_type
          && id_schema->schema_id_ == schema_id) {
        tmp_schema = id_schema->schema_;
        found = true;
      }
    }
    if (!found) {
      ret = OB_ENTRY_NOT_EXIST;
    } else if (OB_ISNULL(tmp_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tmp schema is NULL", KR(ret), K(schema_type), K(schema_id));
    } else {
      schema = static_cast<const T *>(tmp_schema);
    }
  }

  return ret;
}

template<typename T>
int ObSchemaGetterGuard::put_to_local_cache(
    const ObSchemaType schema_type,
    const uint64_t schema_id,
    const T *&schema,
    ObKVCacheHandle &handle)
{
  int ret = OB_SUCCESS;
  SchemaObj schema_obj_tmp; // just for array push back
  if (OB_FAIL(schema_objs_.push_back(schema_obj_tmp))) {
  } else {
    SchemaObj &schema_obj = schema_objs_.at(schema_objs_.count() - 1);
    schema_obj.schema_type_ = schema_type;
    
    schema_obj.schema_id_ = schema_id;
    schema_obj.schema_ = const_cast<ObSchema*>(schema);
    schema_obj.handle_.move_from(handle);
    if (schema_obj.handle_.is_valid()
        && OB_NOT_NULL(schema)
        && pin_cache_size_ < FULL_SCHEMA_MEM_THREHOLD) {
        pin_cache_size_ += schema->get_convert_size();
      if (pin_cache_size_ >= FULL_SCHEMA_MEM_THREHOLD) {
        FLOG_WARN("hold too much full schema memory", K(pin_cache_size_), K(lbt()));
      }
    }
  }
  return ret;
}


template<typename T>
int ObSchemaGetterGuard::get_schema(
    const ObSchemaType schema_type,
    const uint64_t schema_id,
    const T *&schema,
    int64_t specified_version /*=OB_INVALID_VERSION*/)
{
  int ret = OB_SUCCESS;
  int64_t schema_version = OB_INVALID_VERSION;
  const ObSchemaMgr *mgr = NULL;
  const ObSchema *base_schema = NULL;
  ObKVCacheHandle handle;
  ObRefreshSchemaStatus schema_status;
  schema = NULL;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (!is_normal_schema(schema_type)
             || OB_INVALID_ID == schema_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(schema_type), K(schema_id));
  } else if (OB_FAIL(get_from_local_cache(schema_type, schema_id, schema))) {
    if (OB_ENTRY_NOT_EXIST != ret) {
      LOG_WARN("get from local cache failed [id to schema]",
               KR(ret), K(schema_type), K(schema_id));
    } else if (OB_FAIL(get_schema_mgr( mgr))) {
    } else if (OB_NOT_NULL(mgr)) {
      // case 1: not lazy mode
      if (TABLE_SIMPLE_SCHEMA == schema_type) {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("should fetch simple table schema in lazy mode", KR(ret), K(schema_id), K(specified_version));
      } else {
        if (OB_INVALID_VERSION != specified_version) {
          schema_version = specified_version;
        } else if (OB_FAIL(get_schema_version(schema_type,
                                              schema_id,
                                              schema_version))) {
        }
        if (OB_SUCC(ret)) {
          if (OB_INVALID_VERSION == schema_version) {
            if (is_cte_table(schema_id)) {
              LOG_INFO("invalid version", K(schema_type),
                       K(schema_id), K(specified_version));
            }
          } else if (OB_FAIL(get_schema_status(schema_status))) {
          } else if (OB_FAIL(schema_service_->get_schema(mgr,
                                                         schema_status,
                                                         schema_type,
                                                         schema_id,
                                                         schema_version,
                                                         handle,
                                                         base_schema))) {
          } else if (OB_ISNULL(base_schema)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("NULL ptr, unexpected", KR(ret), K(schema_status), K(schema_type),
                     K(schema_id), K(schema_version), K(specified_version));
          } else if (OB_FAIL(put_to_local_cache(schema_type, schema_id,
                                                base_schema, handle))) {
          } else {
            schema = static_cast<const T *>(base_schema);
          }
        }
      }
    } else {
      // case 2: lazy mode
      if (OB_INVALID_VERSION != specified_version) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("specified_version should be invalid for lazy mode", KR(ret),
                 K(schema_type), K(schema_id), K(specified_version));
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(get_schema_version(schema_version))) {
      } else if (OB_FAIL(schema_service_->get_schema(
          NULL,
          schema_status,
          schema_type,
          schema_id,
          schema_version,
          handle,
          base_schema))) {
      } else if (OB_ISNULL(base_schema)) {
        // schema may not exist
      } else if (OB_FAIL(put_to_local_cache(schema_type, schema_id,
                                            base_schema, handle))) {
      } else {
        schema = static_cast<const T *>(base_schema);
      }
    }
  }

  return ret;
}

const ObUserInfo *ObSchemaGetterGuard::get_user_info(const uint64_t user_id)
{
  const ObUserInfo *user_info = NULL;
  int ret = get_user_info(user_id, user_info);
  return OB_SUCC(ret) ? user_info : NULL;
}


const ObColumnSchemaV2 *ObSchemaGetterGuard::get_column_schema(
      const uint64_t table_id,
      const uint64_t column_id)
{
  const ObColumnSchemaV2 *column_schema = NULL;
  int ret = get_column_schema( table_id, column_id, column_schema);
  return OB_SUCC(ret) ? column_schema : NULL;
}

const ObServerRuntimeSchema *ObSchemaGetterGuard::get_server_runtime_info(const ObString &runtime_name)
{
  int ret = OB_SUCCESS;
  const ObServerRuntimeSchema *runtime_info = NULL;
  if (OB_FAIL(get_server_runtime_info(runtime_name, runtime_info))) {
  }
  return OB_SUCC(ret) ? runtime_info : NULL;
}

#define GET_SIMPLE_SCHEMAS_IN_RUNTIME_FUNC_DEFINE(SCHEMA, SIMPLE_SCHEMA_TYPE) \
  int ObSchemaGetterGuard::get_##SCHEMA##_schemas_in_runtime(                       \
      ObIArray<const SIMPLE_SCHEMA_TYPE*> &schema_array)       \
  {                                                                                \
    int ret = OB_SUCCESS;                                                          \
    const ObSchemaMgr *mgr = NULL;                                                 \
    schema_array.reset();                                                          \
    if (!check_inner_stat()) {                                                     \
      ret = OB_INNER_STAT_ERROR;                                                   \
      LOG_WARN("inner stat error", KR(ret));                                        \
    } else if (OB_FAIL(check_lazy_guard( mgr))) { \
      LOG_WARN("fail to check lazy guard", KR(ret)); \
    } else if (OB_FAIL(mgr->get_##SCHEMA##_schemas_in_runtime(schema_array))) {  \
      LOG_WARN("get "#SCHEMA" schemas in runtime failed", KR(ret));    \
    }                                                                             \
    return ret;                                                                   \
  }
GET_SIMPLE_SCHEMAS_IN_RUNTIME_FUNC_DEFINE(database, ObSimpleDatabaseSchema);
#undef GET_SIMPLE_SCHEMAS_IN_RUNTIME_FUNC_DEFINE

#define GET_SCHEMAS_IN_RUNTIME_FUNC_DEFINE(SCHEMA, SCHEMA_TYPE, SIMPLE_SCHEMA_TYPE, SCHEMA_TYPE_ENUM) \
  int ObSchemaGetterGuard::get_##SCHEMA##_schemas_in_runtime(                       \
      ObIArray<const SCHEMA_TYPE *> &schema_array)       \
  {                                                                                \
    int ret = OB_SUCCESS;                                                          \
    const ObSchemaMgr *mgr = NULL;                                                 \
    schema_array.reset();                                                          \
    ObArray<const SIMPLE_SCHEMA_TYPE *> simple_schemas;                            \
    if (!check_inner_stat()) {                                                     \
      ret = OB_INNER_STAT_ERROR;                                                   \
      LOG_WARN("inner stat error", KR(ret));                                        \
    } else if (OB_FAIL(check_lazy_guard( mgr))) { \
      LOG_WARN("fail to check lazy guard", KR(ret)); \
    } else if (OB_FAIL(mgr->get_##SCHEMA##_schemas_in_runtime(simple_schemas))) {  \
      LOG_WARN("get "#SCHEMA" schemas in runtime failed", KR(ret));    \
    } else {                                                                       \
      FOREACH_CNT_X(simple_schema, simple_schemas, OB_SUCC(ret)) {                 \
        const SIMPLE_SCHEMA_TYPE *tmp_schema = *simple_schema;                    \
        const SCHEMA_TYPE *schema = NULL;                                         \
        if (OB_ISNULL(tmp_schema)) {                                              \
          ret = OB_ERR_UNEXPECTED;                                                \
          LOG_WARN("NULL ptr", KR(ret));                                           \
        } else if (OB_FAIL(get_schema(SCHEMA_TYPE_ENUM,                           \
                                 tmp_schema->get_##SCHEMA##_id(),                 \
                                 schema,                                          \
                                 tmp_schema->get_schema_version()))) {            \
          LOG_WARN("get "#SCHEMA" schema failed", KR(ret));          \
        } else if (OB_ISNULL(schema)) {                                           \
          ret = OB_ERR_UNEXPECTED;                                                \
          LOG_WARN("NULL ptr", KR(ret), KP(schema));                               \
        } else if (OB_FAIL(schema_array.push_back(schema))) {                     \
          LOG_WARN("push back schema failed", KR(ret));                            \
        }                                                                         \
      }                                                                           \
    }                                                                             \
    return ret;                                                                   \
  }
GET_SCHEMAS_IN_RUNTIME_FUNC_DEFINE(user, ObUserInfo, ObSimpleUserSchema, USER_SCHEMA);
GET_SCHEMAS_IN_RUNTIME_FUNC_DEFINE(database, ObDatabaseSchema, ObSimpleDatabaseSchema, DATABASE_SCHEMA);
#undef GET_SCHEMAS_IN_RUNTIME_FUNC_DEFINE

#define GET_SCHEMAS_WITH_MGR_IN_RUNTIME_FUNC_DEFINE(MGR, SCHEMA, SCHEMA_TYPE, SIMPLE_SCHEMA_TYPE, SCHEMA_TYPE_ENUM) \
  int ObSchemaGetterGuard::get_##SCHEMA##_schemas_in_runtime(                       \
      ObIArray<const SCHEMA_TYPE *> &schema_array)       \
  {                                                                                \
    int ret = OB_SUCCESS;                                                          \
    const ObSchemaMgr *mgr = NULL;                                                 \
    schema_array.reset();                                                          \
    ObArray<const SIMPLE_SCHEMA_TYPE *> simple_schemas;                            \
    if (!check_inner_stat()) {                                                     \
      ret = OB_INNER_STAT_ERROR;                                                   \
      LOG_WARN("inner stat error", KR(ret));                                        \
    } else if (OB_FAIL(check_lazy_guard( mgr))) { \
      LOG_WARN("fail to check lazy guard", KR(ret)); \
    } else if (OB_FAIL((mgr->MGR).get_##SCHEMA##_schemas_in_runtime(          \
                                                              simple_schemas))) {  \
      LOG_WARN("get "#SCHEMA" schemas in runtime failed", KR(ret));    \
    } else {                                                                       \
      FOREACH_CNT_X(simple_schema, simple_schemas, OB_SUCC(ret)) {                 \
        const SIMPLE_SCHEMA_TYPE *tmp_schema = *simple_schema;                    \
        const SCHEMA_TYPE *schema = NULL;                                         \
        if (OB_ISNULL(tmp_schema)) {                                              \
          ret = OB_ERR_UNEXPECTED;                                                \
          LOG_WARN("NULL ptr", KR(ret));                                           \
        } else if (OB_FAIL(get_schema(SCHEMA_TYPE_ENUM,                           \
                                 tmp_schema->get_##SCHEMA##_id(),                 \
                                 schema,                                          \
                                 tmp_schema->get_schema_version()))) {            \
          LOG_WARN("get "#SCHEMA" schema failed", KR(ret));          \
        } else if (OB_ISNULL(schema)) {                                           \
          ret = OB_ERR_UNEXPECTED;                                                \
          LOG_WARN("NULL ptr", KR(ret), KP(schema));                               \
        } else if (OB_FAIL(schema_array.push_back(schema))) {                     \
          LOG_WARN("push back schema failed", KR(ret));                            \
        }                                                                         \
      }                                                                           \
    }                                                                             \
    return ret;                                                                   \
  }

GET_SCHEMAS_WITH_MGR_IN_RUNTIME_FUNC_DEFINE(outline_mgr_, outline, ObOutlineInfo, ObSimpleOutlineSchema, OUTLINE_SCHEMA);
GET_SCHEMAS_WITH_MGR_IN_RUNTIME_FUNC_DEFINE(routine_mgr_, routine, ObRoutineInfo, ObSimpleRoutineSchema, ROUTINE_SCHEMA);
GET_SCHEMAS_WITH_MGR_IN_RUNTIME_FUNC_DEFINE(package_mgr_, package, ObPackageInfo, ObSimplePackageSchema, PACKAGE_SCHEMA);
GET_SCHEMAS_WITH_MGR_IN_RUNTIME_FUNC_DEFINE(trigger_mgr_, trigger, ObTriggerInfo, ObSimpleTriggerSchema, TRIGGER_SCHEMA);
#undef GET_SCHEMAS_WITH_MGR_IN_RUNTIME_FUNC_DEFINE

int ObSchemaGetterGuard::get_outline_infos_in_runtime(common::ObIArray<const ObOutlineInfo *> &table_schemas)
{
  return get_outline_schemas_in_runtime(table_schemas);
}


int ObSchemaGetterGuard::get_routine_infos_in_runtime(common::ObIArray<const ObRoutineInfo *> &routine_infos)
{
  return get_routine_schemas_in_runtime(routine_infos);
}

int ObSchemaGetterGuard::get_trigger_infos_in_runtime(ObIArray<const ObTriggerInfo *> &triger_infos)
{
  return get_trigger_schemas_in_runtime(triger_infos);
}

#define GET_TABLE_IDS_IN_DST_SCHEMA_FUNC_DEFINE(DST_SCHEMA)                          \
  int ObSchemaGetterGuard::get_table_ids_in_##DST_SCHEMA(   \
      const uint64_t dst_schema_id,                                                  \
      ObIArray<uint64_t> &table_ids)                                                 \
  {                                                                                  \
    int ret = OB_SUCCESS;                                                            \
    const ObSchemaMgr *mgr = NULL;                                                   \
    ObArray<const ObSimpleTableSchemaV2 *> schemas;                                  \
    table_ids.reset();                                                               \
    if (!check_inner_stat()) {                                                       \
      ret = OB_INNER_STAT_ERROR;                                                     \
      LOG_WARN("inner stat error", KR(ret));                                          \
    } else if (OB_INVALID_ID == dst_schema_id) {                                     \
      ret = OB_INVALID_ARGUMENT;                                                     \
      LOG_WARN("invalid argument", KR(ret), K(dst_schema_id));          \
    } else if (OB_FAIL(get_schema_mgr( mgr))) {                            \
      LOG_WARN("fail to get schema mgr", KR(ret));                    \
    } else if (OB_ISNULL(mgr)) {                                                     \
      ret = OB_SCHEMA_EAGAIN;                                                        \
      LOG_WARN("get simple schema in lazy mode not supported", KR(ret));\
    } else if (OB_FAIL(mgr->get_table_schemas_in_##DST_SCHEMA(             \
          dst_schema_id, schemas))) {                                                \
      LOG_WARN("get table schemas in "#DST_SCHEMA" failed", KR(ret),                  \
               #DST_SCHEMA"_id", dst_schema_id);                       \
    } else {                                                                         \
      FOREACH_CNT_X(schema, schemas, OB_SUCC(ret)) {                                 \
        const ObSimpleTableSchemaV2 *tmp_schema = *schema;                           \
        if (OB_ISNULL(tmp_schema)) {                                                 \
          ret = OB_ERR_UNEXPECTED;                                                   \
          LOG_WARN("NULL ptr", KR(ret), KP(tmp_schema));                              \
        } else if (OB_FAIL(table_ids.push_back(tmp_schema->get_table_id()))) {       \
          LOG_WARN("push back table id failed", KR(ret));                             \
        }                                                                            \
      }                                                                              \
    }                                                                                \
    return ret;                                                                      \
  }

GET_TABLE_IDS_IN_DST_SCHEMA_FUNC_DEFINE(database);
#undef GET_TABLE_IDS_IN_DST_SCHEMA_FUNC_DEFINE

int ObSchemaGetterGuard::get_table_ids_in_runtime(ObIArray<uint64_t> &table_ids)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  ObArray<const ObSimpleTableSchemaV2 *> schemas;
  table_ids.reset();
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_FAIL(get_schema_mgr( mgr))) {
  } else if (OB_ISNULL(mgr)) {
    ret = OB_SCHEMA_EAGAIN;
    LOG_WARN("get simple schema in lazy mode not supported", KR(ret));
  } else if (OB_FAIL(mgr->get_table_schemas_in_runtime(schemas))) {
  } else {
    FOREACH_CNT_X(schema, schemas, OB_SUCC(ret)) {
      const ObSimpleTableSchemaV2 *tmp_schema = *schema;
      if (OB_ISNULL(tmp_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("NULL ptr", KR(ret), KP(tmp_schema));
      } else if (OB_FAIL(table_ids.push_back(tmp_schema->get_table_id()))) {
      }
    }
  }
  return ret;
}
#define GET_TABLE_SCHEMAS_IN_DST_SCHEMA_FUNC_DEFINE(DST_SCHEMA)                      \
  int ObSchemaGetterGuard::get_table_schemas_in_##DST_SCHEMA(                        \
      const uint64_t dst_schema_id,                                                  \
      ObIArray<const ObTableSchema *> &schema_array)                                 \
  {                                                                                  \
    int ret = OB_SUCCESS;                                                            \
    const ObSchemaMgr *mgr = NULL;                                                   \
    ObArray<const ObSimpleTableSchemaV2 *> schemas;                                  \
    schema_array.reset();                                                            \
    if (!check_inner_stat()) {                                                       \
      ret = OB_INNER_STAT_ERROR;                                                     \
      LOG_WARN("inner stat error", KR(ret));                                          \
    } else if (OB_INVALID_ID == dst_schema_id) {                                     \
      ret = OB_INVALID_ARGUMENT;                                                     \
      LOG_WARN("invalid argument", KR(ret), K(dst_schema_id));          \
    } else if (OB_FAIL(get_schema_mgr( mgr))) {                            \
      LOG_WARN("fail to get schema mgr", KR(ret));                    \
    } else if (OB_ISNULL(mgr)) {                                                     \
      ret = OB_SCHEMA_EAGAIN;                                                        \
      LOG_WARN("get simple schema in lazy mode not supported", KR(ret));\
    } else if (OB_FAIL(mgr->get_table_schemas_in_##DST_SCHEMA(                        \
          dst_schema_id, schemas))) {                                                \
      LOG_WARN("get table schemas in "#DST_SCHEMA" failed", KR(ret),                  \
                                                                \
               #DST_SCHEMA"_id", dst_schema_id);                                     \
    } else {                                                                         \
      FOREACH_CNT_X(schema, schemas, OB_SUCC(ret)) {                                 \
        const ObSimpleTableSchemaV2 *tmp_schema = *schema;                           \
        const ObTableSchema *table_schema = NULL;                                    \
        if (OB_ISNULL(tmp_schema)) {                                                 \
          ret = OB_ERR_UNEXPECTED;                                                   \
          LOG_WARN("NULL ptr", KR(ret), KP(tmp_schema));                              \
        } else if (OB_FAIL(get_schema(TABLE_SCHEMA,                                  \
            tmp_schema->get_table_id(),                 \
            table_schema, tmp_schema->get_schema_version()))) {                      \
          LOG_WARN("get table schema failed", KR(ret), KPC(tmp_schema));\
        } else if (OB_ISNULL(table_schema)) {                                        \
          ret = OB_ERR_UNEXPECTED;                                                   \
          LOG_WARN("NULL ptr", KR(ret), KP(table_schema));                            \
        } else if (OB_FAIL(schema_array.push_back(table_schema))) {                  \
          LOG_WARN("push back table schema failed", KR(ret));                         \
        }                                                                            \
      }                                                                              \
    }                                                                                \
    return ret;                                                                      \
  }

GET_TABLE_SCHEMAS_IN_DST_SCHEMA_FUNC_DEFINE(database);
#undef GET_TABLE_SCHEMAS_IN_DST_SCHEMA_FUNC_DEFINE

int ObSchemaGetterGuard::get_table_schemas_in_runtime(common::ObIArray<const ObTableSchema *> &table_schemas)
{
  int ret = OB_SUCCESS;
  bool only_view_schema = false;
  ret = get_table_schemas_in_runtime_(only_view_schema, table_schemas);
  return ret;
}

int ObSchemaGetterGuard::get_view_schemas_in_runtime(ObIArray<const ObTableSchema *> &table_schemas)
{
  int ret = OB_SUCCESS;
  bool only_view_schema = true;
  ret = get_table_schemas_in_runtime_(only_view_schema, table_schemas);
  return ret;
}

int ObSchemaGetterGuard::get_table_schemas_in_runtime_(const bool only_view_schema,
                                                      ObIArray<const ObTableSchema *> &table_schemas)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  ObArray<const ObSimpleTableSchemaV2 *> schemas;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_FAIL(get_schema_mgr( mgr))) {
  } else if (OB_ISNULL(mgr)) {
    ret = OB_SCHEMA_EAGAIN;
    LOG_WARN("get simple schema in lazy mode not supported", KR(ret));
  } else if (OB_FAIL(mgr->get_table_schemas_in_runtime(schemas))) {
  } else {
    FOREACH_CNT_X(schema, schemas, OB_SUCC(ret)) {
      const ObSimpleTableSchemaV2 *tmp_schema = *schema;
      const ObTableSchema *table_schema = NULL;
      if (OB_ISNULL(tmp_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("NULL ptr", KR(ret), KP(tmp_schema));
      } else if (only_view_schema && !tmp_schema->is_view_table()) {
        // do nothing
      } else if (OB_FAIL(get_schema(TABLE_SCHEMA,
          tmp_schema->get_table_id(),
          table_schema, tmp_schema->get_schema_version()))) {
      } else if (OB_ISNULL(table_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("NULL ptr", KR(ret), KP(table_schema));
      } else if (OB_FAIL(table_schemas.push_back(table_schema))) {
      }
    }
  }
  return ret;
}

int ObSchemaGetterGuard::get_table_schemas_in_runtime(common::ObIArray<const ObSimpleTableSchemaV2 *> &table_schemas)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  table_schemas.reset();

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_FAIL(get_schema_mgr( mgr))) {
  } else if (OB_ISNULL(mgr)) {
    ret = OB_SCHEMA_EAGAIN;
    LOG_WARN("get simple schema in lazy mode not supported", KR(ret));
  } else if (OB_FAIL(mgr->get_table_schemas_in_runtime(table_schemas))) {
  }
  return ret;
}

# define GET_SIMPLE_TABLE_SCHEMAS_IN_DST_SCHEMA_FUNC_DEFINE(DST_SCHEMA) \
int ObSchemaGetterGuard::get_table_schemas_in_##DST_SCHEMA( \
    const uint64_t dst_schema_id, \
    common::ObIArray<const ObSimpleTableSchemaV2 *> &table_schemas) \
{ \
  int ret = OB_SUCCESS; \
  const ObSchemaMgr *mgr = NULL; \
  table_schemas.reset(); \
  if (!check_inner_stat()) { \
    ret = OB_INNER_STAT_ERROR; \
    LOG_WARN("inner stat error", KR(ret)); \
  } else if (OB_INVALID_ID == dst_schema_id) { \
    ret = OB_INVALID_ARGUMENT; \
    LOG_WARN("invalid argument", KR(ret), K(dst_schema_id)); \
  } else if (OB_FAIL(get_schema_mgr( mgr))) { \
    LOG_WARN("fail to get schema mgr", KR(ret));\
  } else if (OB_ISNULL(mgr)) { \
    ret = OB_SCHEMA_EAGAIN; \
    LOG_WARN("get simple schema in lazy mode not supported", KR(ret)); \
  } else if (OB_FAIL(mgr->get_table_schemas_in_##DST_SCHEMA(dst_schema_id, table_schemas))) { \
    LOG_WARN("get table schemas in "#DST_SCHEMA" failed", KR(ret), K(dst_schema_id)); \
  } \
  return ret; \
}
GET_SIMPLE_TABLE_SCHEMAS_IN_DST_SCHEMA_FUNC_DEFINE(database)
# undef GET_SIMPLE_TABLE_SCHEMAS_IN_DST_SCHEMA_FUNC_DEFINE

int ObSchemaGetterGuard::get_runtime_name_case_mode(ObNameCaseMode &mode)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  mode = OB_NAME_CASE_INVALID;

  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else {
    ret = mgr->get_runtime_name_case_mode(mode);
  }

  return ret;
}

// Inner SQL reads the current read-only value from system-variable metadata.
int ObSchemaGetterGuard::get_runtime_read_only(bool &read_only)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;

  read_only = false;
  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else {
    ret = mgr->get_runtime_read_only(read_only);
  }

  return ret;
}

int ObSchemaGetterGuard::check_outline_exist_with_name(const uint64_t database_id,
    const common::ObString &name,
    const bool is_format,
    uint64_t &outline_id,
    bool &exist)
{
  int ret= OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  outline_id = OB_INVALID_ID;
  exist = false;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == database_id
             || name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(database_id), K(name));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else {
    const ObSimpleOutlineSchema *schema = NULL;
    if (OB_FAIL(mgr->outline_mgr_.get_outline_schema_with_name(database_id,
        name, is_format, schema))) {
    } else if (NULL != schema) {
      outline_id = schema->get_outline_id();
      exist = true;
    }
  }

  return ret;
}

int ObSchemaGetterGuard::check_outline_exist_with_sql_id(const uint64_t database_id,
    const common::ObString &sql_id,
    const bool is_format,
    bool &exist)
{
  int ret= OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  exist = false;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == database_id
             || sql_id.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(database_id), K(sql_id));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else {
    const ObSimpleOutlineSchema *schema = NULL;
    if (OB_FAIL(mgr->outline_mgr_.get_outline_schema_with_sql_id(database_id,
        sql_id, is_format, schema))) {
    } else if (NULL != schema) {
      exist = true;
    }
  }

  return ret;
}

int ObSchemaGetterGuard::check_outline_exist_with_sql(const uint64_t database_id,
    const common::ObString &paramlized_sql,
    const bool is_format,
    bool &exist)
{
  int ret= OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  exist = false;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == database_id
             || paramlized_sql.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(database_id), K(paramlized_sql));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else {
    const ObSimpleOutlineSchema *schema = NULL;
    if (OB_FAIL(mgr->outline_mgr_.get_outline_schema_with_signature(database_id,
        paramlized_sql, is_format, schema))) {
    } else if (NULL != schema) {
      exist = true;
    }
  }

  return ret;
}

int ObSchemaGetterGuard::get_outline_info_with_name(const uint64_t database_id,
    const common::ObString &name,
    const bool is_format,
    const ObOutlineInfo *&outline_info)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  outline_info = NULL;

  const ObSimpleOutlineSchema *simple_outline = NULL;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == database_id
             || name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(database_id), K(name), KR(ret));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->outline_mgr_.get_outline_schema_with_name(database_id, name, is_format, simple_outline))) {
  } else if (NULL == simple_outline) {
    LOG_INFO("outline not exist", K(database_id), K(name));
  } else if (OB_FAIL(get_schema(OUTLINE_SCHEMA,
                                simple_outline->get_outline_id(),
                                outline_info,
                                simple_outline->get_schema_version()))) {
  } else if (OB_ISNULL(outline_info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", KR(ret), KP(outline_info));
  }

  return ret;
}

int ObSchemaGetterGuard::get_outline_info_with_name(const ObString &db_name,
    const ObString &outline_name,
    const bool is_format,
    const ObOutlineInfo *&outline_info)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  outline_info = NULL;

  const ObSimpleOutlineSchema *simple_outline = NULL;
  uint64_t database_id = OB_INVALID_ID;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (db_name.empty()
             || outline_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(db_name), K(outline_name), KR(ret));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(get_database_id(db_name, database_id)))  {
  } else if (OB_INVALID_ID == database_id) {
    // do-nothing
  } else if (OB_FAIL(mgr->outline_mgr_.get_outline_schema_with_name(database_id, outline_name, is_format, simple_outline))) {
  } else if (NULL == simple_outline) {
  } else if (OB_FAIL(get_schema(OUTLINE_SCHEMA,
                                simple_outline->get_outline_id(),
                                outline_info,
                                simple_outline->get_schema_version()))) {
  } else if (OB_ISNULL(outline_info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", KR(ret), KP(outline_info));
  } else {/*do nothing*/}

  return ret;
}
int ObSchemaGetterGuard::get_outline_info_with_signature(const uint64_t database_id,
    const common::ObString &signature,
    const bool is_format,
    const ObOutlineInfo *&outline_info)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  outline_info = NULL;

  const ObSimpleOutlineSchema *simple_outline = NULL;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == database_id
             || signature.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(database_id), K(signature), KR(ret));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->outline_mgr_.get_outline_schema_with_signature(database_id, signature, is_format, simple_outline))) {
  } else if (NULL == simple_outline) {
  } else if (OB_FAIL(get_schema(OUTLINE_SCHEMA,
                                simple_outline->get_outline_id(),
                                outline_info,
                                simple_outline->get_schema_version()))) {
  } else if (OB_ISNULL(outline_info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", KR(ret), KP(outline_info));
  }

  return ret;
}

int ObSchemaGetterGuard::check_routine_exist(uint64_t database_id, uint64_t package_id,
                                             const ObString &routine_name, uint64_t overload,
                                             ObRoutineType routine_type, bool &exist) const
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  exist = false;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == database_id || routine_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(database_id), K(routine_name));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else {
    const ObSimpleRoutineSchema *schema = NULL;
    if (OB_FAIL(mgr->routine_mgr_.get_routine_schema( database_id, package_id,
                                                      routine_name, overload, routine_type, schema))) {
    } else if (NULL != schema) {
      exist = true;
    }
  }
  return ret;
}

int ObSchemaGetterGuard::check_package_exist(uint64_t database_id,
                                             const common::ObString &package_name,
                                             ObPackageType package_type,
                                             bool &exist) {
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  exist = false;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == database_id || package_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(database_id), K(package_name));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else {
    const ObSimplePackageSchema *schema = NULL;
    if (OB_FAIL(mgr->package_mgr_.get_package_schema(database_id, package_name, package_type, schema))) {
    } else if (NULL != schema) {
      exist = true;
    }
  }
  return ret;
}

int ObSchemaGetterGuard::get_package_id(uint64_t database_id,
                                        const ObString &package_name, ObPackageType type,
                                        uint64_t &package_id)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  const ObSimplePackageSchema *schema = NULL;
  package_id = OB_INVALID_ID;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == database_id || package_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(database_id), K(package_name));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->package_mgr_.get_package_schema(database_id, package_name, type, schema))) {
  } else if (NULL != schema) {
    package_id = schema->get_package_id();
  }
  return ret;
}

int ObSchemaGetterGuard::get_routine_id(uint64_t database_id, uint64_t package_id,
                                        const ObString &routine_name, uint64_t overload,
                                        ObRoutineType routine_type, uint64_t &routine_id)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  routine_id = OB_INVALID_ID;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == database_id
             || routine_name.empty()
             || (overload == OB_INVALID_INDEX)
             || (INVALID_ROUTINE_TYPE == routine_type)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(database_id), K(routine_name));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else {
    const ObSimpleRoutineSchema *schema = NULL;
    if (OB_FAIL(mgr->routine_mgr_.get_routine_schema( database_id, package_id,
                                                      routine_name, overload, routine_type, schema))) {
    } else if (NULL != schema) {
      routine_id = schema->get_routine_id();
    }
  }
  return ret;
}

int ObSchemaGetterGuard::check_routine_definer_existed(const ObString &user_name, bool &existed)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (user_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(user_name));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->routine_mgr_.check_user_reffered_by_definer(user_name, existed))) {
  }
  return ret;
}

int ObSchemaGetterGuard::get_routine_info( const uint64_t database_id, const uint64_t package_id,
    const ObString &routine_name, uint64_t overload,
    ObRoutineType routine_type, const ObRoutineInfo *&routine_info)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  routine_info = NULL;

  const ObSimpleRoutineSchema *simple_routine = NULL;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if ((OB_INVALID_ID == database_id)
      || routine_name.empty()
      || (overload == OB_INVALID_INDEX)
      || (INVALID_ROUTINE_TYPE == routine_type)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(database_id), K(package_id), K(routine_name),
             K(overload), K(routine_type), KR(ret));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->routine_mgr_.get_routine_schema( database_id, package_id,
                                                           routine_name, overload, routine_type, simple_routine))) {
  } else if (NULL == simple_routine) {
  } else if (OB_FAIL(get_schema(ROUTINE_SCHEMA,
                                simple_routine->get_routine_id(),
                                routine_info,
                                simple_routine->get_schema_version()))) {
  } else if (OB_ISNULL(routine_info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", KR(ret), KP(routine_info));
  } else {/*do nothing*/}
  return ret;
}

int ObSchemaGetterGuard::get_routine_info(
    const uint64_t routine_id,
    const ObRoutineInfo *&routine_info)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  const ObSimpleRoutineSchema *simple_routine = NULL;
  routine_info = NULL;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner state error", KR(ret));
  } else if (OB_UNLIKELY(routine_id == OB_INVALID_ID)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(routine_id), KR(ret));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->get_routine_schema( routine_id, simple_routine))) {
  } else if (NULL == simple_routine) {
  } else if (OB_FAIL(get_schema(ROUTINE_SCHEMA,
                                simple_routine->get_routine_id(),
                                routine_info,
                                simple_routine->get_schema_version()))) {
  }
  return ret;
}

int ObSchemaGetterGuard::get_package_routine_infos(uint64_t database_id, uint64_t package_id, const common::ObString &routine_name,
  ObRoutineType routine_type, common::ObIArray<const ObIRoutineInfo *> &routine_infos,
  ObRoutineType inside_routine_type)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  const ObSimpleRoutineSchema *simple_routine = NULL;
  routine_infos.reset();
  ObArray<const ObSimpleRoutineSchema *> simple_routines;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if ((OB_INVALID_ID == database_id)
      || (OB_INVALID_ID == package_id)
      || routine_name.empty()
      || (ROUTINE_PROCEDURE_TYPE != routine_type && ROUTINE_FUNCTION_TYPE != routine_type)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(database_id), K(package_id),
                                               K(routine_name), KR(ret));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->routine_mgr_.get_routine_schema( database_id, package_id,
                                                 routine_name, 0,
                                                 inside_routine_type, simple_routine))) {
  } else if (NULL != simple_routine) {
    if (OB_FAIL(simple_routines.push_back(simple_routine))) {
    }
  } else {
    bool end_loop = false;
    for (int i=1; OB_SUCC(ret) && !end_loop; i++) {
      if (OB_FAIL(mgr->routine_mgr_.get_routine_schema( database_id, package_id,
                                                 routine_name, i,
                                                 inside_routine_type, simple_routine))) {
      } else if (NULL != simple_routine) {
        if (OB_FAIL(simple_routines.push_back(simple_routine))) {
        }
      } else {
        end_loop = true;
      }
    }
  }
  if (OB_SUCC(ret)) {
    FOREACH_CNT_X(simple_routine, simple_routines, OB_SUCC(ret)) {
      const ObSimpleRoutineSchema *tmp_schema = *simple_routine;
      const ObRoutineInfo *schema = NULL;
      if (OB_ISNULL(tmp_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("NULL ptr", KR(ret));
      } else if (OB_FAIL(get_schema(ROUTINE_SCHEMA,
                                    tmp_schema->get_routine_id(),
                                    schema,
                                    tmp_schema->get_schema_version()))) {
      } else if (OB_ISNULL(schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("NULL ptr", KR(ret), KP(schema));
      } else {
        if (ROUTINE_PROCEDURE_TYPE == routine_type) {
          if (schema->is_procedure()) {
            if (OB_FAIL(routine_infos.push_back(schema))) {
            }
          }
        } else {  //ROUTINE_FUNCTION_TYPE
          if (schema->is_function()) {
            if (OB_FAIL(routine_infos.push_back(schema))) {
            }
          }
        }
      }
    }
  }

  return ret;
}

int ObSchemaGetterGuard::get_package_info(
    const uint64_t package_id,
    const ObPackageInfo *&package_info)
{
  int ret = OB_SUCCESS;
  if (!ObTriggerInfo::is_trigger_package_id(package_id)) {
    const ObSchemaMgr *mgr = NULL;
    const ObSimplePackageSchema *simple_package = NULL;
    package_info = NULL;
    if (!check_inner_stat()) {
      ret = OB_INNER_STAT_ERROR;
      LOG_WARN("inner state error", KR(ret));
    } else if (OB_UNLIKELY(package_id == OB_INVALID_ID)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(package_id), KR(ret));
    } else if (OB_FAIL(check_lazy_guard( mgr))) {
    } else if (OB_FAIL(mgr->get_package_schema( package_id, simple_package))) {
    } else if (NULL == simple_package) {
    } else if (OB_FAIL(get_schema(PACKAGE_SCHEMA,
                                  simple_package->get_package_id(),
                                  package_info,
                                  simple_package->get_schema_version()))) {
    }
  } else {
    if (OB_FAIL(get_package_info_from_trigger(package_id, package_info))) {
    }
  }
  return ret;
}

int ObSchemaGetterGuard::get_simple_package_info(
    const uint64_t package_id,
    const ObSimplePackageSchema *&package_info)
{
  int ret = OB_SUCCESS;
  package_info = NULL;
  if (!ObTriggerInfo::is_trigger_package_id(package_id)) {
    const ObSchemaMgr *mgr = NULL;
    if (!check_inner_stat()) {
      ret = OB_INNER_STAT_ERROR;
      LOG_WARN("inner state error", KR(ret));
    } else if (OB_UNLIKELY(package_id == OB_INVALID_ID)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(package_id), KR(ret));
    } else if (OB_FAIL(check_lazy_guard( mgr))) {
    } else if (OB_FAIL(mgr->get_package_schema( package_id, package_info))) {
    } else if (NULL == package_info) {
    }
  } else {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get simple package info error", KR(ret), K(package_id));
  }
  return ret;
}

int ObSchemaGetterGuard::get_simple_trigger_schema(const uint64_t trigger_id,
    const ObSimpleTriggerSchema *&simple_trigger)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner state error", KR(ret));
  } else if (OB_UNLIKELY(trigger_id == OB_INVALID_ID)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(trigger_id));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->get_trigger_schema( trigger_id, simple_trigger))) {
  }
  return ret;
}

int ObSchemaGetterGuard::get_simple_trigger_schema(const uint64_t database_id,
    const ObString &trigger_name,
    const ObSimpleTriggerSchema *&simple_trigger)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner state error", KR(ret));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->trigger_mgr_.get_trigger_schema( database_id,
                                                          trigger_name, simple_trigger))) {
  }
  return ret;
}

int ObSchemaGetterGuard::get_trigger_info(
                                          const uint64_t trigger_id,
                                          const ObTriggerInfo *&trigger_info)
{
  int ret = OB_SUCCESS;
  const ObSimpleTriggerSchema *simple_trigger = NULL;
  if (OB_FAIL(get_simple_trigger_schema(trigger_id, simple_trigger))) {
  } else if (NULL == simple_trigger) {
    trigger_info = NULL;
  } else if (OB_FAIL(get_schema(TRIGGER_SCHEMA,
                                simple_trigger->get_trigger_id(),
                                trigger_info,
                                simple_trigger->get_schema_version()))) {
  }
  return ret;
}

int ObSchemaGetterGuard::get_trigger_info(
                                          const uint64_t database_id,
                                          const ObString &trigger_name,
                                          const ObTriggerInfo *&trigger_info)
{
  int ret = OB_SUCCESS;
  const ObSimpleTriggerSchema *simple_trigger = NULL;
  if (OB_FAIL(get_simple_trigger_schema(database_id, trigger_name, simple_trigger))) {
  } else if (NULL == simple_trigger) {
    trigger_info = NULL;
  } else if (OB_FAIL(get_schema(TRIGGER_SCHEMA,
                                simple_trigger->get_trigger_id(),
                                trigger_info,
                                simple_trigger->get_schema_version()))) {
  }
  return ret;
}

int ObSchemaGetterGuard::get_package_info_from_trigger(const uint64_t package_id,
    const ObPackageInfo *&package_info)
{
  int ret = OB_SUCCESS;
  uint64_t trigger_id = ObTriggerInfo::get_package_trigger_id(package_id);
  const ObTriggerInfo *trigger_info = NULL;
  if (OB_FAIL(get_trigger_info( trigger_id, trigger_info))) {
  } else if (OB_ISNULL(trigger_info)) {
    package_info = NULL;
  } else {
    package_info = !ObTriggerInfo::is_trigger_body_package_id(package_id) ?
                     &trigger_info->get_package_spec_info() :
                     &trigger_info->get_package_body_info();
  }
  return ret;
}

int ObSchemaGetterGuard::get_package_info_from_trigger(const uint64_t package_id,
    const ObPackageInfo *&package_spec_info,
    const ObPackageInfo *&package_body_info)
{
  int ret = OB_SUCCESS;
  uint64_t trigger_id = ObTriggerInfo::get_package_trigger_id(package_id);
  const ObTriggerInfo *trigger_info = NULL;
  if (OB_FAIL(get_trigger_info( trigger_id, trigger_info))) {
  } else if (OB_ISNULL(trigger_info)) {
    package_spec_info = NULL;
    package_body_info = NULL;
  } else {
    package_spec_info = &trigger_info->get_package_spec_info();
    package_body_info = &trigger_info->get_package_body_info();
  }
  return ret;
}


int ObSchemaGetterGuard::get_outline_info_with_sql_id(const uint64_t database_id,
    const common::ObString &sql_id,
    const bool is_format,
    const ObOutlineInfo *&outline_info)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  outline_info = NULL;

  const ObSimpleOutlineSchema *simple_outline = NULL;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == database_id
             || sql_id.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(database_id), K(sql_id), KR(ret));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->outline_mgr_.get_outline_schema_with_sql_id(database_id, sql_id, is_format, simple_outline))) {
  } else if (NULL == simple_outline) {
  } else if (OB_FAIL(get_schema(OUTLINE_SCHEMA,
                                simple_outline->get_outline_id(),
                                outline_info,
                                simple_outline->get_schema_version()))) {
  } else if (OB_ISNULL(outline_info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", KR(ret), KP(outline_info));
  }
  return ret;
}

int ObSchemaGetterGuard::get_package_info(
    const uint64_t database_id,
    const ObString &package_name,
    ObPackageType package_type,
    const ObPackageInfo *&package_info)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  const ObSimplePackageSchema *simple_package = NULL;
  package_info = NULL;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == database_id)
      || OB_UNLIKELY(package_name.empty())
      || OB_UNLIKELY(package_type == INVALID_PACKAGE_TYPE)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(database_id), K(package_name), K(package_type), KR(ret));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->package_mgr_.get_package_schema(database_id, package_name, package_type, simple_package))) {
  } else if (NULL == simple_package) {
  } else if (OB_FAIL(get_schema(PACKAGE_SCHEMA,
                                simple_package->get_package_id(),
                                package_info,
                                simple_package->get_schema_version()))) {
  } else if (OB_ISNULL(package_info)) {
  } else {/*do nothing*/}
  return ret;
}

int ObSchemaGetterGuard::check_user_exist(const ObString &user_name,
                                          const ObString &host_name,
                                          bool &is_exist,
                                          uint64_t *user_id/*=NULL*/)
{
  int ret = OB_SUCCESS;
  is_exist = false;
  if (NULL != user_id) {
    *user_id = OB_INVALID_ID;
  }

  uint64_t tmp_user_id = OB_INVALID_ID;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_FAIL(get_user_id(user_name, host_name, tmp_user_id))) {
  } else if (OB_INVALID_ID != tmp_user_id) {
    is_exist = true;
    if (NULL != user_id) {
      *user_id = tmp_user_id;
    }
  }
  return ret;
}

int ObSchemaGetterGuard::check_user_exist(const uint64_t user_id,
                                          bool &is_exist)
{
  int ret = OB_SUCCESS;
  is_exist = false;

  int64_t schema_version = OB_INVALID_VERSION;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == user_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(user_id));
  } else if (OB_FAIL(get_schema_version(
             USER_SCHEMA, user_id, schema_version))) {
  } else if (OB_INVALID_VERSION != schema_version) {
    is_exist = true;
  }

  return ret;
}

int ObSchemaGetterGuard::check_database_exist(const common::ObString &database_name,
                                              bool &is_exist,
                                              uint64_t *database_id/*= NULL*/)
{
  int ret = OB_SUCCESS;
  is_exist = false;
  if (NULL != database_id) {
    *database_id = OB_INVALID_ID;
  }

  uint64_t tmp_database_id = OB_INVALID_ID;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (database_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(database_name));
  } else if (OB_FAIL(get_database_id(database_name, tmp_database_id))) {
  } else if (OB_INVALID_ID != tmp_database_id) {
    is_exist = true;
    if (NULL != database_id) {
      *database_id = tmp_database_id;
    }
  }

  return ret;
}

int ObSchemaGetterGuard::check_database_in_recyclebin(const uint64_t database_id,
    bool &in_recyclebin)
{
  int ret = OB_SUCCESS;
  in_recyclebin = false;
  const ObDatabaseSchema *database_schema = NULL;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == database_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(database_id), KR(ret));
  } else if (OB_FAIL(get_schema(DATABASE_SCHEMA,
                                database_id,
                                database_schema))) {
  } else if (OB_ISNULL(database_schema)) {
    ret = OB_ENTRY_NOT_EXIST;
    LOG_WARN("database schema should not be null", KR(ret), K(database_id));
  } else {
    in_recyclebin = database_schema->is_in_recyclebin();
  }
  return ret;
}

int ObSchemaGetterGuard::check_database_exist(const uint64_t database_id,
    bool &is_exist)
{
  int ret = OB_SUCCESS;
  is_exist = false;

  int64_t schema_version = OB_INVALID_VERSION;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == database_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(database_id));
  } else if (OB_FAIL(get_schema_version(
             DATABASE_SCHEMA, database_id, schema_version))) {
  } else {
    is_exist = OB_INVALID_VERSION != schema_version;
  }

  return ret;
}

int ObSchemaGetterGuard::check_table_exist(const uint64_t database_id,
                                           const common::ObString &table_name,
                                           const bool is_index,
                                           const CheckTableType check_type,  // check if temporary table is visable
                                           bool &is_exist,
                                           uint64_t *table_id/*=NULL*/)
{
  int ret = OB_SUCCESS;
  is_exist = false;
  if (NULL != table_id) {
    *table_id = OB_INVALID_ID;
  }

  uint64_t tmp_table_id = OB_INVALID_ID;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == database_id
             || table_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(database_id), K(table_name));
  } else if (OB_FAIL(get_table_id(database_id, table_name, is_index, check_type, tmp_table_id))) {
  } else if (OB_INVALID_ID != tmp_table_id) {
    is_exist = true;
    if (NULL != table_id) {
      *table_id = tmp_table_id;
    }
  }

  return ret;
}

int ObSchemaGetterGuard::check_table_exist(
    const uint64_t table_id,
    bool &is_exist)
{
  int ret = OB_SUCCESS;
  is_exist = false;

  int64_t schema_version = OB_INVALID_VERSION;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == table_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(table_id));
  } else if (is_cte_table(table_id)) {
    // fake table is only used in sql execution process and doesn't have schema.
    // We should avoid error in such situation.
  } else if (OB_FAIL(get_schema_version(TABLE_SCHEMA, table_id, schema_version))) {
  } else {
    is_exist = OB_INVALID_VERSION != schema_version;
  }

  return ret;
}

template <>
int ObSchemaGetterGuard::check_recyclebin_restore_object_exist<ObTriggerInfo>(
    const ObTriggerInfo &object_schema,
    const ObString &object_name,
    bool &object_exist)
{
  int ret = OB_SUCCESS;
  const ObSimpleTriggerSchema *simple_trigger = NULL;
  OZ (get_simple_trigger_schema(object_schema.get_database_id(),
                                object_name, simple_trigger),
      object_schema.get_trigger_id(), object_name);
  OX (object_exist = (NULL != simple_trigger))
  return ret;
}

/*
  interface for simple schema
*/

int ObSchemaGetterGuard::get_simple_table_schema(
    const uint64_t table_id,
    const ObSimpleTableSchemaV2 * &table_schema)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  table_schema = NULL;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == table_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(table_id));
  } else if (is_cte_table(table_id)) {
    // fake table is only used in sql execution process and doesn't have schema.
    // We should avoid error in such situation.
  } else if (OB_FAIL(get_schema_mgr( mgr))) {
  } else if (OB_ISNULL(mgr)) {
    // This accessor requires a materialized schema_mgr; retry a lazy guard later.
    ret = OB_SCHEMA_EAGAIN;
    LOG_WARN("schema mgr is null", KR(ret), K(table_id));
  } else if (OB_FAIL(mgr->get_table_schema( table_id, table_schema))) {
  } else if (OB_ISNULL(table_schema)) {
    LOG_INFO("table not exist", K(table_id));
  }
  return ret;
}


int ObSchemaGetterGuard::get_schema_count(int64_t &schema_count)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  schema_count = 0;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->get_schema_count(schema_count))) {
  }
  return ret;
}

int ObSchemaGetterGuard::get_schema_size(int64_t &schema_size)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  schema_size = 0;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->get_schema_size(schema_size))) {
  }
  return ret;
}


// This function return indexes which are in unavaliable status
// It's used in the following scenes:
// 1. Schedule unavaliable indexes build tasks in primary cluster.
// 2. Drop unavaliable indexes when cluster switchover.
// 3. Rebuild unavaliable indexes in physical restore.



// mock_fk_parent_table begin
int ObSchemaGetterGuard::get_mock_fk_parent_table_ids_in_database(const uint64_t database_id,
    ObIArray<uint64_t> &mock_fk_parent_table_ids)
{
  int ret= OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  mock_fk_parent_table_ids.reset();
  ObArray<const ObSimpleMockFKParentTableSchema *> simple_schemas;
  if (OB_UNLIKELY(!check_inner_stat())) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", K(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == database_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(database_id));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->mock_fk_parent_table_mgr_.get_mock_fk_parent_table_schemas_in_database(
                     database_id, simple_schemas))) {
  } else if (OB_FAIL(mock_fk_parent_table_ids.reserve(simple_schemas.count()))) {
  } else {
    FOREACH_CNT_X(schema, simple_schemas, OB_SUCC(ret)) {
      const ObSimpleMockFKParentTableSchema *tmp_schema = *schema;
      if (OB_ISNULL(tmp_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("NULL ptr", KR(ret), KP(tmp_schema));
      } else if (OB_FAIL(mock_fk_parent_table_ids.push_back(tmp_schema->get_mock_fk_parent_table_id()))) {
      }
    }
  }
  return ret;
}

int ObSchemaGetterGuard::get_simple_mock_fk_parent_table_schema(const uint64_t database_id,
    const common::ObString &name,
    const ObSimpleMockFKParentTableSchema *&schema)
{
  int ret= OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", K(ret));
  } else if (name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(database_id), K(name));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->mock_fk_parent_table_mgr_.get_mock_fk_parent_table_schema_with_name(database_id, name, schema))) {
  }
  return ret;
}

int ObSchemaGetterGuard::get_simple_mock_fk_parent_table_schema(const uint64_t mock_fk_parent_table_id,
    const ObSimpleMockFKParentTableSchema *&schema)
{
  int ret= OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", K(ret));
  } else if (OB_INVALID_ID == mock_fk_parent_table_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(mock_fk_parent_table_id));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->mock_fk_parent_table_mgr_.get_mock_fk_parent_table_schema(
                     mock_fk_parent_table_id, schema))) {
  }
  return ret;
}

int ObSchemaGetterGuard::get_mock_fk_parent_table_schema_with_name(const uint64_t database_id,
    const common::ObString &name,
    const ObMockFKParentTableSchema *&mock_fk_parent_table_schema)
{
  int ret = OB_SUCCESS;
  const ObSimpleMockFKParentTableSchema *simple_mock_fk_parent_table = NULL;
  if (OB_FAIL(get_simple_mock_fk_parent_table_schema(database_id, name, simple_mock_fk_parent_table))) {
  } else if (NULL == simple_mock_fk_parent_table) {
    mock_fk_parent_table_schema = NULL;
  } else if (OB_FAIL(get_schema(MOCK_FK_PARENT_TABLE_SCHEMA, simple_mock_fk_parent_table->get_mock_fk_parent_table_id(),
                                   mock_fk_parent_table_schema, simple_mock_fk_parent_table->get_schema_version()))) {
  }
  return ret;
}

int ObSchemaGetterGuard::get_mock_fk_parent_table_schema_with_id(const uint64_t mock_fk_parent_table_id,
    const ObMockFKParentTableSchema *&mock_fk_parent_table_schema)
{
  int ret = OB_SUCCESS;
  const ObSimpleMockFKParentTableSchema *simple_mock_fk_parent_table = NULL;
  if (OB_FAIL(get_simple_mock_fk_parent_table_schema(mock_fk_parent_table_id, simple_mock_fk_parent_table))) {
  } else if (NULL == simple_mock_fk_parent_table) {
    mock_fk_parent_table_schema = NULL;
  } else if (OB_FAIL(get_schema(MOCK_FK_PARENT_TABLE_SCHEMA, simple_mock_fk_parent_table->get_mock_fk_parent_table_id(),
                                   mock_fk_parent_table_schema, simple_mock_fk_parent_table->get_schema_version()))) {
  }
  return ret;
}



int ObSchemaGetterGuard::get_idx_schema_by_origin_idx_name(uint64_t database_id,
                                                           const common::ObString &index_name,
                                                           const ObTableSchema *&table_schema)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  table_schema = NULL;

  const ObSimpleTableSchemaV2 *simple_table = NULL;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == database_id
             || index_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(database_id), K(index_name), KR(ret));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->get_idx_schema_by_origin_idx_name(database_id,
                                                            index_name,
                                                            simple_table))) {
  } else if (NULL == simple_table) {
    LOG_INFO("table not exist", K(database_id), K(index_name));
  } else if (OB_FAIL(get_schema(TABLE_SCHEMA,
                                simple_table->get_table_id(),
                                table_schema,
                                simple_table->get_schema_version()))) {
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", KR(ret), KP(table_schema));
  }
  return ret;
}

int ObSchemaGetterGuard::get_schema_mgr(const ObSchemaMgr *&schema_mgr) const
{
  int ret = OB_SUCCESS;
  const ObSchemaMgrInfo *schema_mgr_info = NULL;
  schema_mgr = NULL;
  if (OB_FAIL(get_schema_mgr_info( schema_mgr_info))) {
  } else {
    schema_mgr = schema_mgr_info->get_schema_mgr();
    if (OB_ISNULL(schema_mgr)) {
    }
  }
  return ret;
}

int ObSchemaGetterGuard::get_schema_mgr_info(const ObSchemaMgrInfo *&schema_mgr_info) const
{
  int ret = OB_SUCCESS;
  schema_mgr_info = NULL;
  if (schema_mgr_infos_.count() != 1) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("runtime guard must contain exactly one schema manager", KR(ret),
             "schema_mgr_count", schema_mgr_infos_.count());
  } else {
    schema_mgr_info = &schema_mgr_infos_.at(0);
  }
  return ret;
}

int ObSchemaGetterGuard::check_lazy_guard(const ObSchemaMgr *&mgr) const
{
  int ret = OB_SUCCESS;
  mgr = NULL;
  if (OB_FAIL(get_schema_mgr( mgr))) {
  } else if (OB_ISNULL(mgr)) {
    ret = OB_SCHEMA_EAGAIN;
    LOG_WARN("get simple schema in lazy mode not supported", KR(ret));
  }
  return ret;
}

int ObSchemaGetterGuard::get_schema_status(ObRefreshSchemaStatus &schema_status)
{
  int ret = OB_SUCCESS;
  schema_status.reset();
  const ObSchemaMgrInfo *schema_mgr_info = NULL;
  if (OB_FAIL(get_schema_mgr_info( schema_mgr_info))) {
  } else {
    schema_status = schema_mgr_info->get_schema_status();
  }
  return ret;
}

// Check whether the schema guard exposes a formal current schema version.
int ObSchemaGetterGuard::check_formal_guard() const
{
  int ret = OB_SUCCESS;
  int64_t schema_version = OB_INVALID_VERSION;
  if (OB_FAIL(get_schema_version(schema_version))) {
  } else if (OB_CORE_SCHEMA_VERSION + 1 == schema_version
             || ObSchemaService::is_formal_version(schema_version)) {
    // We thought "OB_CORE_SCHEMA_VERSION + 1" is a format schema version, because
    // schema mgr with such schema version is the first complete schema mgr generated in the bootstrap stage.
    ret = OB_SUCCESS;
  } else {
    ret = OB_SCHEMA_EAGAIN;
    LOG_WARN("local schema_version is not formal, try again", KR(ret), K(schema_version));
  }
  return ret;
}


int ObSchemaGetterGuard::get_sys_priv_with_grantee_id(const uint64_t grantee_id,
    ObSysPriv *&sys_priv)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == grantee_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(grantee_id));
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->priv_mgr_.get_sys_priv_in_grantee(grantee_id, sys_priv))) {
  }

  return ret;
}

// check whether the given table has global index or not

// TODO YIREN, remove it when MDS prepare.

int ObSchemaGetterGuard::deep_copy_index_name_map(
    common::ObIAllocator &allocator,
    ObIndexNameMap &index_name_cache)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(const_cast<ObSchemaMgr*>(mgr)
             ->deep_copy_index_name_map(allocator, index_name_cache))) {
  }
  return ret;
}

#define GET_SIMPLE_SCHEMAS_IN_DATABASE_FUNC_DEFINE(SCHEMA, SIMPLE_SCHEMA_TYPE)                       \
  int ObSchemaGetterGuard::get_simple_##SCHEMA##_schemas_in_database(                                \
      const uint64_t database_id,                                                                    \
      common::ObIArray<const SIMPLE_SCHEMA_TYPE*> &schema_array)                                     \
  {                                                                                                  \
    int ret = OB_SUCCESS;                                                                            \
    const ObSchemaMgr *mgr = NULL;                                                                   \
    schema_array.reset();                                                                            \
    if (!check_inner_stat()) {                                                                       \
      ret = OB_INNER_STAT_ERROR;                                                                     \
      LOG_WARN("inner stat error", KR(ret));                                                         \
    } else if (OB_INVALID_ID == database_id) {                                                       \
      ret = OB_INVALID_ARGUMENT;                                                                     \
      LOG_WARN("invalid argument", KR(ret), K(database_id));                                         \
    } else if (OB_FAIL(check_lazy_guard( mgr))) {                                          \
      LOG_WARN("fail to check lazy guard", KR(ret));                             \
    } else if (OB_FAIL(mgr->SCHEMA##_mgr_.get_##SCHEMA##_schemas_in_database(                        \
        database_id, schema_array))) {                                                               \
      LOG_WARN("get "#SCHEMA" schemas in database failed", KR(ret), K(database_id)); \
    }                                                                                                \
    return ret;                                                                                      \
  }

GET_SIMPLE_SCHEMAS_IN_DATABASE_FUNC_DEFINE(outline, ObSimpleOutlineSchema);
GET_SIMPLE_SCHEMAS_IN_DATABASE_FUNC_DEFINE(package, ObSimplePackageSchema);
GET_SIMPLE_SCHEMAS_IN_DATABASE_FUNC_DEFINE(routine, ObSimpleRoutineSchema);
GET_SIMPLE_SCHEMAS_IN_DATABASE_FUNC_DEFINE(mock_fk_parent_table, ObSimpleMockFKParentTableSchema);

int ObSchemaGetterGuard::get_vector_info_index_ids_in_runtime(bool &has_ivf_index,
                                                             ObIArray<uint64_t> &table_ids)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  ObArray<const ObSimpleTableSchemaV2 *> schemas;
  table_ids.reset();
  has_ivf_index = false;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_FAIL(get_schema_mgr( mgr))) {
  } else if (OB_ISNULL(mgr)) {
    ret = OB_SCHEMA_EAGAIN;
    LOG_WARN("get simple schema in lazy mode not supported", KR(ret));
  } else if (OB_FAIL(mgr->get_vector_index_schemas_in_runtime(schemas))) {
  } else {
    FOREACH_CNT_X(schema, schemas, OB_SUCC(ret)) {
      const ObSimpleTableSchemaV2 *tmp_schema = *schema;
      if (OB_ISNULL(tmp_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("NULL ptr", KR(ret), KP(tmp_schema));
      } else if (OB_FAIL(table_ids.push_back(tmp_schema->get_table_id()))) {
      } else if (!has_ivf_index && tmp_schema->is_vec_ivf_index()) {
        has_ivf_index = true;
      }  
    }
  }
  return ret;
}

} //end of namespace schema
} //end of namespace share
} //end of namespace oceanbase
