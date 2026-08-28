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
#include "ob_multi_version_schema_service.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_schema_publish_signal.h"
#include "share/rc/ob_context.h"  // CREATE_WITH_TEMP_ENTITY_P/RESOURCE_OWNER(previously hidden behind a transitive include)
#include "share/ob_schema_status_proxy.h"  // previously hidden behind the ob_server.h include chain,make the dependency explicit
#include "share/ob_rpc_struct.h"
#include "share/ob_share_util.h"
#include "share/config/ob_server_config.h"
#include "lib/atomic/atomic128.h"  // types::uint128_t/LOAD128/CAS128, previously hidden behind the ob_service.h include chain, make the dependency explicit
#include "lib/stat/ob_diagnostic_info_guard.h"  // ObASHSetInnerSqlWaitGuard, previously hidden behind the same include chain, make the dependency explicit
#ifdef __APPLE__
#include <unistd.h> // For useconds_t on macOS
#endif

namespace oceanbase
{
using namespace common;
using namespace common::hash;
using namespace oceanbase::sql;

namespace share
{
namespace schema
{
// Defined in observer/omt/ob_server_runtime_controller.cpp from the schema-slot configuration.
int64_t get_max_schema_slot_num_for_add_schema(const int64_t default_val);


const char *ObMultiVersionSchemaService::print_refresh_schema_mode(const RefreshSchemaMode mode)
{
  const char *mode_str= "UNKNOWN";

  switch (mode) {
    case NORMAL: {
      mode_str = "normal";
      break;
    }
    case FORCE_FALLBACK: {
      mode_str = "force_fallback";
      break;
    }
    case FORCE_LAZY: {
      mode_str = "force_lazy";
      break;
    }
    default: {
      mode_str = "UNKNOWN";
      break;
    }
  }

  return mode_str;
}


///////////////////////////////////////////////////////

#define dbg_construct_task 0

ObSchemaConstructTask::ObSchemaConstructTask()
{
  schema_tasks_.set_attr(ObMemAttr("SchemaTasks", ObCtxIds::SCHEMA_SERVICE));
  (void)pthread_mutex_init(&schema_mutex_, NULL);
  (void)pthread_cond_init(&schema_cond_, NULL);
}

ObSchemaConstructTask::~ObSchemaConstructTask()
{
  (void)pthread_mutex_destroy(&schema_mutex_);
  (void)pthread_cond_destroy(&schema_cond_);
}

ObSchemaConstructTask& ObSchemaConstructTask::get_instance()
{
  static ObSchemaConstructTask task;
  return task;
}

// Schema construction is serialized by version to avoid duplicate cache population.
// blocked if existing same version, or over max parallel size
void ObSchemaConstructTask::cc_before(const int64_t version)
{
  lock();
  if (count() == 0) {
    // leader
  } else {
    do {
      if (exist(version)) {
        wait(version);
      } else {
        break;
      }
    } while (true);
  }

  do {
    if (count() >= MAX_PARALLEL_TASK) {
      wait(version);
    } else {
      add(version);
      unlock();
      break;
    }
  } while (true);
}

// must called after cc_before
void ObSchemaConstructTask::cc_after(const int64_t version)
{
  lock();
  remove(version);
  wakeup(version);
  unlock();
}

void ObSchemaConstructTask::lock()
{
  (void)pthread_mutex_lock(&schema_mutex_);
}

void ObSchemaConstructTask::unlock()
{
  (void)pthread_mutex_unlock(&schema_mutex_);
}

void ObSchemaConstructTask::wait(const int64_t version)
{
  if (dbg_construct_task) {
    LOG_WARN_RET(OB_SUCCESS, "task: waiting", K(version), K(count()));
  }
  // Use portable timed wait with relative timeout (1 second) to avoid clock drift issues on macOS
  int rc = ob_pthread_cond_timedwait_us(&schema_cond_, &schema_mutex_, 1000000 /* 1 second */);
  (void) rc; // make compiler happy
}

void ObSchemaConstructTask::wakeup(const int64_t version)
{
  if (dbg_construct_task) {
    LOG_WARN_RET(OB_SUCCESS, "task: wakingup", K(version), K(count()));
  }
  (void)pthread_cond_broadcast(&schema_cond_);
}

int ObSchemaConstructTask::get_idx(int64_t id)
{
  int hit_idx = -1;
  for (int i = 0; i < schema_tasks_.count(); i++) {
    if (id == schema_tasks_.at(i)) {
      hit_idx = i;
      break;
    }
  }
  return hit_idx;
}

// must protected by mutex
void ObSchemaConstructTask::add(int64_t id)
{
  int ret = OB_SUCCESS;
  if (OB_SUCCESS != (ret = schema_tasks_.push_back(id))) {
  }

  if (dbg_construct_task) {
    LOG_WARN("task: add ", K(id), K(count()));
  }
}

// must protected by mutex
void ObSchemaConstructTask::remove(int64_t id)
{
  int ret = OB_SUCCESS;
  int idx = get_idx(id);
  if (idx != -1) {
    if (OB_SUCCESS != (ret = schema_tasks_.remove(idx))) {
    }
  } else {
    LOG_WARN("failed to get task idx", K(id));
  }

  if (dbg_construct_task) {
    LOG_WARN("task: remove", K(id), K(count()));
  }
}

int ObMultiVersionSchemaService::init_multi_version_schema_struct(
    )
{
  int ret = OB_SUCCESS;
  if (schema_store_.get_refreshed_version() > 0) {
    LOG_INFO("schema store already inited", K(ret));
  } else if (OB_FAIL(schema_store_.init(init_version_cnt_))) {
  }
  return ret;
}

int ObMultiVersionSchemaService::update_schema_cache(
    common::ObIArray<ObTableSchema*> &schema_array)
{
  int ret = OB_SUCCESS;
  LOG_TRACE("update schema cache", K(lbt()));
  for (int64_t i = 0; OB_SUCC(ret) && i < schema_array.count(); ++i) {
    ObTableSchema *table = schema_array.at(i);
    if (OB_ISNULL(table)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("schema is null", KR(ret));
    } else if (OB_FAIL(ObSysTableChecker::fill_sys_index_infos(*table))) {
    } else if (OB_FAIL(schema_cache_.put_schema(TABLE_SCHEMA,
                                                table->get_table_id(),
                                                table->get_schema_version(),
                                                *table))) {
    } else {
    }
  }
  return ret;
}

int ObMultiVersionSchemaService::update_schema_cache(
    common::ObIArray<ObTableSchema> &schema_array)
{
  int ret = OB_SUCCESS;
  LOG_TRACE("update schema cache", K(lbt()));
  for (int64_t i = 0; OB_SUCC(ret) && i < schema_array.count(); ++i) {
    ObTableSchema &table = schema_array.at(i);
    if (OB_FAIL(ObSysTableChecker::fill_sys_index_infos(table))) {
    } else if (OB_FAIL(schema_cache_.put_schema(TABLE_SCHEMA,
                                                table.get_table_id(),
                                                table.get_schema_version(),
                                                table))) {
    } else {
    }
  }
  return ret;
}

int ObMultiVersionSchemaService::update_schema_cache(
    const common::ObIArray<ObServerRuntimeSchema> &schema_array)
{
  int ret = OB_SUCCESS;
  LOG_TRACE("update schema cache", K(lbt()));
  for (int64_t i = 0; OB_SUCC(ret) && i < schema_array.count(); ++i) {
    const ObServerRuntimeSchema &runtime_schema = schema_array.at(i);
    if (OB_FAIL(schema_cache_.put_schema(SERVER_RUNTIME_SCHEMA,
                                         1UL,
                                         runtime_schema.get_schema_version(),
                                         runtime_schema))) {
    } else {
    }
  }
  return ret;
}

int ObMultiVersionSchemaService::update_schema_cache(
    const share::schema::ObSysVariableSchema &schema)
{
  int ret = OB_SUCCESS;
  LOG_TRACE("update schema cache", K(lbt()));
  if (OB_FAIL(schema_cache_.put_schema(SYS_VARIABLE_SCHEMA,
                                       1UL,
                                       schema.get_schema_version(),
                                       schema))) {
  } else {
  }
  return ret;
}

// for ObLatestSchemaGuard
int ObMultiVersionSchemaService::get_latest_schema(
    common::ObIAllocator &allocator,
    const ObSchemaType schema_type,
    const uint64_t schema_id,
    const ObSchema *&schema)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(SYS_VARIABLE_SCHEMA == schema_type && 1UL != schema_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("schema_id not match for SERVER_RUNTIME_SCHEMA",
             KR(ret), K(schema_id));
  } else if (OB_UNLIKELY(!is_normal_schema(schema_type)
             || OB_INVALID_ID == schema_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(schema_type), K(schema_id));
  } else if ((TABLE_SCHEMA == schema_type
              || TABLE_SIMPLE_SCHEMA == schema_type)
             && OB_ALL_CORE_TABLE_TID == schema_id) {
    const ObTableSchema *hard_code_schema = schema_cache_.get_all_core_table();
    if (OB_ISNULL(hard_code_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("all core table schema is null", KR(ret));
    } else {
      schema = hard_code_schema;
    }
  } else {
    ObRefreshSchemaStatus schema_status;
    
    const int64_t schema_version = INT64_MAX;
    ObSchema *new_schema = NULL;
    if (OB_FAIL(schema_fetcher_.fetch_schema(schema_type,
                                             schema_status,
                                             schema_id,
                                             schema_version,
                                             allocator,
                                             new_schema))) {
    } else if (OB_ISNULL(new_schema)) {
      // schema not exist or schema history is recycled.
    } else if (TABLE_SCHEMA != schema_type) {
      schema = new_schema;
    } else {
      ObTableSchema *new_table = static_cast<ObTableSchema *>(new_schema);
      if (OB_ALL_CORE_TABLE_TID == schema_id) {
        // do-nothing
      } else if (!need_construct_aux_infos_(*new_table)) {
        // do-nothing
      } else if (ObSysTableChecker::is_sys_table_has_index(schema_id)) {
        if (OB_FAIL(ObSysTableChecker::fill_sys_index_infos(*new_table))) {
        }
      } else if (OB_FAIL(construct_aux_infos_(*sql_proxy_,
                 schema_status, *new_table))) {
      }
      if (OB_SUCC(ret)) {
        schema = static_cast<const ObSchema*>(new_table);
      }
    }
  }
  return ret;
}

// Keep special system table/index cache updates in the upper layer and use the
// schema guard to obtain the server runtime schema.
// Whether it is lazy mode is distinguished by whether mgr is NULL
int ObMultiVersionSchemaService::get_schema(const ObSchemaMgr *mgr,
                                            const ObRefreshSchemaStatus &schema_status,
                                            const ObSchemaType schema_type,
                                            const uint64_t schema_id,
                                            const int64_t schema_version,
                                            ObKVCacheHandle &handle,
                                            const ObSchema *&schema)
{
  int ret = OB_SUCCESS;
  const bool is_lazy = (NULL == mgr);
  
  bool update_history_cache = false;
  schema = NULL;
  if (SYS_VARIABLE_SCHEMA == schema_type && 1UL != schema_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("schema_id not match for SERVER_RUNTIME_SCHEMA",
             KR(ret), K(schema_id));
  } else if (TABLE_SIMPLE_SCHEMA == schema_type) {
    // Simple table schemas are not available in the server runtime cache.
    ret = OB_SCHEMA_EAGAIN;
    LOG_WARN("fail to get simple table", K(ret),
             KP(mgr), K(schema_id), K(schema_version));
  } else if ((TABLE_SCHEMA == schema_type || TABLE_SIMPLE_SCHEMA == schema_type)
             && OB_ALL_CORE_TABLE_TID == schema_id) {
    const ObTableSchema *hard_code_schema = schema_cache_.get_all_core_table();
    if (OB_ISNULL(hard_code_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("all core table schema is null", KR(ret));
    } else {
      schema = hard_code_schema;
    }
  } else if (OB_FAIL(schema_cache_.get_schema(schema_type,
                                              schema_id,
                                              schema_version,
                                              handle,
                                              schema))) {
    if (ret != OB_ENTRY_NOT_EXIST) {
      LOG_WARN("get schema from cache failed", K(1UL), K(schema_type), K(schema_id),
               K(schema_version), K(ret));
    } else {
      // fetch schema and renew cache
      ret = OB_SUCCESS;

      ObSchema *tmp_schema = NULL;
      ObArenaAllocator allocator(ObModIds::OB_TEMP_VARIABLES);
      bool has_hit = false;
      // Use this to mark whether the schema exists in the specified version
      bool not_exist = false;

      // 1. Query the version history dictionary table
      if (OB_FAIL(ret)) {
      } else if (is_lazy
                 && (SERVER_RUNTIME_SCHEMA == schema_type
                     || TABLE_SCHEMA == schema_type
                     || TABLE_SIMPLE_SCHEMA == schema_type
                     || DATABASE_SCHEMA == schema_type)) {
        ObSchemaType fetch_schema_type = TABLE_SIMPLE_SCHEMA == schema_type ? TABLE_SCHEMA : schema_type;
        VersionHisKey key(fetch_schema_type, schema_id);
        VersionHisVal val;
        if (OB_FAIL(get_schema_version_history(schema_status, schema_version,
                                               key, val, not_exist))) {
        }
        if (OB_SUCC(ret) && !not_exist) {
          int i = 0;
          int64_t precise_version = OB_INVALID_VERSION;
          for (; i < val.valid_cnt_; ++i) {
            if (val.versions_[i] <= schema_version) {
              break;
            }
          }
          if (i < val.valid_cnt_) {
            if (0 == i && val.is_deleted_) {
              not_exist = true;
              LOG_INFO("schema has been deleted under specified version", KR(ret),
                       K(key), K(val), K(schema_version), K(precise_version));
            } else {
              // Access cache with accurate version
              precise_version = val.versions_[i];
            }
          } else if (schema_version < val.min_version_) {
            not_exist = true;
            LOG_INFO("schema has not been created under specified version",
                     KR(ret), K(key), K(val), K(schema_version));
          } else if (schema_version == val.min_version_) {
            precise_version = val.min_version_;
            LOG_INFO("use min schema version as precise schema version",
                     KR(ret), K(key), K(val), K(schema_version));
          } else {
            // i >= cnt && schema_version > val.min_version_
            // try use discrete schema version relationship
            if (OB_FAIL(schema_cache_.get_schema_history_cache(
                schema_type, schema_id, schema_version, precise_version))) {
              if (OB_ENTRY_NOT_EXIST != ret) {
                LOG_WARN("get schema history cache failed",
                         KR(ret), K(schema_type), K(schema_id), K(schema_version));
              } else {
                ret = OB_SUCCESS;
                update_history_cache = true;
                LOG_INFO("precise version not founded since schema version is too old, " \
                         "will retrieve it from inner table", KR(ret), K(key), K(val),
                         K(schema_version), "schema_type", schema_type_str(schema_type));
              }
            }
          }

          // try use precise_version
          if (OB_SUCC(ret) && precise_version > 0) {
            if (OB_FAIL(schema_cache_.get_schema(schema_type,
                                                 schema_id,
                                                 precise_version,
                                                 handle,
                                                 schema))) {
              if (ret != OB_ENTRY_NOT_EXIST) {
                LOG_WARN("get schema from cache failed", KR(ret), K(key),
                         K(schema_version), K(precise_version));
              } else {
                ret = OB_SUCCESS;
              }
            } else {
              LOG_TRACE("precise version hit", K(key), K(schema_version), K(precise_version),
                       K(schema_id), "schema_type", schema_type_str(schema_type));
              has_hit = true;
            }
          }
        }
      }

      // 2. Query inner table
      if (OB_SUCC(ret) && !not_exist && !has_hit) {
        if (OB_FAIL(schema_fetcher_.fetch_schema(schema_type,
                                                 schema_status,
                                                 schema_id,
                                                 schema_version,
                                                 allocator,
                                                 tmp_schema))) {
        } else if (OB_ISNULL(tmp_schema)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("NULL ptr", K(schema_type), K(schema_id),
                   K(schema_version), KP(tmp_schema), K(ret));
        } else if (TABLE_SCHEMA == schema_type) {
          ObTableSchema *table_schema = static_cast<ObTableSchema *>(tmp_schema);
          // process index
          if (OB_ALL_CORE_TABLE_TID == schema_id) {
            // do-nothing
          } else if (!need_construct_aux_infos_(*table_schema)) {
            // do-nothing
          } else if (ObSysTableChecker::is_sys_table_has_index(schema_id)) {
            if (OB_FAIL(ObSysTableChecker::fill_sys_index_infos(*table_schema))) {
            }
          } else if (is_lazy) {
            if (OB_FAIL(construct_aux_infos_(
                *sql_proxy_, schema_status, *table_schema))) {
            }
          } else {
            if (OB_FAIL(add_aux_schema_from_mgr(*mgr, *table_schema, USER_INDEX))) {
            } else if (OB_FAIL(add_aux_schema_from_mgr(*mgr, *table_schema, AUX_LOB_META))) {
            } else if (OB_FAIL(add_aux_schema_from_mgr(*mgr, *table_schema, AUX_LOB_PIECE))) {
            }
          }
        }

        // 3. convert schema_version to raise cache hit ratio
        int64_t precise_version = schema_version;
        if (OB_FAIL(ret)) {
        } else if (OB_ISNULL(tmp_schema)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("NULL ptr", K(schema_id), KP(tmp_schema), K(ret));
        } else if (TABLE_SCHEMA == schema_type) {
          ObTableSchema *table_schema = static_cast<ObTableSchema *>(tmp_schema);
          precise_version = table_schema->get_schema_version();
          // add debug info
          if (ObSysTableChecker::is_sys_table_has_index(table_schema->get_table_id())) {
            ObTaskController::get().allow_next_syslog();
            LOG_INFO("fetch sys table schema with index", KR(ret),
                     K(schema_status), K(schema_id),
                     K(schema_version), K(precise_version),
                     "schema_mgr_version", OB_ISNULL(mgr) ? 0 : mgr->get_schema_version(),
                     "table_name", table_schema->get_table_name(),
                     "index_cnt", table_schema->get_index_tid_count());
          }
          if (is_system_table(table_schema->get_table_id())) {
            LOG_TRACE("fetch sys table schema with lob", KR(ret),
                      K(schema_status), K(schema_id),
                      K(schema_version), K(precise_version),
                      "schema_mgr_version", OB_ISNULL(mgr) ? 0 : mgr->get_schema_version(),
                      "table_name", table_schema->get_table_name(),
                      "lob_meta_table_id", table_schema->get_aux_lob_meta_tid(),
                      "lob_piece_table_id", table_schema->get_aux_lob_piece_tid());
          }
        } else if (TABLE_SIMPLE_SCHEMA == schema_type) {
          ObSimpleTableSchemaV2 *table_schema = static_cast<ObSimpleTableSchemaV2 *>(tmp_schema);
          precise_version = table_schema->get_schema_version();
        } else if (SERVER_RUNTIME_SCHEMA == schema_type) {
          ObServerRuntimeSchema *runtime_schema = static_cast<ObServerRuntimeSchema *>(tmp_schema);
          precise_version = runtime_schema->get_schema_version();
        } else if (DATABASE_SCHEMA == schema_type) {
          ObDatabaseSchema *database_schema = static_cast<ObDatabaseSchema *>(tmp_schema);
          precise_version = database_schema->get_schema_version();
        }

        // 4. renew cache
        if (FAILEDx(schema_cache_.put_and_fetch_schema(
                    schema_type,
                    schema_id,
                    precise_version,
                    *tmp_schema,
                    handle,
                    schema))) {
          LOG_WARN("put and fetch schema failed", K(1UL), K(schema_type),
                   K(schema_id), K(precise_version), K(schema_version), KR(ret));
        } else if (update_history_cache
                   && OB_FAIL(schema_cache_.put_schema_history_cache(
                      schema_type, schema_id, schema_version, precise_version))) {
          LOG_WARN("fail to put schema history cache", KR(ret), K(schema_type),
                   K(1UL), K(schema_id), K(schema_version), K(precise_version));
        }

#ifndef NDEBUG
        if (OB_SUCC(ret) && is_lazy) {
          // The expectation of lazy mode is to use the specific schema's schema_version to take guard.
          // add a check to see if there is any usage that does not match the expected behavior.
          if (TABLE_SCHEMA == schema_type
              || TABLE_SIMPLE_SCHEMA == schema_type
              || SERVER_RUNTIME_SCHEMA == schema_type
              || DATABASE_SCHEMA == schema_type) {
            if (precise_version != schema_version) {
              LOG_INFO("schema_version not match in lazy mode", K(ret),
                       K(precise_version), K(schema_version), K(schema_id), K(schema_type));
            }
          } else {
            LOG_INFO("schema_type not match in lazy mode", K(ret),
                     K(schema_version), K(schema_id), K(schema_type));
          }
        }
#endif
      }
    }
  }
  return ret;
}

int ObMultiVersionSchemaService::add_aux_schema_from_mgr(
    const ObSchemaMgr &mgr,
    ObTableSchema &table_schema,
    const ObTableType table_type)
{
  int ret = OB_SUCCESS;
  ObSEArray<const ObSimpleTableSchemaV2 *, 8> simple_aux_tables;
  if (OB_FAIL(mgr.get_aux_schemas(
              table_schema.get_table_id(), simple_aux_tables, table_type))) {
  } else {
    FOREACH_CNT_X(tmp_simple_aux_table, simple_aux_tables, OB_SUCC(ret)) {
      const ObSimpleTableSchemaV2 *simple_aux_table = *tmp_simple_aux_table;
      if (OB_ISNULL(simple_aux_table)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("NULL ptr", K(ret));
      } else {
        if (simple_aux_table->is_index_table()) {
          if (OB_FAIL(table_schema.add_simple_index_info(ObAuxTableMetaInfo(
                     simple_aux_table->get_table_id(),
                     simple_aux_table->get_table_type(),
                     simple_aux_table->get_index_type())))) {
          }
        } else if (simple_aux_table->is_aux_lob_meta_table()) {
          table_schema.set_aux_lob_meta_tid(simple_aux_table->get_table_id());
        } else if (simple_aux_table->is_aux_lob_piece_table()) {
          table_schema.set_aux_lob_piece_tid(simple_aux_table->get_table_id());
        } else {
          ret = OB_ERR_UNEXPECTED;
          LOG_ERROR("unexpected", K(ret));
        }
      }
    }
  }

  return ret;
}

/**
 * put fallback schema to slot and check need to switch allocator
 */
int ObMultiVersionSchemaService::put_fallback_schema_to_slot(ObSchemaMgr *&new_mgr,
                                                             ObSchemaMgrCache &schema_mgr_cache,
                                                             ObSchemaMemMgr &schema_mem_mgr,
                                                             ObSchemaMgrHandle &handle)
{
  int ret = OB_SUCCESS;
  ObSchemaMgr *eli_schema_mgr = NULL;
  const int64_t start_time = ObTimeUtility::current_time();
  if (OB_FAIL(schema_mgr_cache.put(new_mgr, eli_schema_mgr, &handle))) {
  } else {
    int64_t cost = ObTimeUtility::current_time() - start_time;
    LOG_INFO("put schema mgr succeed", K(cost),
             "schema_version", new_mgr->get_schema_version(),
             "eliminated_schema_version", NULL != eli_schema_mgr ?
                 eli_schema_mgr->get_schema_version() : OB_INVALID_VERSION);
    if (OB_FAIL(schema_mem_mgr.free_schema_mgr(eli_schema_mgr))) {
    } else {
      // A reconstructed historical schema_mgr owns an independent allocator, so it does
      // not need to be released through switch_allocator.
    }
  }
  return ret;
}

// Obtain a schema guard at a specific runtime schema version, or the latest
// local version when runtime_schema_version is not specified.
int ObMultiVersionSchemaService::get_runtime_schema_guard(
    ObSchemaGetterGuard &guard,
    int64_t runtime_schema_version/* = common::OB_INVALID_VERSION*/,
    const RefreshSchemaMode refresh_schema_mode /* = RefreshSchemaMode::NORMAL */)
{
  int ret = OB_SUCCESS;
  int64_t latest_local_version = OB_INVALID_VERSION;
  int64_t snapshot_version = OB_INVALID_VERSION;
  int64_t baseline_schema_version = OB_INVALID_VERSION;
  ObRefreshSchemaStatus schema_status;
  ObSchemaStore* schema_store = NULL;
  if (OB_FAIL(guard.fast_reset())) {
  } else if (OB_FAIL(guard.init())) {
  }
  

  if (OB_FAIL(ret)) {
  } else if (FALSE_IT(schema_store = &schema_store_)) {
  } else if (OB_INVALID_VERSION == (latest_local_version = schema_store->get_refreshed_version())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("refreshed schema version is invalid", K(ret), K(latest_local_version), K(runtime_schema_version));
  } else if (runtime_schema_version > latest_local_version) {
    ret = OB_SCHEMA_EAGAIN;
    LOG_WARN("specified schema version larger than latest schema version, need retry",
             KR(ret), K(runtime_schema_version), K(latest_local_version));
  } else {
    snapshot_version = OB_INVALID_VERSION == runtime_schema_version ?
                       latest_local_version : runtime_schema_version;
    if (OB_INVALID_VERSION != runtime_schema_version) {
      // for max avaliablity, ignore tmp_ret
      int tmp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (tmp_ret = get_baseline_schema_version(
          false/*auto_update*/, baseline_schema_version))) {
      }
    }
    if (OB_INVALID_VERSION != baseline_schema_version
        && OB_INVALID_VERSION != runtime_schema_version
        && runtime_schema_version < baseline_schema_version) {
      LOG_INFO("change runtime schema version to baseline",
               K(runtime_schema_version),
               "baseline version", baseline_schema_version);
      snapshot_version = baseline_schema_version;
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(add_schema_mgr_info(guard, schema_store, schema_status, snapshot_version, latest_local_version,
               refresh_schema_mode))) {
    } else {
      guard.schema_service_ = this;
      guard.schema_guard_type_ = ObSchemaGetterGuard::RUNTIME_SCHEMA_GUARD;
    }
  }

  if (OB_SUCC(ret)) {
  
    
  }

  return ret;
}


int ObMultiVersionSchemaService::get_full_runtime_schema_guard(
    ObSchemaGetterGuard &guard,
    bool check_formal /*= true*/)
{
  int ret = OB_SUCCESS;

  if (!is_runtime_schema_ready()) {
    ret = OB_SCHEMA_EAGAIN;
    if (EXECUTE_COUNT_PER_SEC(1)) {
      LOG_WARN("runtime schema is not ready", K(ret));
    }
  } else if (OB_FAIL(get_runtime_schema_guard(guard))) {
  } else if (check_formal && OB_FAIL(guard.check_formal_guard())) {
    LOG_WARN("schema_guard is not formal", K(ret));
  }
  return ret;
}

int ObMultiVersionSchemaService::get_runtime_schema_guard_with_version_in_inner_table(
    ObSchemaGetterGuard &schema_guard)
{
  int ret = OB_SUCCESS;
  int64_t version_in_inner_table = OB_INVALID_VERSION;
  ObRefreshSchemaStatus schema_status;
  common::ObMySQLProxy *sql_proxy = get_sql_proxy();
  if (OB_ISNULL(sql_proxy)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sql_proxy is null", K(ret));
  }
  if (OB_FAIL(ret)) {
  } else {
    
    if (OB_FAIL(get_schema_version_in_inner_table(*sql_proxy, schema_status, version_in_inner_table))) {
    } else if (OB_FAIL(get_runtime_schema_guard(schema_guard, version_in_inner_table))) {
      if (OB_SCHEMA_EAGAIN == ret) {
        int t_ret = OB_SUCCESS;
        if (OB_SUCCESS != (t_ret = refresh_and_add_schema())) {
        } else if (OB_FAIL(get_runtime_schema_guard(schema_guard, version_in_inner_table))) {
        }
      } else {
        LOG_WARN("get schema manager failed", K(ret));
      }
    }
  }
  return ret;
}

int ObMultiVersionSchemaService::construct_fallback_schema_mgr_(
    ObSchemaStore *schema_store,
    const ObRefreshSchemaStatus &schema_status,
    const int64_t target_version,
    const int64_t latest_local_version,
    const ObSchemaMgr *&schema_mgr,
    ObSchemaMgrHandle &handle)
{
  int ret = OB_SUCCESS;
  // serialize concurrent reconstruction of the same version (MAX_PARALLEL_TASK == 1)
  ObSchemaConstructTask &task = ObSchemaConstructTask::get_instance();
  task.cc_before(target_version);
  // Fallback managers are allocated from mem_mgr_'s rotating arenas.  Keep the
  // whole reconstruction under the same lock as normal schema refresh and
  // arena reclamation so no refresh can rotate/reset the arena concurrently.
  // Acquire this lock after cc_before: same-version waiters must not hold it
  // while waiting for the constructing task to call cc_after().
  lib::ObMutexGuard refresh_guard(schema_refresh_mutex_);
  ObSchemaMgrCache *schema_mgr_cache = NULL;
  ObSchemaMemMgr *mem_mgr = mem_mgr_;
  if (OB_ISNULL(schema_store) || OB_ISNULL(mem_mgr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_store or mem_mgr is null", KR(ret), KP(schema_store), KP(mem_mgr));
  } else if (FALSE_IT(schema_mgr_cache = &schema_store->schema_mgr_cache_)) {
  } else if (OB_FAIL(schema_mgr_cache->get(target_version, schema_mgr, handle))) {
    // double-check: another thread may have built the slot while we waited on cc_before
    if (OB_ENTRY_NOT_EXIST != ret) {
      LOG_WARN("get schema mgr failed", KR(ret), K(target_version));
    } else if (!is_runtime_schema_ready()) {
      ret = OB_SCHEMA_EAGAIN;
      LOG_WARN("full schema is not ready, can't construct fallback schema guard",
               KR(ret), K(schema_status), K(target_version));
    } else {
      // cache miss: reconstruct the evicted historical schema_mgr
      FLOG_INFO("[FALLBACK_SCHEMA] schema mgr cache miss, reconstruct",
                K(schema_status), K(target_version), K(latest_local_version));
      const ObSchemaMgr *src_mgr = NULL;
      ObSchemaMgrHandle src_mgr_handle;
      bool need_latest = false;
      // for faster fallback, start from the schema_mgr with the nearest version
      if (OB_FAIL(schema_mgr_cache->get_nearest(target_version, src_mgr, src_mgr_handle))) {
        if (OB_ENTRY_NOT_EXIST != ret) {
          LOG_WARN("get_nearest schema_mgr failed", KR(ret), K(schema_status), K(target_version));
        } else {
          need_latest = true;
          ret = OB_SUCCESS;
        }
      } else if (OB_ISNULL(src_mgr)
                 || llabs(src_mgr->get_schema_version() - target_version)
                      > llabs(latest_local_version - target_version)) {
        // the latest slot is closer to target than the nearest cached one
        need_latest = true;
      }
      if (OB_SUCC(ret) && need_latest) {
        src_mgr_handle.reset();
        if (OB_FAIL(schema_mgr_cache->get(latest_local_version, src_mgr, src_mgr_handle))) {
        } else if (OB_ISNULL(src_mgr)) {
          ret = OB_SCHEMA_ERROR;
          LOG_WARN("src_mgr is null", KR(ret), K(schema_status), K(target_version));
        }
      }
      if (OB_SUCC(ret)) {
        ObSchemaMgr *new_mgr = NULL;
        const int64_t from_version = src_mgr->get_schema_version();
        if (OB_FAIL(mem_mgr->alloc_schema_mgr(new_mgr))) {
        } else if (OB_ISNULL(new_mgr)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("new_mgr is null", KR(ret), K(from_version), K(target_version));
        } else if (OB_FAIL(new_mgr->init())) {
        } else if (OB_FAIL(new_mgr->deep_copy(*src_mgr))) {
        } else if (FALSE_IT(src_mgr_handle.reset())) {  // release borrowed source after the copy
        } else if (OB_FAIL(fallback_schema_mgr(schema_status, *new_mgr, target_version))) {
        } else if (OB_FAIL(put_fallback_schema_to_slot(new_mgr, *schema_mgr_cache, *mem_mgr, handle))) {
        } else {
          schema_mgr = new_mgr;
          FLOG_INFO("[FALLBACK_SCHEMA] reconstruct fallback schema mgr finish",
                    K(schema_status), K(from_version), K(target_version));
        }
        if (OB_FAIL(ret)) {
          int tmp_ret = OB_SUCCESS;
          schema_mgr = NULL;
          if (OB_TMP_FAIL(mem_mgr->free_schema_mgr(new_mgr))) {
          }
        }
      }
    }
  }
  task.cc_after(target_version);
  return ret;
}

int ObMultiVersionSchemaService::add_schema_mgr_info(
    ObSchemaGetterGuard &schema_guard,
    ObSchemaStore* schema_store,
    const ObRefreshSchemaStatus &schema_status,
    const int64_t snapshot_version,
    const int64_t latest_local_version,
    const RefreshSchemaMode refresh_schema_mode /* = RefreshSchemaMode::NORMAL */)
{
  int ret = OB_SUCCESS;
  const ObSchemaMgr *schema_mgr = NULL;
  ObSchemaMgrInfo* new_schema_mgr_info = NULL;
  if (snapshot_version <= 0
      || latest_local_version <= 0
      || NULL == schema_store) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument or snapshot_version", K(ret), K(snapshot_version), KP(schema_store));
  } else {
    ObSchemaMgrHandle handle(schema_guard.mod_);
    ObSchemaMgrInfo schema_mgr_info(snapshot_version,
                                    schema_mgr,
                                    handle,
                                    schema_status);
    int64_t count = schema_guard.schema_mgr_infos_.count();
    // Guaranteed to be monotonically increasing when inserted
    if (OB_FAIL(schema_guard.schema_mgr_infos_.push_back(schema_mgr_info))) {
    } else {
      new_schema_mgr_info = &schema_guard.schema_mgr_infos_.at(count);
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(new_schema_mgr_info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_mgr is null", KR(ret));
  } else {
    ObSchemaMgrHandle& handle = new_schema_mgr_info->get_schema_mgr_handle();
    if (RefreshSchemaMode::FORCE_FALLBACK == refresh_schema_mode) {
      // The requested historical version may have aged out of the live schema_mgr_cache;
      // reconstruct it instead of falling into NULL/lazy mode (which would make the
      // change stream async index retry on OB_SCHEMA_EAGAIN forever).
      if (OB_FAIL(construct_fallback_schema_mgr_(schema_store, schema_status, snapshot_version,
          latest_local_version, schema_mgr, handle))) {
      }
    } else if (OB_FAIL(schema_store->schema_mgr_cache_.get(snapshot_version, schema_mgr, handle))) {
      if (OB_ENTRY_NOT_EXIST != ret) {
        LOG_WARN("get schema mgr failed", K(ret), K(snapshot_version));
      } else {
        ret = OB_SUCCESS;
      }
    }
    if (OB_SUCC(ret)) {
      new_schema_mgr_info->set_schema_mgr(schema_mgr);
      if (snapshot_version == latest_local_version && OB_ISNULL(schema_mgr)) {
        LOG_INFO("should not be lazy mode", K(ret), KPC(new_schema_mgr_info), K(latest_local_version));
      }
    }
  }

  return ret;
}

// Resolve the nearest usable schema version when a compaction snapshot refers
// to a table version that is no longer present in the current schema cache.
int ObMultiVersionSchemaService::retry_get_schema_guard(const int64_t schema_version,
    const uint64_t table_id,
    ObSchemaGetterGuard &schema_guard,
    int64_t &save_schema_version)
{
  int ret = OB_SUCCESS;

  int32_t retry_time = 0;
  const ObTableSchema *table_schema = NULL;
  save_schema_version = schema_version;

  if (!ObSchemaService::is_formal_version(schema_version)
      || 0 == schema_version) {
    // There are several special versions of schema_version, here only warning is printed, and the version is not verified
    // 1. schema_version = 0 : Before 223, the partition did not record the schema_version,
    //  and the minor free will use this value to get the schema;
    // 2. schema_version = 1 : 225 The demand for schema_history recovery point calculation.
    //  Merger will increase the partition schema_version; version 1 is retained for historical data.
    // 3. schema_version = 2 : The first schema_version version of bootstrap (only memory is used temporarily);
    // 4. informal version: system-table/core-table changes.
    // Versions 0~2 are converted to baseline_schema_version.
    // Here the defense is removed first, only print warn
    LOG_WARN("get schema guard with informal version", K(table_id), K(schema_version));
  }
  if (OB_SUCC(ret)) {
    while (retry_time < MAX_RETRY_TIMES) {
      if (OB_FAIL(get_runtime_schema_guard(schema_guard, schema_version))) {
        if (OB_SCHEMA_EAGAIN != ret) {
          LOG_WARN("fail to get runtime schema guard", K(ret), K(table_id), K(schema_version));
        }
      }
      if (OB_SCHEMA_EAGAIN != ret && OB_NOT_INIT != ret) {
        break;
      } else {
        ob_usleep(RETRY_INTERVAL_US);
        ++retry_time;
      }
    }
    if (OB_FAIL(ret)) {
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(schema_guard.get_table_schema( table_id, table_schema))) {
  } else if (OB_NOT_NULL(table_schema)) {
    // success
  } else {
    // table not exist , return guard which can get original table schema
    ObRefreshSchemaStatus schema_status;
    

    if (OB_FAIL(ret)) {
    } else if (is_inner_table(table_id)) {
      int64_t baseline_schema_version = OB_INVALID_VERSION;
      if (OB_FAIL(get_baseline_schema_version(false/*auto_update*/, baseline_schema_version))) {
      } else if (baseline_schema_version <= 0) {
        ret = OB_SCHEMA_EAGAIN;
        LOG_WARN("baseline schema version is invalid, try later",
                 K(ret), K(table_id), K(schema_version));
      } else {
        // try use version_his_map
        VersionHisKey key(TABLE_SCHEMA, table_id);
        VersionHisVal val;
        int ret = version_his_map_.get_refactored(key, val);
        if (OB_SUCCESS != ret && OB_HASH_NOT_EXIST != ret) {
          LOG_WARN("fail to get table version history", K(ret), K(key), K(schema_version));
        } else if (OB_HASH_NOT_EXIST == ret) { // overwrite ret
          int64_t local_version = OB_INVALID_VERSION;
          if (OB_FAIL(get_runtime_refreshed_schema_version(local_version))) {
          } else if (local_version <= OB_CORE_SCHEMA_VERSION) {
            ret = OB_SCHEMA_EAGAIN;
            LOG_WARN("local schema is old, try later",
                     K(ret), K(key), K(schema_version), K(local_version));
          } else if (OB_FAIL(construct_schema_version_history(
                             schema_status, local_version, key, val))) {
          } else if (0 >= val.min_version_ || 0 == val.valid_cnt_) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("sys table not exist", K(ret),
                     K(key), K(val), K(schema_version));
          } else {
            save_schema_version = max(val.min_version_, baseline_schema_version);
          }
        } else {
          save_schema_version = max(val.min_version_, baseline_schema_version);
        }
      }
    } else {
      // try use orig_schema_version
      if (OB_FAIL(ret)) {
      } else if (NULL == schema_service_) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("schema_service_ is null", K(ret));
      } else if (OB_FAIL(schema_service_->get_ori_schema_version(
                         schema_status, table_id, save_schema_version))) {
        if (OB_ITER_END == ret) {
          // There are several situations where orig_schema_version cannot be obtained:
          // 1. The table built in 1.4.x does not have orig_schema_version,
          //  and the following situations occur when entering this branch:
          //    - The schema_version is derived from major or minor freeze and is a relatively large value.
          //      At this time, the table has been dropped, and errors will continue to be reported
          //      until the partition is recycled by the GC logic;
          //    - BUG, should not enter this branch.
          // 2. 2.0 and 2.1.x concurrently build tables, the transaction is not committed and needs to be tried again.
          ret = OB_SCHEMA_EAGAIN;
          LOG_WARN("orig_schema_version not exist, try again",
                   K(ret), K(table_id), K(schema_version));
        } else {
          LOG_WARN("failed to get_ori_schema_version", K(ret), K(save_schema_version), K(table_id));
        }
      } else if (schema_version > save_schema_version) {
        // If the specified version is greater than orig_schema_version, the table can be determined to be deleted
        ret = OB_TABLE_IS_DELETED;
        ObTaskController::get().allow_next_syslog();
        LOG_INFO("table is deleted",K(ret), K(table_id), K(schema_version), K(save_schema_version));
      } else {}
    }
    if (OB_SUCC(ret)) {
      while (retry_time < MAX_RETRY_TIMES) {
        if (OB_FAIL(get_runtime_schema_guard(schema_guard, save_schema_version))) {
          if (OB_SCHEMA_EAGAIN != ret) {
            LOG_WARN("fail to get runtime schema guard",
                     K(ret), K(table_id), K(schema_version), K(save_schema_version));
          }
        }
        if (OB_SCHEMA_EAGAIN != ret && OB_NOT_INIT != ret) {
          break;
        } else {
          ob_usleep(RETRY_INTERVAL_US);
          ++retry_time;
        }
      }
      if (OB_FAIL(ret)) {
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(schema_guard.get_table_schema( table_id, table_schema))) {
      } else if (OB_ISNULL(table_schema)) {
        ret = OB_SCHEMA_ERROR;
        LOG_WARN("table should exist",
                 K(ret), K(table_id), K(schema_version), K(save_schema_version));
      }
    }
  }
  if (OB_SUCC(ret)) {
    schema_guard.schema_guard_type_ = ObSchemaGetterGuard::TABLE_SCHEMA_GUARD;
  }
  return ret;
}

ObMultiVersionSchemaService::ObMultiVersionSchemaService() :
    init_(false),
    schema_refresh_scheduler_(NULL),
    schema_publish_signal_(NULL),
    schema_refresh_mutex_(common::ObLatchIds::REFRESH_SCHEMA_LOCK),
    schema_cache_(),
    schema_mgr_cache_(),
    schema_fetcher_(),
    schema_info_rwlock_(common::ObLatchIds::REFRESHED_SCHEMA_CACHE_LOCK),
    last_refreshed_schema_info_(),
    init_version_cnt_(OB_INVALID_COUNT)
{
}

ObMultiVersionSchemaService::~ObMultiVersionSchemaService()
{
  destroy();
}

void ObMultiVersionSchemaService::stop()
{
  ddl_trans_controller_.stop();
}

void ObMultiVersionSchemaService::wait()
{
  ddl_trans_controller_.wait();
}

int ObMultiVersionSchemaService::destroy()
{
  int ret = OB_SUCCESS;
  ddl_trans_controller_.destroy();
  schema_cache_.destroy();
  schema_service_ = NULL;
  schema_refresh_scheduler_ = NULL;
  schema_publish_signal_ = NULL;
  init_ = false;
  return ret;
}

ObMultiVersionSchemaService &ObMultiVersionSchemaService::get_instance()
{
  static ObMultiVersionSchemaService THE_ONE;
  return THE_ONE;
}

// init in main thread
int ObMultiVersionSchemaService::init(
    ObMySQLProxy *sql_proxy,
    const ObCommonConfig *config,
    ObSchemaStatusProxy &schema_status_proxy,
    const ObServiceStatus &service_status,
    bool &in_bootstrap,
    const int64_t init_version_count,
    ObSchemaService &schema_backend,
    ObISchemaRefreshScheduler &schema_refresh_scheduler,
    ObSchemaPublishSignal &schema_publish_signal)
{
  int ret = OB_SUCCESS;

  if (true == init_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init schema manager twice, ", K(ret));
  } else if (FALSE_IT(schema_refresh_scheduler_ = &schema_refresh_scheduler)) {
  } else if (FALSE_IT(schema_publish_signal_ = &schema_publish_signal)) {
  } else if (OB_FAIL(ObServerSchemaService::init(
      sql_proxy, config, schema_status_proxy, service_status,
      in_bootstrap, schema_backend))) {
  } else if (OB_FAIL(schema_fetcher_.init(schema_service_, sql_proxy))) {
  } else if (OB_FAIL(schema_cache_.init())) {
  } else if (OB_FAIL(schema_mgr_cache_.init(init_version_count))) {
  } else if (OB_FAIL(ddl_trans_controller_.init(this))) {
  } else if (OB_FAIL(ddl_epoch_mgr_.init(sql_proxy, this))) {
  } else {
    // init sys schema struct
    init_version_cnt_ = init_version_count;
    if (OB_FAIL(init_multi_version_schema_struct())) {
    } else if (OB_FAIL(init_system_runtime_user_schema())) {
    } else if (OB_FAIL(init_original_schema())) {
    }
  }

  return ret;
}

bool ObMultiVersionSchemaService::check_inner_stat() const
{
  bool ret = true;
  if (!ObServerSchemaService::check_inner_stat()
      || OB_ISNULL(schema_refresh_scheduler_)
      || OB_ISNULL(schema_publish_signal_)
      || !init_) {
    ret = false;
    LOG_WARN("inner stat error", K(init_), KP_(schema_refresh_scheduler),
             KP_(schema_publish_signal));
  }
  return ret;
}

// Seed the runtime, user, and system-variable schemas used during bootstrap.
int ObMultiVersionSchemaService::init_system_runtime_user_schema()
{
  int ret = OB_SUCCESS;

  ObServerRuntimeSchema runtime_schema;
  SMART_VAR(ObSysVariableSchema, sys_variable) {
    HEAP_VAR(ObUserInfo, sys_user) {


      runtime_schema.set_schema_version(OB_CORE_SCHEMA_VERSION);

      
      sys_user.set_user_id(OB_SYS_USER_ID);
      sys_user.set_priv_set(OB_PRIV_ALL | OB_PRIV_GRANT | OB_PRIV_BOOTSTRAP);
      sys_user.set_schema_version(OB_CORE_SCHEMA_VERSION);

      
      sys_variable.set_schema_version(OB_CORE_SCHEMA_VERSION);
      sys_variable.set_name_case_mode(OB_LOWERCASE_AND_INSENSITIVE);

      if (OB_FAIL(sys_variable.load_default_system_variable())) {
      } else if (OB_FAIL(runtime_schema.set_runtime_name(OB_SERVER_RUNTIME_NAME))) {
      } else if (OB_FAIL(sys_user.set_user_name(OB_SYS_USER_NAME))){
      } else if (OB_FAIL(sys_user.set_host(OB_SYS_HOST_NAME))){
      } else if (OB_FAIL(schema_cache_.put_schema(SERVER_RUNTIME_SCHEMA,
                                                  1UL,
                                                  runtime_schema.get_schema_version(),
                                                  runtime_schema))) {
      } else if (OB_FAIL(schema_cache_.put_schema(USER_SCHEMA,
                                                  sys_user.get_user_id(),
                                                  sys_user.get_schema_version(),
                                                  sys_user))) {
      } else if (OB_FAIL(schema_cache_.put_schema(SYS_VARIABLE_SCHEMA,
                                                  1UL,
                                                  sys_variable.get_schema_version(),
                                                  sys_variable))) {
      } else {}
    }
  }

  return ret;
}

int ObMultiVersionSchemaService::broadcast_runtime_schema(const common::ObIArray<share::schema::ObTableSchema> &table_schemas)
{
  int ret = OB_SUCCESS;
  lib::ObMutexGuard guard(schema_refresh_mutex_);
  FOREACH_CNT_X(table_schema, table_schemas, OB_SUCC(ret)) {
    if (OB_ALL_CORE_TABLE_TID == table_schema->get_table_id()) {
      continue;
    } else if (OB_FAIL(schema_cache_.put_schema(
                TABLE_SCHEMA,
                table_schema->get_table_id(),
                table_schema->get_schema_version(),
                *table_schema))) {
    } else {
    }
  }
  auto attr = lib::ObMemAttr("BroFullSchema", ObCtxIds::SCHEMA_SERVICE);
  ObArenaAllocator allocator(attr);
  ObArray<ObSimpleTableSchemaV2*> simple_table_schemas(
                  common::OB_MALLOC_NORMAL_BLOCK_SIZE,
                  common::ModulePageAllocator(allocator));
  ObSchemaMgr *schema_mgr_for_cache = NULL;
  const bool refresh_full_schema = true;
  if (FAILEDx(convert_to_simple_schema(allocator, table_schemas, simple_table_schemas))) {
    LOG_WARN("failed to convert", KR(ret));
  } else if (FALSE_IT(schema_mgr_for_cache = ATOMIC_LOAD(&schema_mgr_for_cache_))) {
  } else if (OB_ISNULL(schema_mgr_for_cache)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_mgr is null", KR(ret));
  } else if (OB_FAIL(schema_mgr_for_cache->add_tables(simple_table_schemas, refresh_full_schema))) {
  } else if (FALSE_IT(schema_mgr_for_cache->set_schema_version(
             OB_CORE_SCHEMA_VERSION + 1))) {
  } else if (OB_FAIL(add_schema(false))) {
  } else {
    LOG_INFO("broadcast runtime schema", KR(ret));
  }
  return ret;
}

// check table exist
// table_schema_version: Indicates the schema_version corresponding to table_schema
// 1) OB_INVALID_VERSION, Indicates to take the latest version of the local guard
//  (not the latest version of the internal table)
// 2) table_schema_version > local refreshed schema_version, Indicates that the local schema is behind,
//  and a special error code is returned
// 3) table_schema_version <= local refreshed schema_version: Indicates that the local schema is new enough,
//  take the latest version of the local guard for judgment
int ObMultiVersionSchemaService::check_table_exist(
  const uint64_t database_id,
  const ObString &table_name,
  const bool is_index,
  const int64_t table_schema_version,
  bool &exist)
{
  int ret = OB_SUCCESS;
  exist = false;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", K(ret));
  } else {
    SpinRLockGuard guard(schema_manager_rwlock_);
    ObSchemaGetterGuard schema_guard;
    int64_t local_version = OB_INVALID_VERSION;
    if (!is_runtime_schema_ready()) {
      ret = OB_NOT_INIT;
      LOG_WARN("local schema not inited, ", K(ret), K(database_id), K(table_name));
    } else if (table_schema_version >= 0 && OB_FAIL(get_runtime_refreshed_schema_version(local_version))) {
      LOG_WARN("fail to get local schema version", K(ret), K(table_name), K(table_schema_version));
    } else if (table_schema_version > local_version) {
      ret = OB_SCHEMA_EAGAIN;
      LOG_WARN("local schema is old, try again", K(ret), K(table_name), K(table_schema_version));
    } else if (OB_FAIL(get_runtime_schema_guard(schema_guard))) {
    } else if (OB_SUCCESS
        != (ret = schema_guard.check_table_exist(database_id,
            table_name,
            is_index,
            ObSchemaGetterGuard::ALL_NON_HIDDEN_TYPES,
            exist))) {
    }
  }
  return ret;
}

// table_schema_version: schema_version of table_schema
// 1) OB_INVALID_VERSION, Indicates to take the latest version of the local guard
//  (not the latest version of the internal table)
// 2) table_schema_version > local refreshed schema_version, Indicates that the local schema is behind,
//  and a special error code is returned
// 3) table_schema_version <= local refreshed schema_version: Indicates that the local schema is new enough,
//  take the latest version of the local guard for judgment
int ObMultiVersionSchemaService::check_table_exist(
    const uint64_t table_id,
    const int64_t table_schema_version,
    bool &exist)
{
  int ret = OB_SUCCESS;
  exist = false;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", K(ret));
  } else {
    SpinRLockGuard guard(schema_manager_rwlock_);
    ObSchemaGetterGuard schema_guard;
    int64_t local_version = OB_INVALID_VERSION;
    if (!is_runtime_schema_ready()) {
      ret = OB_NOT_INIT;
      LOG_WARN("local schema not inited,", K(ret), K(table_id));
    } else if (table_schema_version >= 0 && OB_FAIL(get_runtime_refreshed_schema_version(local_version))) {
      LOG_WARN("fail to get local schema version", K(ret), K(table_id), K(table_schema_version));
    } else if (table_schema_version > local_version) {
      ret = OB_SCHEMA_EAGAIN;
      LOG_WARN("local schema is old, try again", K(ret), K(table_id), K(table_schema_version));
    } else if (OB_FAIL(get_runtime_schema_guard(schema_guard))) {
    } else if (OB_FAIL(schema_guard.check_table_exist(table_id, exist))) {
    }
  }
  return ret;
}

int ObMultiVersionSchemaService::check_database_exist(const ObString &database_name,
  uint64_t &database_id,
  bool &exist)
{
  int ret = OB_SUCCESS;
  exist = false;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", K(ret));
  } else {
    SpinRLockGuard guard(schema_manager_rwlock_);
    ObSchemaGetterGuard schema_guard;
    if (!is_runtime_schema_ready()) {
      ret = OB_NOT_INIT;
      LOG_WARN("local schema not inited, ", K(ret));
    } else if (OB_FAIL(get_runtime_schema_guard(schema_guard))) {
    } else if (OB_FAIL(schema_guard.check_database_exist(database_name,
                                                         exist,
                                                         &database_id))) {
      ObCStringHelper helper;
      LOG_WARN(
          "failed to check database exist, ",
          "database_name",
          helper.convert(database_name),
          K(ret));
    }
  }
  return ret;
}

int ObMultiVersionSchemaService::check_runtime_schema_refreshed(bool &is_refreshed)
{
  int ret = OB_SUCCESS;
  is_refreshed = false;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_UNLIKELY(!refresh_full_schema_present_)) {
    ret = OB_HASH_NOT_EXIST;
  } else {
    is_refreshed = !refresh_full_schema_;
  }

  return ret;
}


int ObMultiVersionSchemaService::init_original_schema()
{
  int ret = OB_SUCCESS;
  SpinRLockGuard guard(schema_manager_rwlock_);
  const bool force_add = true;
  if (OB_FAIL(add_schema(force_add))) {
  } else {
    init_ = true;
  }
  return ret;
}

// schema version must incremental
int ObMultiVersionSchemaService::add_schema(
    const bool force_add)
{
  int ret = OB_SUCCESS;

  ObSchemaMgr *schema_mgr_for_cache = NULL;
  ObSchemaMemMgr *mem_mgr = NULL;
  ObSchemaMgrCache *schema_mgr_cache = NULL;
  int64_t new_schema_version = OB_INVALID_VERSION;
  int64_t refreshed_schema_version = OB_INVALID_VERSION;
  int64_t received_broadcast_version = OB_INVALID_VERSION;
  ObSchemaStore* schema_store = NULL;
  const int64_t start_time = ObTimeUtility::current_time();
  if (!force_add && !check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", K(ret));
  } else if (FALSE_IT(schema_store = &schema_store_)) {
  } else if (FALSE_IT(schema_mgr_for_cache = ATOMIC_LOAD(&schema_mgr_for_cache_))) {
  } else if (OB_ISNULL(schema_mgr_for_cache)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_mgr is null", K(ret));
  } else if (FALSE_IT(mem_mgr = mem_mgr_)) {
  } else if (OB_ISNULL(mem_mgr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("mem_mgr is null", K(ret));
  } else {
    schema_mgr_cache = &schema_store->schema_mgr_cache_;
    new_schema_version = schema_mgr_for_cache->get_schema_version();
    refreshed_schema_version = schema_store->get_refreshed_version();
    if (OB_ISNULL(schema_mgr_cache)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("schema_mgr_cache is null", K(ret));
    } else if (refreshed_schema_version > new_schema_version) {
      LOG_WARN("add schema is old",
               K(refreshed_schema_version),
               K(new_schema_version),
               K(received_broadcast_version));
    }
    FLOG_INFO("add schema", K(refreshed_schema_version), K(new_schema_version));

    bool is_exist = false;
    if (FAILEDx(schema_mgr_cache->check_schema_mgr_exist(new_schema_version, is_exist))) {
      LOG_WARN("fail to check schema_mgr exist", K(ret), K(new_schema_version));
    } else if (is_exist) {
      LOG_INFO("schema mgr already exist, just skip", K(ret), K(new_schema_version));
    } else if (OB_FAIL(alloc_and_put_schema_mgr_(*mem_mgr, *schema_mgr_for_cache, *schema_mgr_cache))) {
    }
    // try switch allocator
    if (OB_SUCC(ret)) {
      bool can_switch = false;
      int64_t max_schema_slot_num = GCONF._max_schema_slot_num;
      const int64_t switch_cnt = max_schema_slot_num;
      if (OB_FAIL(mem_mgr->check_can_switch_allocator(switch_cnt, can_switch))) {
      } else if (can_switch) {
        // Switch allocator && rewrite schema_mgr_for_cache_
        if (OB_FAIL(switch_allocator_(*mem_mgr, schema_mgr_for_cache))) {
        }
      }
    }
    if (OB_SUCC(ret)) {
      // Because RS only notifies other observers through RPC, the received_broadcast_version of the local observer
      // is not updated
      // This variable will be copied in obmp_query to ob_latest_schema_version in the session variable
      // The proxy will use the variable ob_latest_schema_version to ensure that
      // multiple observers are connected to the same schema version.
      schema_store->update_refreshed_version(new_schema_version);
      FLOG_INFO("[REFRESH_SCHEMA] change refreshed_schema_version with new mode", K(new_schema_version));
      // To reduce allocator's memory more frequently
      if (OB_FAIL(try_gc_allocator_when_add_schema_(mem_mgr, schema_mgr_cache))) {
      }
    }
    int64_t end_time = ObTimeUtility::current_time();
    LOG_INFO("finish add schema", KR(ret), K(new_schema_version), "cost_ts", end_time - start_time);
  }
  return ret;
}

ERRSIM_POINT_DEF(ERRSIM_PUT_SCHEMA);
ERRSIM_POINT_DEF(ERRSIM_ASSIGN_NEW_MGR);
int ObMultiVersionSchemaService::alloc_and_put_schema_mgr_(
    ObSchemaMemMgr &mem_mgr,
    ObSchemaMgr &latest_schema_mgr,
    ObSchemaMgrCache &schema_mgr_cache)
{
  int ret = OB_SUCCESS;
  ObSchemaMgr *new_mgr = NULL;
  ObSchemaMgr *eli_schema_mgr = NULL;
  
  const int64_t schema_version = latest_schema_mgr.get_schema_version();
  if (OB_FAIL(mem_mgr.alloc_schema_mgr(new_mgr))) {
  } else {
    if (OB_ISNULL(new_mgr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("new_mgr is NULL", KR(ret), K(schema_version));
    } else if (OB_FAIL(new_mgr->init())) {
    } else if (OB_UNLIKELY(ERRSIM_ASSIGN_NEW_MGR)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("turn on error injection ERRSIM_ASSIGN_NEW_MGR", KR(ret));
    } else if (OB_FAIL(new_mgr->assign(latest_schema_mgr))) {
    } else if (OB_UNLIKELY(ERRSIM_PUT_SCHEMA)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("turn on error injection ERRSIM_PUT_SCHEMA", KR(ret));
    } else if (OB_FAIL(schema_mgr_cache.put(new_mgr, eli_schema_mgr))) {
    } else {
      LOG_INFO("put schema mgr succeed",
                "schema_version", new_mgr->get_schema_version(),
                "eliminated_schema_version", NULL != eli_schema_mgr ?
                  eli_schema_mgr->get_schema_version() : OB_INVALID_VERSION);
    }
    int tmp_ret = OB_SUCCESS;
    // whatever put success or put failed, we should try to free eli_schema_mgr
    if (OB_TMP_FAIL(mem_mgr.free_schema_mgr(eli_schema_mgr))) {
      LOG_ERROR("fail to free eli_schema_mgr", KR(tmp_ret));
      ret = OB_SUCC(ret) ? tmp_ret : ret;
    }
    // whatever assign/put/free schema mgr failed, new schema mgr will be useless, so free it
    if (OB_FAIL(ret)) {
      LOG_WARN("handle new schema mgr failed", KR(ret), K(schema_version));
      int tmp_ret = OB_SUCCESS;
      if (OB_TMP_FAIL(mem_mgr.free_schema_mgr(new_mgr))) {
      }
    }
  }
  return ret;
}

ERRSIM_POINT_DEF(ERRSIM_SET_REFACTOR);
ERRSIM_POINT_DEF(ERRSIM_AFTER_SET_REFACTOR);
int ObMultiVersionSchemaService::switch_allocator_(
    ObSchemaMemMgr &mem_mgr,
    ObSchemaMgr *&latest_schema_mgr)
{
  int ret = OB_SUCCESS;
  int64_t start_time = ObTimeUtility::current_time();

  if (OB_ISNULL(latest_schema_mgr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("latest schema mgr is NULL", KR(ret));
  } else if (OB_FAIL(mem_mgr.switch_allocator())) {
  } else {
    bool need_switch_back = true;
    ObSchemaMgr *new_mgr = NULL;
    ObSchemaMgr *old_mgr = latest_schema_mgr;
    
    const int64_t schema_version = latest_schema_mgr->get_schema_version();
    LOG_INFO("try to switch allocator", KR(ret), K(schema_version));

    if (OB_FAIL(mem_mgr.alloc_schema_mgr(new_mgr))) {
    } else {
      if (OB_ISNULL(new_mgr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("new mgr is NULL", KR(ret), K(schema_version));
      } else if (OB_FAIL(new_mgr->init())) {
      } else if (OB_FAIL(new_mgr->deep_copy(*old_mgr))) {
      } else if (OB_UNLIKELY(ERRSIM_SET_REFACTOR)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("turn on error injection ERRSIM_SET_REFACTOR", KR(ret));
      } else {
        {
          // serialize the swap against get_runtime_schema_version's locked load+deref (restores
          // the collapsed 1-entry map's bucket lock); free_schema_mgr(old) stays outside the
          // guard, exactly as the original freed after set_refactored.
          SpinWLockGuard cache_guard(schema_mgr_for_cache_rwlock_);
          ATOMIC_STORE(&schema_mgr_for_cache_, new_mgr);
        }
        // handle new schema mgr success, no need to switch back allocator
        need_switch_back = false;
        latest_schema_mgr = new_mgr;
        if (OB_UNLIKELY(ERRSIM_AFTER_SET_REFACTOR)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("turn on error injection ERRSIM_AFTER_SET_REFACTOR", KR(ret));
        } else if (OB_FAIL(mem_mgr.free_schema_mgr(old_mgr))) {
        }
      }
    }
    // switch back allocator when cur allocator can not use
    // 1.alloc new schema mgr failed
    // 2.handle new schema failed, like deep copy
    if (need_switch_back) {
      LOG_WARN("after switch allocator, handle schema mgr encounters something wrong", KR(ret), K(schema_version));
      int tmp_ret = OB_SUCCESS;
      if (OB_TMP_FAIL(mem_mgr.switch_back_allocator())) {
      } else if (OB_TMP_FAIL(mem_mgr.free_schema_mgr(new_mgr))) {
      }
    }
    int64_t end_time = ObTimeUtility::current_time();
    LOG_INFO("finish switch allocator", KR(ret), K(schema_version), "cost_ts", end_time - start_time);
  }
  return ret;
}

int ObMultiVersionSchemaService::async_refresh_schema(const int64_t schema_version)
{
  ObASHSetInnerSqlWaitGuard ash_inner_sql_guard(ObInnerSqlWaitTypeId::WAIT_REFRESH_SCHEMA);
  int ret = OB_SUCCESS;
  int64_t local_schema_version = OB_INVALID_VERSION;
  bool check_formal = ObSchemaService::is_formal_version(schema_version);
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_FAIL(get_runtime_refreshed_schema_version(
                     local_schema_version))) {
  } else if (local_schema_version >= schema_version
             && (!check_formal || ObSchemaService::is_formal_version(local_schema_version))) {
    // do nothing
  } else {
    int64_t retry_cnt = 0;
#if defined(__APPLE__) || defined(_WIN32)
    const useconds_t RETRY_IDLE_TIME = 10 * 1000L; // 10ms
#else
    const __useconds_t RETRY_IDLE_TIME = 10 * 1000L; // 10ms
#endif
    const int64_t MAX_RETRY_CNT = 100 * 1000 * 1000L / RETRY_IDLE_TIME; // 100s at most
    const int64_t SUBMIT_TASK_FREQUENCE = 2 * 1000 * 1000L / RETRY_IDLE_TIME; // each 2s
    while (OB_SUCC(ret)) {
      if (OB_FAIL(get_runtime_refreshed_schema_version(
                         local_schema_version))) {
      } else if (local_schema_version >= schema_version
                 && (!check_formal || ObSchemaService::is_formal_version(local_schema_version))) {
        // success
        break;
      } else if (THIS_WORKER.is_timeout()
                || (!THIS_WORKER.is_timeout_ts_valid() && retry_cnt >= MAX_RETRY_CNT)) {
        ret = OB_TIMEOUT;
        LOG_WARN("already timeout", KR(ret), K(schema_version));
      } else {
        if (0 == retry_cnt % SUBMIT_TASK_FREQUENCE) {
          {
            ObSchemaGetterGuard guard;
            if (OB_FAIL(get_runtime_schema_guard(guard))) {
            }
          }
          if (OB_FAIL(ret)) {
          } else if (OB_ISNULL(schema_refresh_scheduler_)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("schema refresh scheduler is null", K(ret));
          } else if (OB_FAIL(schema_refresh_scheduler_->schedule_refresh_at_least(
              schema_version))) {
            if (OB_EAGAIN == ret || OB_SIZE_OVERFLOW == ret) {
              ret = OB_SUCCESS;
            } else {
              LOG_ERROR("fail to submit async refresh schema task",
                       KR(ret), K(schema_version));
            }
          }
        }
        if (OB_SUCC(ret)) {
          int64_t sleep_time = RETRY_IDLE_TIME;
          if (THIS_WORKER.is_timeout_ts_valid()
              && THIS_WORKER.get_timeout_remain() < RETRY_IDLE_TIME) {
            int64_t timeout_remain = THIS_WORKER.get_timeout_remain();
            sleep_time = timeout_remain > 0 ? timeout_remain : 0;
          }
          retry_cnt++;
          ob_usleep<common::ObWaitEventIds::WAIT_REFRESH_SCHEMA>(RETRY_IDLE_TIME, RETRY_IDLE_TIME, schema_version, 0);
        }
      }
    }
  }
  return ret;
}


// Refresh the server runtime schema to the version stored in the inner tables.
int ObMultiVersionSchemaService::refresh_and_add_schema(bool check_bootstrap/* = false*/,
                                                        common::ObIArray<share::schema::ObTableSchema> *table_schemas/* = nullptr*/)
{
  FLOG_INFO("[REFRESH_SCHEMA] start to refresh and add schema");
  const int64_t start = ObTimeUtility::current_time();
  int ret = OB_SUCCESS;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", K(ret));
  } else {
    lib::ObMutexGuard guard(schema_refresh_mutex_);
    auto func = [&]() {
      if (OB_FAIL(ret)) {
      } else if (check_bootstrap) {
        // The schema refresh triggered by the heartbeat is forbidden in the bootstrap phase,
        // and it needs to be judged in the schema_refresh_mutex_lock
        // 
        int64_t baseline_schema_version = OB_INVALID_VERSION;
        if (OB_FAIL(get_baseline_schema_version(true/*auto_update*/, baseline_schema_version))) {
        } else if (baseline_schema_version < 0) {
          // still in bootstrap phase, refresh schema is not allowed
          ret = OB_OP_NOT_ALLOW;
          LOG_WARN("refresh schema in bootstrap phase is not allowed", K(ret));
        }
      }

      // Keep temporary schema-refresh allocations in the server runtime context.
      ObArenaAllocator allocator(ObModIds::OB_MODULE_PAGE_ALLOCATOR, OB_MALLOC_BIG_BLOCK_SIZE);
      ObSchemaStackAllocatorGuard guard(&allocator);

      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(refresh_runtime_schema(table_schemas))) {
      }
    };
    CREATE_WITH_TEMP_ENTITY_P(true, RESOURCE_OWNER, common::OB_SERVER_RUNTIME_ID)
    {
      func();
    } else {
      func();
    }
  }
  FLOG_INFO("[REFRESH_SCHEMA] end refresh and add schema", KR(ret),
            "cost", ObTimeUtility::current_time() - start);
  return ret;
}

// Return the latest completed DDL transaction boundary at or before timestamp
// for current change-stream readers.
int ObMultiVersionSchemaService::get_schema_version_by_timestamp(
    const ObRefreshSchemaStatus &schema_status,
    int64_t timestamp,
    int64_t &schema_version)
{
  int ret = OB_SUCCESS;
  if (timestamp <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(timestamp));
  } else if (OB_ISNULL(sql_proxy_) || OB_ISNULL(schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("proxy or schema_service is null", K(ret), KP(sql_proxy_), KP(schema_service_));
  } else if (OB_FAIL(schema_service_->get_schema_version_by_timestamp(
                     *sql_proxy_, schema_status, timestamp, schema_version))) {
  }
  LOG_INFO("[REFRESH_SCHEMA] get_schema_version_by_timestamp", K(ret), K(timestamp), K(schema_version));
  return ret;
}

int ObMultiVersionSchemaService::refresh_runtime_schema(
    common::ObIArray<share::schema::ObTableSchema> *table_schemas)
{
  FLOG_INFO("[REFRESH_SCHEMA] start to refresh and add runtime schema");
  const int64_t start = ObTimeUtility::current_time();
  int ret = OB_SUCCESS;
  bool refresh_full_schema = false;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_ISNULL(sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("proxy is null", KR(ret));
  } else {
    int64_t new_published_schema_version = OB_INVALID_VERSION;
    ObRefreshSchemaStatus refresh_schema_status;
    ObISQLClient &sql_client = *sql_proxy_;

    // Read refresh_schema_status from the inner table.
    refresh_schema_status.reset();

    refresh_schema_status.snapshot_timestamp_ = OB_INVALID_TIMESTAMP;
    refresh_schema_status.readable_schema_version_ = OB_INVALID_VERSION;

    if (OB_SUCC(ret)) {
      bool need_refresh = true;
      int64_t baseline_schema_version = OB_INVALID_VERSION;
      if (OB_FAIL(get_baseline_schema_version(true/*auto_update*/, baseline_schema_version))) {
      } else if (FALSE_IT(refresh_full_schema = refresh_full_schema_)) {
      } else if (!refresh_full_schema) {
        if (OB_FAIL(get_schema_version_in_inner_table(
            sql_client, refresh_schema_status, new_published_schema_version))) {
        } else {
          ObSchemaStore* schema_store = &schema_store_;
          {
            // The inner-table version is the version published by local DDL.
            if (schema_store->get_refreshed_version() >= new_published_schema_version) {
              need_refresh = false;
            }
          }
        }
      }

      if (OB_SUCC(ret) && need_refresh) {
        if (OB_FAIL(refresh_schema(refresh_schema_status, table_schemas))) {
        }
      }
      int tmp_ret = OB_SUCCESS;
      if (OB_INVALID_SCHEMA_VERSION != new_published_schema_version) {
        if (OB_SUCCESS != (tmp_ret = set_published_schema_version(new_published_schema_version))) {
          LOG_WARN("fail to set published schema version", KR(tmp_ret), K(new_published_schema_version));
          ret = OB_SUCC(ret) ? tmp_ret : ret;
        }
      }
    }
  }
  FLOG_INFO("[REFRESH_SCHEMA] end refresh runtime schema", KR(ret),
            "cost", ObTimeUtility::current_time() - start);
  return ret;
}

int ObMultiVersionSchemaService::publish_schema()
{
  int ret = OB_SUCCESS;
  const bool force_add = false;
  if (OB_FAIL(add_schema(force_add))) {
  }
  if (OB_NOT_NULL(schema_publish_signal_)) {
    schema_publish_signal_->notify_schema_published();
  }
  return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

int ObMultiVersionSchemaService::check_outline_exist_with_name(const uint64_t database_id,
                                                               const common::ObString &outline_name,
                                                               uint64_t &outline_id,
                                                               bool is_format,
                                                               bool &exist)
{
  int ret = OB_SUCCESS;
  exist = false;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", K(ret));
  } else {
    SpinRLockGuard guard(schema_manager_rwlock_);
    ObSchemaGetterGuard schema_guard;
    if (!is_runtime_schema_ready()) {
      ret = OB_NOT_INIT;
      LOG_WARN("local schema not inited", K(ret));
    } else if (OB_UNLIKELY(OB_INVALID_ID == database_id
                           || outline_name.empty())) {
      LOG_WARN("invalid arguments", K(database_id), K(outline_name), K(ret));
    } else if (OB_FAIL(get_runtime_schema_guard(schema_guard))) {
    } else if (OB_FAIL(schema_guard.check_outline_exist_with_name(database_id,
                outline_name,
                is_format,
                outline_id,
                exist))) {
    } else {/*do nothing*/}
  }
  return ret;
}

int ObMultiVersionSchemaService::check_outline_exist_with_sql(const uint64_t database_id,
                                                              const common::ObString &paramlized_sql,
                                                              bool is_format,
                                                              bool &exist)

{
  int ret = OB_SUCCESS;
  exist = false;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", K(ret));
  } else {
    SpinRLockGuard guard(schema_manager_rwlock_);
    ObSchemaGetterGuard schema_guard;
    if (!is_runtime_schema_ready()) {
      ret = OB_NOT_INIT;
      LOG_WARN("local schema not inited", K(ret));
    } else if (OB_FAIL(get_runtime_schema_guard(schema_guard))) {
    } else if (OB_FAIL(schema_guard.check_outline_exist_with_sql(database_id,
                paramlized_sql,
                is_format,
                exist))) {
    } else {/*do nothing*/}
  }
  return ret;
}

int ObMultiVersionSchemaService::check_outline_exist_with_sql_id(const uint64_t database_id,
                                                              const common::ObString &sql_id,
                                                              bool is_format,
                                                              bool &exist)

{
  int ret = OB_SUCCESS;
  exist = false;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", K(ret));
  } else {
    SpinRLockGuard guard(schema_manager_rwlock_);
    ObSchemaGetterGuard schema_guard;
    if (!is_runtime_schema_ready()) {
      ret = OB_NOT_INIT;
      LOG_WARN("local schema not inited", K(ret));
    } else if (OB_FAIL(get_runtime_schema_guard(schema_guard))) {
    } else if (OB_FAIL(schema_guard.check_outline_exist_with_sql_id(database_id,
                sql_id,
                is_format,
                exist))) {
    } else {/*do nothing*/}
  }
  return ret;
}


//-----------For managing privileges-----------

int ObMultiVersionSchemaService::check_user_exist(
    const common::ObString &user_name,
    const common::ObString &host_name,
    bool &exist)
{
  int ret = OB_SUCCESS;
  exist = false;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", K(ret));
  } else {
    uint64_t user_id = OB_INVALID_ID;
    ret = check_user_exist(user_name, host_name, user_id, exist);
  }
  return ret;
}

int ObMultiVersionSchemaService::check_user_exist(
    const common::ObString &user_name,
    const common::ObString &host_name,
    uint64_t &user_id,
    bool &exist)
{
  int ret = OB_SUCCESS;
  exist = false;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", K(ret));
  } else {
    SpinRLockGuard guard(schema_manager_rwlock_);
    ObSchemaGetterGuard schema_guard;
    if (!is_runtime_schema_ready()) {
      ret = OB_NOT_INIT;
      LOG_WARN("local schema not inited", K(ret));
    } else if (OB_FAIL(get_runtime_schema_guard(schema_guard))) {
    } else if (OB_FAIL(schema_guard.check_user_exist(user_name,
                                                     host_name,
                                                     exist,
                                                     &user_id))) {
    }
  }
  return ret;
}

int ObMultiVersionSchemaService::check_user_exist(
    const uint64_t user_id,
    bool &exist)
{
  int ret = OB_SUCCESS;
  exist = false;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", K(ret));
  } else {
    SpinRLockGuard guard(schema_manager_rwlock_);
    ObSchemaGetterGuard schema_guard;
    if (!is_runtime_schema_ready()) {
      ret = OB_NOT_INIT;
      LOG_WARN("local schema not inited", K(ret));
    } else if (OB_FAIL(get_runtime_schema_guard(schema_guard))) {
    } else if (OB_FAIL(schema_guard.check_user_exist(user_id, exist))) {
    }
  }
  return ret;
}

void ObMultiVersionSchemaService::dump_schema_statistics()
{
  int ret = OB_SUCCESS;
  lib::ObMutexGuard guard(schema_refresh_mutex_);
  FLOG_INFO("[SCHEMA_STATISTICS] dump schema statistics info start");
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", K(ret));
  } else {
    {
      ObSchemaMemMgr *mem_mgr = mem_mgr_;
      FLOG_INFO("[SCHEMA_STATISTICS] dump schema for refresh start", K(ret));
      if (OB_ISNULL(mem_mgr)) {
        LOG_INFO("mem_mgr is null", K(ret));
      } else {
        mem_mgr->dump();

        ObSchemaMgr *schema_mgr_for_cache = ATOMIC_LOAD(&schema_mgr_for_cache_);
        if (OB_NOT_NULL(schema_mgr_for_cache)) {
          schema_mgr_for_cache->dump();
        }

        schema_store_.schema_mgr_cache_.dump();
      }
      FLOG_INFO("[SCHEMA_STATISTICS] dump schema for refresh end", K(ret));
    }
  }
}

int ObMultiVersionSchemaService::try_eliminate_schema_mgr()
{
  int ret = OB_SUCCESS;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", K(ret));
  } else if (OB_FAIL(try_gc_existing_runtime_schema_mgr())) {
  }
  return ret;
}

int ObMultiVersionSchemaService::try_gc_existing_runtime_schema_mgr()
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (!is_runtime_schema_ready()) {
    ret = OB_SCHEMA_EAGAIN;
    LOG_WARN("full schema is not ready, cann't get fallback schema guard", KR(ret));
  } else if (OB_FAIL(get_runtime_schema_guard(schema_guard))) {
  } else {
    ObSchemaMemMgr *mem_mgr = mem_mgr_;
    ObSchemaMgrCache *schema_mgr_cache = &schema_store_.schema_mgr_cache_;
    { // ignore ret
      int tmp_ret = OB_SUCCESS;
      // 1. another allocator for schema refresh
      if (OB_TMP_FAIL(try_gc_another_allocator(mem_mgr, schema_mgr_cache))) {
      }
      // 2. let schema mgr free slot memory
      if (OB_FAIL(try_gc_current_allocator(mem_mgr, schema_mgr_cache))) {
      }
    }
  }
  return ret;
}

// need protected by schema_refresh_mutex_
int ObMultiVersionSchemaService::try_gc_another_allocator(
    ObSchemaMemMgr *&mem_mgr,
    ObSchemaMgrCache *&schema_mgr_cache)
{
  int ret = OB_SUCCESS;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", K(ret));
  } else if (OB_ISNULL(mem_mgr) || OB_ISNULL(schema_mgr_cache)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("mem_mgr or schema_mgr_cahe is null",
             K(ret), KP(mem_mgr), KP(schema_mgr_cache));
  } else {
    lib::ObMutexGuard guard(schema_refresh_mutex_);
    ObArray<void *> another_ptrs;
    int64_t local_version = OB_INVALID_VERSION;
    if (OB_FAIL(mem_mgr->get_another_ptrs(another_ptrs))) {
    } else if (OB_FAIL(get_runtime_refreshed_schema_version(local_version))) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < another_ptrs.count(); i++) {
        ObSchemaMgr *tmp_mgr = NULL;
        if (OB_ISNULL(another_ptrs.at(i))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("ptrs is null", K(ret), K(i));
        } else if (FALSE_IT(tmp_mgr = static_cast<ObSchemaMgr *>(another_ptrs.at(i)))) {
        } else if (tmp_mgr->get_schema_version() >= local_version) {
          ret = OB_SCHEMA_EAGAIN;
          LOG_INFO("schema mgr is in used, try reset another allocator next round",
                   K(ret), "version", tmp_mgr->get_schema_version(),
                   K(local_version));
        }
      }
      ObSchemaMgr *eli_schema_mgr = NULL;
      for (int64_t i = 0; OB_SUCC(ret) && i < another_ptrs.count(); i++) {
        if (OB_ISNULL(another_ptrs.at(i))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("ptrs is null", K(ret), K(i));
        } else if (FALSE_IT(eli_schema_mgr = static_cast<ObSchemaMgr *>(another_ptrs.at(i)))) {
        } else if (OB_FAIL(schema_mgr_cache->try_eliminate_schema_mgr(eli_schema_mgr))) {
          if (OB_ENTRY_NOT_EXIST == ret) {
            ret = OB_SCHEMA_EAGAIN;
            LOG_INFO("schema mgr is not in cache, try reset another allocator next round",
                     KR(ret), "schema_mgr", another_ptrs.at(i));
          } else {
            LOG_WARN("fail to eliminate schema_mgr", K(ret), K(eli_schema_mgr));
          }
        } else if (OB_FAIL(mem_mgr->free_schema_mgr(eli_schema_mgr))) {
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(mem_mgr->try_reset_another_allocator())) {
      }
    }
  }
  LOG_INFO("try gc another allocator", K(ret));
  return ret;
}

// try to gc current allocator's schema mgr, it can reduce the number of schema mgr in the background
int ObMultiVersionSchemaService::try_gc_current_allocator(
    ObSchemaMemMgr *&mem_mgr,
    ObSchemaMgrCache *&schema_mgr_cache)
{
  int ret = OB_SUCCESS;
  int64_t recycle_interval = GCONF._schema_memory_recycle_interval;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_ISNULL(mem_mgr) || OB_ISNULL(schema_mgr_cache)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("mem_mgr or schema_mgr_cahe is null",
             KR(ret), KP(mem_mgr), KP(schema_mgr_cache));
  } else if (0 == recycle_interval) {
    // 0 means turn off gc current allocator
  } else {
    int64_t start_time = ObTimeUtility::current_time();
    ObArray<void *> current_ptrs;
    int64_t refreshed_schema_version = OB_INVALID_VERSION;
    int64_t latest_schema_version = OB_INVALID_VERSION;
    int64_t local_version = OB_INVALID_VERSION;
    lib::ObMutexGuard guard(schema_refresh_mutex_);

    ObSchemaMgr *latest_schema_mgr = NULL;
    if (OB_FAIL(mem_mgr->get_current_ptrs(current_ptrs))) {
    } else if (OB_FAIL(get_runtime_refreshed_schema_version(refreshed_schema_version))) {
    } else if (FALSE_IT(latest_schema_mgr = ATOMIC_LOAD(&schema_mgr_for_cache_))) {
    } else if (OB_ISNULL(latest_schema_mgr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("schema_mgr is null", KR(ret));
    } else if (FALSE_IT(latest_schema_version = latest_schema_mgr->get_schema_version())) {
    } else if (FALSE_IT(local_version = min(refreshed_schema_version, latest_schema_version))) {
    } else if (!ObSchemaService::is_formal_version(local_version)) {
    } else {
      int64_t eli_timestamp = 0;
      ObSchemaMgr *eli_schema_mgr = NULL;
      int64_t eli_schema_version = OB_INVALID_VERSION;
      for (int64_t i = 0; OB_SUCC(ret) && i < current_ptrs.count(); i++) {
        if (OB_ISNULL(current_ptrs.at(i))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("ptrs is null", KR(ret), K(i));
        } else {
          eli_schema_mgr = static_cast<ObSchemaMgr *>(current_ptrs.at(i));
          eli_timestamp = eli_schema_mgr->get_timestamp_in_slot();
          eli_schema_version = eli_schema_mgr->get_schema_version();
          if (eli_schema_version >= local_version
              || (recycle_interval > ObClockGenerator::getClock() - eli_timestamp)) {
          } else {
            //gc only those that have been put in the slot for more than recycle_interval
            LOG_INFO("try to gc current allocator's schema mgr which is in slot",
                     K(eli_schema_version), K(local_version),
                     K(refreshed_schema_version), K(latest_schema_version),
                     K(eli_timestamp), K(recycle_interval));
            if (OB_FAIL(schema_mgr_cache->try_eliminate_schema_mgr(eli_schema_mgr))) {
              if (OB_EAGAIN == ret || OB_ENTRY_NOT_EXIST == ret) {
                // schema mgr in use or not in cache, just ignore
                ret = OB_SUCCESS;
              } else {
                LOG_WARN("fail to eliminate schema_mgr", KR(ret),
                         K(eli_schema_version), K(eli_timestamp));
              }
            } else if (OB_FAIL(mem_mgr->free_schema_mgr(eli_schema_mgr))) {
            }
          }
        }
      }
    }
    int64_t end_time = ObTimeUtility::current_time();
    LOG_INFO("finish gc current allocator's schema mgr which is in slot", KR(ret),
             "cost_ts", end_time - start_time);
  }
  return ret;
}

bool ObMultiVersionSchemaService::compare_schema_mgr_info_(
     const ObSchemaMgr *lhs,
     const ObSchemaMgr *rhs)
{
  return lhs->get_schema_version() < rhs->get_schema_version();
}

// need protected by schema_refresh_mutex_
// try to gc current and another allocators' schema mgr, it can reduce the number of schema mgr in the foreground
// 1.reserve_mgr_count can let us reserve the number of total schema mgr
// 2.we can turn this off by set _schema_memory_recycle_interval to zero
ERRSIM_POINT_DEF(ERRSIM_GC_ALLOCATOR_WHEN_REFRESH_SCHEMA);
int ObMultiVersionSchemaService::try_gc_allocator_when_add_schema_(
    ObSchemaMemMgr *&mem_mgr,
    ObSchemaMgrCache *&schema_mgr_cache)
{
  int ret = OB_SUCCESS;
  ObArray<void *> all_ptrs;
  int64_t refreshed_schema_version = OB_INVALID_VERSION;
  int64_t latest_schema_version = OB_INVALID_VERSION;
  int64_t local_version = OB_INVALID_VERSION;
  int64_t reserve_version = OB_INVALID_VERSION;
  int64_t start_time = ObTimeUtility::current_time();
  const int64_t reserve_mgr_count = RESERVE_SCHEMA_MGR_CNT;
  ObSchemaMgr *latest_schema_mgr = NULL;
  if (OB_ISNULL(mem_mgr) || OB_ISNULL(schema_mgr_cache)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("mem_mgr or schema_mgr_cahe is null",
             KR(ret), KP(mem_mgr), KP(schema_mgr_cache));
  } else if (0 > reserve_mgr_count) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("reserve_mgr_count is less than zero", KR(ret));
  } else if (OB_UNLIKELY(ERRSIM_GC_ALLOCATOR_WHEN_REFRESH_SCHEMA)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("inject error when ERRSIM_GC_ALLOCATOR_WHEN_REFRESH_SCHEMA is set", KR(ret));
  } else if (0 == GCONF._schema_memory_recycle_interval) {
    // ignore
  } else if (OB_FAIL(mem_mgr->get_all_ptrs(all_ptrs))) {
  } else if (OB_FAIL(get_runtime_refreshed_schema_version(refreshed_schema_version))) {
  } else if (FALSE_IT(latest_schema_mgr = ATOMIC_LOAD(&schema_mgr_for_cache_))) {
  } else if (OB_ISNULL(latest_schema_mgr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_mgr is null", KR(ret));
  } else if (FALSE_IT(latest_schema_version = latest_schema_mgr->get_schema_version())) {
  } else if (FALSE_IT(local_version = min(refreshed_schema_version, latest_schema_version))) {
  } else if (!ObSchemaService::is_formal_version(local_version)) {
  } else {
    SchemaMgrIterator iter;
    SchemaMgrInfos schema_mgr_infos;
    ObSchemaMgr *eli_schema_mgr = NULL;
    for (int64_t i = 0; OB_SUCC(ret) && i < all_ptrs.count(); i++) {
      iter = schema_mgr_infos.end();
      eli_schema_mgr = static_cast<ObSchemaMgr *>(all_ptrs.at(i));
      if (OB_ISNULL(eli_schema_mgr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("eli_schema_mgr is null", KR(ret), K(i));
      } else if (OB_FAIL(schema_mgr_infos.insert(eli_schema_mgr, iter, compare_schema_mgr_info_))) {
      }
    }
    if (OB_FAIL(ret)) {
      // ignore
    } else if (all_ptrs.count() != schema_mgr_infos.count()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("all_ptrs and schema_mgr_infos count not equal", KR(ret),
                K(all_ptrs.count()), K(schema_mgr_infos.count()));
    } else {
      int64_t schema_mgr_cnt = schema_mgr_infos.count();
      int64_t reserve_index = schema_mgr_cnt > reserve_mgr_count ?
                              schema_mgr_cnt - reserve_mgr_count - 1 : OB_INVALID_INDEX;
      // we should skip free schema mgr when schema_mgr_cnt less than reserve_mgr_count
      if (reserve_index >= 0 && 0 != schema_mgr_cnt) {
        reserve_version = schema_mgr_infos.at(reserve_index)->get_schema_version();
      }
    }
    int64_t eli_schema_version = 0;
    int64_t total_schema_ptr_cnt = all_ptrs.count();
    int64_t remain_schema_ptr_cnt = all_ptrs.count();
    for (int64_t i = 0; OB_SUCC(ret) && i < total_schema_ptr_cnt; i++) {
      eli_schema_mgr = static_cast<ObSchemaMgr *>(all_ptrs.at(i));
      if (OB_ISNULL(eli_schema_mgr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("eli_schema_mgr is null", KR(ret), K(i));
      } else {
        eli_schema_version = eli_schema_mgr->get_schema_version();
        if (eli_schema_version >= local_version
            || eli_schema_version >= reserve_version) {
        } else {
          LOG_INFO("try to gc allocator's schema mgr which schema version is less than reserve_version",
                   K(eli_schema_version), K(local_version), K(refreshed_schema_version),
                   K(latest_schema_version), K(reserve_version));
          if (OB_FAIL(schema_mgr_cache->try_eliminate_schema_mgr(eli_schema_mgr))) {
            if (OB_EAGAIN == ret || OB_ENTRY_NOT_EXIST == ret) {
              // schema mgr in use or not in cache, just ignore
              ret = OB_SUCCESS;
            } else {
              LOG_WARN("fail to eliminate schema_mgr", KR(ret), K(eli_schema_version));
            }
          } else if (OB_FAIL(mem_mgr->free_schema_mgr(eli_schema_mgr))) {
          } else {
            remain_schema_ptr_cnt--;
          }
        }
      }
    } // for
    int64_t end_time = ObTimeUtility::current_time();
    LOG_INFO("finish gc allocator's schema mgr when add schema", KR(ret),
              K(total_schema_ptr_cnt), K(remain_schema_ptr_cnt), K(reserve_version),
              "cost_ts", end_time - start_time);
  }
  return ret;
}

bool ObMultiVersionSchemaService::is_runtime_schema_ready() const
{
  bool bret = false;
  int64_t schema_version = OB_INVALID_VERSION;
  int ret = get_runtime_refreshed_schema_version(schema_version);
  bret = OB_SUCC(ret) && schema_version > OB_CORE_SCHEMA_VERSION;
  return bret;
}

bool ObMultiVersionSchemaService::is_runtime_schema_refreshed() const
{
  return refresh_full_schema_present_ && !refresh_full_schema_;
}

int ObMultiVersionSchemaService::check_runtime_schema_ready(bool &all_refreshed)
{
  int ret = OB_SUCCESS;
  all_refreshed = true;
  if (!is_runtime_schema_refreshed()) {
    all_refreshed = false;
  }
  return ret;
}

int ObMultiVersionSchemaService::get_runtime_refreshed_schema_version(
    int64_t &schema_version,
    const bool core_version) const
{
  int ret = OB_SUCCESS;
  int64_t refreshed_schema_version = OB_INVALID_VERSION;
  {
    // new schema refresh
    refreshed_schema_version = schema_store_.get_refreshed_version();
  }
  if (OB_SUCC(ret)) {
    schema_version = (!core_version || refreshed_schema_version > 0) ? refreshed_schema_version : OB_CORE_SCHEMA_VERSION;
  }
  return ret;
}

int ObMultiVersionSchemaService::get_published_schema_version(
    int64_t &schema_version,
    const bool core_schema_version) const
{
  int ret = OB_SUCCESS;
  int64_t published_schema_version = OB_INVALID_VERSION;
  {
    published_schema_version = schema_store_.get_published_version();
  }
  if (OB_SUCC(ret)) {
    schema_version = (!core_schema_version || published_schema_version > 0)
        ? published_schema_version
        : OB_CORE_SCHEMA_VERSION;
  }
  return ret;
}

int ObMultiVersionSchemaService::set_published_schema_version(
    const int64_t version)
{
  int ret = OB_SUCCESS;
  if (version != OB_CORE_SCHEMA_VERSION) {
    schema_store_.update_published_version(version);
    LOG_INFO("set published schema version", K(ret), K(version));
  } else {
    ret = OB_OLD_SCHEMA_VERSION;
  }
  return ret;
}

int ObMultiVersionSchemaService::get_last_refreshed_schema_info(ObRefreshSchemaInfo &schema_info)
{
  int ret = OB_SUCCESS;
  SpinRLockGuard guard(schema_info_rwlock_);
  if (OB_FAIL(schema_info.assign(last_refreshed_schema_info_))) {
  }
  return ret;
}

int ObMultiVersionSchemaService::set_last_refreshed_schema_info(const ObRefreshSchemaInfo &schema_info)
{
  int ret = OB_SUCCESS;
  SpinWLockGuard guard(schema_info_rwlock_);
  const ObDDLSequenceID last_sequence_id = last_refreshed_schema_info_.get_sequence_id();
  const ObDDLSequenceID new_sequence_id = schema_info.get_sequence_id();
  if (!new_sequence_id.is_valid()
      || (last_sequence_id.is_valid() && (ObDDLSequenceID::LESS_THAN == new_sequence_id.compare_to_other_id(last_sequence_id)
                                          || ObDDLSequenceID::EQUAL_TO == new_sequence_id.compare_to_other_id(last_sequence_id)))) {
    LOG_INFO("no need to set last refreshed schema info", K(ret), K(last_refreshed_schema_info_), K(schema_info));
  } else if (OB_FAIL(last_refreshed_schema_info_.assign(schema_info))) {
  }
  return ret;
}

int ObMultiVersionSchemaService::gen_new_schema_version(
    int64_t &schema_version)
{
  int ret = OB_SUCCESS;
  int64_t refreshed_schema_version = OB_INVALID_VERSION;
  schema_version = OB_INVALID_VERSION;
  if (OB_FAIL(get_runtime_refreshed_schema_version(refreshed_schema_version))) {
  } else if (OB_ISNULL(schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema service is null", K(ret));
  } else if (OB_FAIL(schema_service_->gen_new_schema_version(refreshed_schema_version, schema_version))) {
  }
  return ret;
}

int ObMultiVersionSchemaService::gen_batch_new_schema_versions(const int64_t version_cnt,
    int64_t &schema_version)
{
  int ret = OB_SUCCESS;
  int64_t refreshed_schema_version = OB_INVALID_VERSION;
  schema_version = OB_INVALID_VERSION;
  if (OB_UNLIKELY(version_cnt < 1)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret));
  } else if (OB_FAIL(get_runtime_refreshed_schema_version(refreshed_schema_version))) {
  } else if (OB_ISNULL(schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema service is null", KR(ret));
  } else if (OB_FAIL(schema_service_->gen_batch_new_schema_versions(refreshed_schema_version, version_cnt, schema_version))) {
  }
  return ret;
}

int ObMultiVersionSchemaService::get_new_schema_version(int64_t &schema_version) {
  int ret = OB_SUCCESS;
  schema_version = OB_INVALID_VERSION;
  if (OB_ISNULL(schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema service is null", K(ret));
  } else {
    ret = schema_service_->get_new_schema_version(schema_version);
  }
  return ret;
}

int ObMultiVersionSchemaService::get_runtime_mem_info(
    const uint64_t &req_id,
    common::ObIArray<ObSchemaMemory> &runtime_mem_infos)
{
  int ret = OB_SUCCESS;
  ObSchemaMemMgr *mem_mgr = NULL;

  if (OB_ISNULL(schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema service is null", KR(ret));
  } else if (FALSE_IT(mem_mgr = (1UL == req_id) ? mem_mgr_ : NULL)) {
  } else if (OB_ISNULL(mem_mgr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("mem_mgr is NULL", KR(ret));
  } else if (OB_FAIL(mem_mgr->get_all_alloc_info(runtime_mem_infos))) {
  }
  return ret;
}

int ObMultiVersionSchemaService::get_runtime_slot_info(
    common::ObIAllocator &allocator,
    const uint64_t &req_id,
    common::ObIArray<ObSchemaSlot> &runtime_slot_infos)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema service is null", KR(ret));
  } else if (OB_FAIL(schema_store_.schema_mgr_cache_.get_slot_info(allocator, runtime_slot_infos))) {
  }
  return ret;
}

int ObMultiVersionSchemaService::get_schema_version_history(
    const ObRefreshSchemaStatus &fetch_schema_status,
    const int64_t schema_version,
    const VersionHisKey &key,
    VersionHisVal &val,
    bool &not_exist)
{
  int ret = OB_SUCCESS;
  not_exist = false;
  val.reset();
  if (!key.is_valid() || schema_version < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(key), K(schema_version));
  } else {
    int hash_ret = version_his_map_.get_refactored(key, val);
    if (hash_ret == OB_HASH_NOT_EXIST || schema_version > val.snapshot_version_) {
      int64_t snapshot_version = OB_INVALID_VERSION;
      if (OB_FAIL(get_runtime_refreshed_schema_version(snapshot_version))) {
      } else if (OB_FAIL(construct_schema_version_history(fetch_schema_status, snapshot_version, key, val))) {
      } else if (0 == val.valid_cnt_) {
        //FIXME: When the specified schema is too small, there is no corresponding record in the history,
        //  and a null pointer is returned.
        //       1) The history is complete, indicating that the schema for a given version does not exist;
        //       2) If history is reclaimed, it may also enter the branch, but considering the following points,
        //        it will not be processed for the time being:
        //       - 2.x not reclaim history
        //       - 1.4.x Even if history is reclaimed, it is also reclaiming long-awaited schema multi-version information,
        //        and the corresponding multi-version information is likely not to be relied on;
        //       - The reservoir fetches table_schema through retry_get_schema_guard, even if the incoming schema_version
        //        is too small, it will return the schema version that the table_schema exists for the first time
        not_exist = true;
        LOG_INFO("specific schema_version is small, schema not exist",
                 K(ret), K(fetch_schema_status), K(schema_version), K(snapshot_version));
      } else {
        if (OB_FAIL(version_his_map_.set_refactored(key, val, 1 /*overwrite val*/))) {
        } else {
          LOG_INFO("construct_schema_version_history succeed", K(key), K(val));
        }
      }
    }
  }
  return ret;
}

int ObMultiVersionSchemaService::get_runtime_name_case_mode(ObNameCaseMode &name_case_mode)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard guard;
  const ObSimpleSysVariableSchema *sys_variable = NULL;
  name_case_mode = OB_NAME_CASE_INVALID;
  if (OB_FAIL(get_runtime_schema_guard(guard))) {
  } else if (OB_FAIL(guard.get_sys_variable_schema( sys_variable))) {
  } else if (OB_ISNULL(sys_variable)) {
    ret = OB_ENTRY_NOT_EXIST;
    LOG_WARN("sys variable not exist", KR(ret));
  } else {
    name_case_mode = sys_variable->get_name_case_mode();
  }
  return ret;
}

int ObMultiVersionSchemaService::update_baseline_schema_version(
    const int64_t baseline_schema_version)
{
  int ret = OB_SUCCESS;
  int64_t bl_schema_version = OB_INVALID_VERSION;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else {
    bl_schema_version = schema_store_.get_baseline_schema_version();
    if (baseline_schema_version < bl_schema_version) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", KR(ret),
               K(baseline_schema_version), K(bl_schema_version));
    } else {
      schema_store_.update_baseline_schema_version(baseline_schema_version);
    }
  }
  return ret;
}

int ObMultiVersionSchemaService::get_baseline_schema_version(
    bool auto_update,
    int64_t &baseline_schema_version)
{
  int ret = OB_SUCCESS;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else {
    baseline_schema_version = schema_store_.get_baseline_schema_version();
    if (OB_INVALID_VERSION == baseline_schema_version && auto_update) {
      ObISQLClient &sql_client = *sql_proxy_;
      ObRefreshSchemaStatus schema_status;
      ObSchemaStatusProxy *schema_status_proxy = get_schema_status_proxy();
      if (OB_ISNULL(schema_status_proxy)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("schema_status_proxy is null", K(ret));
      } else if (OB_FAIL(schema_status_proxy->get_refresh_schema_status(schema_status))) {
      }
      if (FAILEDx(schema_service_->get_baseline_schema_version(
                  sql_client, schema_status, baseline_schema_version))) {
        LOG_WARN("get baseline schema version failed", KR(ret), K(schema_status));
      } else if (baseline_schema_version < OB_INVALID_VERSION) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("unexpected baseline schema version",
                  KR(ret), K(schema_status), K(baseline_schema_version));
      } else {
        schema_store_.update_baseline_schema_version(baseline_schema_version);
        LOG_INFO("fetch baseline schema version finish",
                 KR(ret), K(schema_status), K(baseline_schema_version));
      }
    }
  }
  return ret;
}

int ObMultiVersionSchemaService::get_tablet_to_table_history(const ObIArray<ObTabletID> &tablet_ids,
    const int64_t schema_version,
    ObIArray<uint64_t> &table_ids)
{
  int ret = OB_SUCCESS;
  table_ids.reset();
  int64_t tablet_ids_cnt = tablet_ids.count();
  if (OB_UNLIKELY(!check_inner_stat())) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_UNLIKELY(tablet_ids_cnt <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", KR(ret), K(tablet_ids_cnt));
  } else if (OB_FAIL(table_ids.reserve(tablet_ids_cnt))) {
  } else {
    // record idx of tablet_ids which can't get tablet-table from cache
    ObArray<int64_t> fetch_idxs;
    ObTabletCacheKey key;
    uint64_t table_id = OB_INVALID_ID;
    for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ids.count(); i++) {
      const ObTabletID &tablet_id = tablet_ids.at(i);
      if (!tablet_id.is_valid()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid tablet_id or argument", KR(ret), K(tablet_id));
      } else if (tablet_id.is_inner_tablet()) {
        // case 1: inner tablet_id is equal to its table_id
        table_id = tablet_id.id();
      } else if (OB_FAIL(key.init(tablet_id, schema_version))) {
      } else if (OB_FAIL(schema_cache_.get_tablet_cache(key, table_id))) {
        if (OB_ENTRY_NOT_EXIST != ret) {
          LOG_WARN("fail to get from cache", KR(ret), K(key));
        } else if (OB_FAIL(fetch_idxs.push_back(i))) {
        } else {
          // case 2: cache miss, fetch later
          table_id = OB_INVALID_ID; // occupancy
        }
      } else {
        // case 3: cache hit
      }
      if (FAILEDx(table_ids.push_back(table_id))) {
        LOG_WARN("fail to push back table_id", KR(ret), K(tablet_id), K(table_id));
      }
    } // end for

    if (OB_SUCC(ret) && tablet_ids_cnt != table_ids.count()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("array cnt not match", KR(ret),
               K(tablet_ids_cnt), "table_ids_cnt", table_ids.count());
    }

    if (OB_SUCC(ret) && fetch_idxs.count() > 0) {
      // init map
      const int64_t BUCKET_NUM = 10000;
      common::hash::ObHashMap<ObTabletID, uint64_t> tablet_map; // (tablet_id, table_id)
      if (OB_UNLIKELY(schema_version <= 0
          || !ObSchemaService::is_formal_version(schema_version))) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid arg", KR(ret), K(tablet_ids_cnt), K(schema_version));
      } else if (OB_FAIL(tablet_map.create(BUCKET_NUM, "TbtTbPair", "TbtTbPair"))) {
      }

      // fetch result
      const int64_t EACH_BATCH_CNT = 1000;
      int64_t start_idx = 0;
      int64_t end_idx = min(fetch_idxs.count(), start_idx + EACH_BATCH_CNT);
      while (OB_SUCC(ret)
             && end_idx <= fetch_idxs.count()
             && end_idx - start_idx > 0) {
        if (OB_FAIL(batch_fetch_tablet_to_table_history_(tablet_ids, schema_version,
            fetch_idxs, start_idx, end_idx, tablet_map))) {
        } else {
          start_idx = end_idx;
          end_idx = min(fetch_idxs.count(), start_idx + EACH_BATCH_CNT);
        }
      } // end while

      // construct result
      for (int64_t i = 0; OB_SUCC(ret) && i < fetch_idxs.count(); i++) {
        int64_t idx = fetch_idxs.at(i);
        if (idx < 0 || idx >= tablet_ids_cnt) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("idx is invalid", KR(ret), K(idx), K(tablet_ids_cnt));
        } else {
          const ObTabletID &tablet_id = tablet_ids.at(idx);
          if (OB_FAIL(tablet_map.get_refactored(tablet_id, table_id))) {
            if (OB_HASH_NOT_EXIST != ret) {
              LOG_WARN("fail to get from map", KR(ret), K(tablet_id));
            } else {
              // Can't fetch from inner table. tablet-table history may be recycled or never exists.
              table_ids.at(idx) = OB_INVALID_ID;
              ret = OB_SUCCESS;
            }
          } else {
            table_ids.at(idx) = table_id;
          }
        }
      } // end for
    }
  }
  return ret;
}

int ObMultiVersionSchemaService::cal_purge_need_timeout(
    const obcall::ObPurgeRecycleBinArg &purge_recyclebin_arg,
    int64_t &cal_timeout)
{
  int ret = OB_SUCCESS;
  int64_t tmp_timeout = 0;
  int64_t total_purge_count = 0;
  ObArray<ObRecycleObject> recycle_objs;
  const int64_t purge_num = purge_recyclebin_arg.purge_num_;
  const int64_t expire_time = purge_recyclebin_arg.expire_time_;

  if (OB_ISNULL(schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema service is NULL", KR(ret));
  } else if (OB_ISNULL(sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sql proxy is NULL", KR(ret));
  } else if (OB_FAIL(schema_service_->fetch_expire_recycle_objects(
      expire_time, *sql_proxy_, recycle_objs))) {
  } else {
    for (int64_t i = 0;
         OB_SUCC(ret) && i < recycle_objs.count()
             && total_purge_count < purge_num;
         ++i) {
      const ObRecycleObject &recycle_obj = recycle_objs.at(i);
      switch (recycle_obj.get_type()) {
        case ObRecycleObject::VIEW:
        case ObRecycleObject::TABLE: {
          int64_t cal_table_timeout = 0;
          const uint64_t table_id = recycle_obj.get_table_id();
          if (OB_FAIL(cal_purge_table_timeout_(
              table_id, cal_table_timeout, total_purge_count))) {
          } else {
            tmp_timeout += cal_table_timeout;
          }
          break;
        }
        case ObRecycleObject::DATABASE: {
          int64_t cal_database_timeout = 0;
          const int64_t database_id = recycle_obj.get_database_id();
          if (OB_FAIL(cal_purge_database_timeout_(
              database_id, cal_database_timeout, total_purge_count))) {
          } else {
            tmp_timeout += cal_database_timeout;
          }
          break;
        }
        case ObRecycleObject::TRIGGER:
        case ObRecycleObject::INDEX:
        case ObRecycleObject::AUX_LOB_META:
        case ObRecycleObject::AUX_LOB_PIECE:
        case ObRecycleObject::RESERVED_TYPE_5:
          break;
        default:
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unknown recycle object type", K(recycle_obj));
          break;
      }
    }
  }

  if (OB_SUCC(ret)) {
    int64_t high_bound_timeout = 0;
    const int64_t low_bound_timeout = 10 * GCONF.rpc_timeout;
    if (0 == total_purge_count) {
      cal_timeout = 0;
    } else if (OB_FAIL(ObShareUtil::get_ctx_timeout(
        GCONF._ob_ddl_timeout, high_bound_timeout))) {
    } else {
      tmp_timeout = std::max(low_bound_timeout, tmp_timeout);
      cal_timeout = std::min(high_bound_timeout, tmp_timeout);
    }
  }
  return ret;
}

int ObMultiVersionSchemaService::cal_purge_table_timeout_(
    const uint64_t &table_id,
    int64_t &cal_table_timeout,
    int64_t &total_purge_count)
{
  int ret = OB_SUCCESS;
  int64_t part_num = 0;
  cal_table_timeout = 0;
  ObArray<uint64_t> table_ids;
  ObSchemaGetterGuard schema_guard;
  const ObTableSchema *orig_table_schema = NULL;
  ObSEArray<ObAuxTableMetaInfo, 16> simple_index_infos;

  if (OB_UNLIKELY(!check_inner_stat())) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == table_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("arg is not invalid", KR(ret), K(table_id));
  } else if (OB_FAIL(get_runtime_schema_guard(schema_guard))) {
  } else if (OB_FAIL(schema_guard.get_table_schema( table_id, orig_table_schema))) {
  } else if (OB_ISNULL(orig_table_schema)) {
    // ignore
  } else if (OB_FAIL(orig_table_schema->get_simple_index_infos(simple_index_infos))) {
  } else {
    total_purge_count++;
    part_num = orig_table_schema->get_all_part_num();
    ObIndexType index_type = INDEX_TYPE_IS_NOT;
    ObTableType table_type = MAX_TABLE_TYPE;
    // get all index table id
    int64_t index_count = simple_index_infos.count();
    for (int64_t i = 0; OB_SUCC(ret) && i < index_count; ++i) {
      index_type = simple_index_infos.at(i).index_type_;
      table_type = simple_index_infos.at(i).table_type_;
      if (index_has_tablet(index_type)) {
        if (OB_FAIL(table_ids.push_back(simple_index_infos.at(i).table_id_))) {
        }
      }
    }
    // get lob table id
    if (OB_SUCC(ret) && orig_table_schema->has_lob_aux_table()) {
      uint64_t mtid = orig_table_schema->get_aux_lob_meta_tid();
      uint64_t ptid = orig_table_schema->get_aux_lob_piece_tid();
      if (OB_INVALID_ID == mtid || OB_INVALID_ID == ptid) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Expect meta tid and piece tid valid",
                KR(ret), K(mtid), K(ptid));
      } else if (OB_FAIL(table_ids.push_back(mtid))) {
      } else if (OB_FAIL(table_ids.push_back(ptid))) {
      }
    }
    // cal tablet cost
    if (OB_SUCC(ret) && 0 != table_ids.count()) {
      const ObSimpleTableSchemaV2 *tmp_table_schema = NULL;
      const int64_t table_count = table_ids.count();

      for (int64_t i = 0; OB_SUCC(ret) && i < table_count; ++i) {
        int64_t table_id = table_ids.at(i);
        if (OB_FAIL(schema_guard.get_simple_table_schema( table_id, tmp_table_schema))) {
        } else if (OB_ISNULL(tmp_table_schema)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("table schema is NULL", KR(ret), K(table_id));
        } else {
          part_num += tmp_table_schema->get_all_part_num();
        }
      }
    }
    // has autoinc
    if (OB_SUCC(ret) && 0 != orig_table_schema->get_autoinc_column_id()) {
      cal_table_timeout += GCONF.rpc_timeout;
    }
    // has trigger
    if (OB_SUCC(ret)) {
      const ObIArray<uint64_t> &trigger_id_list = orig_table_schema->get_trigger_list();
      cal_table_timeout += trigger_id_list.count() * GCONF.rpc_timeout;
    }
    if (OB_SUCC(ret)) {
      //100 tablet 2s,default 2s
      cal_table_timeout += (part_num / 100 + (part_num % 100 == 0 ? 0 : 1)) * GCONF.rpc_timeout;
    }
  }
  return ret;
}

int ObMultiVersionSchemaService::cal_purge_database_timeout_(
    const uint64_t &database_id,
    int64_t &cal_database_timeout,
    int64_t &total_purge_count)
{
  int ret = OB_SUCCESS;
  int64_t part_num = 0;
  cal_database_timeout = 0;
  ObSchemaGetterGuard schema_guard;
  ObArray<ObRecycleObject> recycle_objs;
  bool need_cal_timeout = true;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == database_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("arg is not valid", KR(ret), K(database_id));
  } else if (OB_FAIL(get_runtime_schema_guard(schema_guard))) {
  } else {
    const ObSimpleDatabaseSchema *database_schema = NULL;
    if (OB_FAIL(schema_guard.get_database_schema( database_id, database_schema))) {
    } else if (OB_ISNULL(database_schema)) {
      need_cal_timeout = false;
    }
  }
  if (OB_SUCC(ret) && need_cal_timeout) {
    total_purge_count++;
    schema_guard.reset();
    // database itself
    cal_database_timeout += GCONF.rpc_timeout;
    // cal table which is already in recyclebin
    if (OB_FAIL(schema_service_->fetch_recycle_objects_of_db(database_id,
                                                            *sql_proxy_,
                                                            recycle_objs))) {
    } else {
      for (int i = 0; OB_SUCC(ret) && i < recycle_objs.count(); ++i) {
        int64_t tmp_count = 0;
        int64_t tmp_table_timeout = 0;
        const ObRecycleObject &recycle_obj = recycle_objs.at(i);
        const uint64_t table_id = recycle_obj.get_table_id();
        if (OB_FAIL(cal_purge_table_timeout_(table_id, tmp_table_timeout, tmp_count))) {
        } else {
          cal_database_timeout += tmp_table_timeout;
        }
      }
    }
    // to prevent schema memory hang, we should use get_runtime_schema_guard to reuse memory
    // cal delete tables in database
    if (OB_SUCC(ret)) {
      ObArray<uint64_t> table_ids;
      if (OB_FAIL(get_runtime_schema_guard(schema_guard))) {
      } else if (OB_FAIL(schema_guard.get_table_ids_in_database(database_id, table_ids))) {
      } else {
        schema_guard.reset();
        for (int64_t i = 0; OB_SUCC(ret) && i < table_ids.count(); ++i) {
          int64_t tmp_count = 0;
          int64_t tmp_table_timeout = 0;
          uint64_t table_id = table_ids.at(i);
          if (OB_FAIL(cal_purge_table_timeout_(table_id, tmp_table_timeout, tmp_count))) {
          } else {
            cal_database_timeout += tmp_table_timeout;
          }
        }
      }
    }
    // cal outline
    if (OB_SUCC(ret)) {
      ObArray<const ObSimpleOutlineSchema *> outlines;
      if (OB_FAIL(get_runtime_schema_guard(schema_guard))) {
      } else if (OB_FAIL(schema_guard.get_simple_outline_schemas_in_database(database_id, outlines))) {
      } else {
        cal_database_timeout += outlines.count() * GCONF.rpc_timeout;
      }
    }
    // cal packags
    if (OB_SUCC(ret)) {
      ObArray<const ObSimplePackageSchema *> packages;
      if (OB_FAIL(get_runtime_schema_guard(schema_guard))) {
      } else if (OB_FAIL(schema_guard.get_simple_package_schemas_in_database(database_id, packages))) {
      } else {
        cal_database_timeout += packages.count() * GCONF.rpc_timeout;
      }
    }
    // cal routines
    if (OB_SUCC(ret)) {
      ObArray<const ObSimpleRoutineSchema *> routines;
      if (OB_FAIL(get_runtime_schema_guard(schema_guard))) {
      } else if (OB_FAIL(schema_guard.get_simple_routine_schemas_in_database(database_id, routines))) {
      } else {
        cal_database_timeout += routines.count() * GCONF.rpc_timeout;
      }
    }
    // cal mock_fk
    if (OB_SUCC(ret)) {
      ObArray<const ObSimpleMockFKParentTableSchema *> mock_fk_parent_table_schemas;
      if (OB_FAIL(get_runtime_schema_guard(schema_guard))) {
      } else if (OB_FAIL(schema_guard.get_simple_mock_fk_parent_table_schemas_in_database(database_id, mock_fk_parent_table_schemas))) {
      } else {
        cal_database_timeout += mock_fk_parent_table_schemas.count() * GCONF.rpc_timeout;
      }
    }
  }
  return ret;
}

int ObMultiVersionSchemaService::batch_fetch_tablet_to_table_history_(const ObIArray<ObTabletID> &tablet_ids,
    const int64_t schema_version,
    const ObIArray<int64_t> &tablet_idxs,
    const int64_t start_idx,
    const int64_t end_idx,
    ObHashMap<ObTabletID, uint64_t> &tablet_map)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!check_inner_stat())) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_UNLIKELY(start_idx < 0
             || end_idx - start_idx <= 0
             || end_idx > tablet_idxs.count()
             || tablet_ids.count() <= 0
             || tablet_idxs.count() <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", KR(ret), K(start_idx), K(end_idx),
             "tablet_ids_cnt", tablet_ids.count(), "tablet_idxs_cnt", tablet_idxs.count());
  } else if (OB_ISNULL(sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sql_proxy is null", KR(ret));
  } else {
    SMART_VAR(ObMySQLProxy::MySQLResult, res) {
      common::sqlclient::ObMySQLResult *result = NULL;
      ObSqlString sql;
      ObSqlString tablet_ids_sql;
      for (int64_t i = start_idx; OB_SUCC(ret) && i < end_idx; i++) {
        int64_t idx = tablet_idxs.at(i);
        if (idx < 0 || idx >= tablet_ids.count()) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("idx is invalid", KR(ret), K(idx),
                   "tablet_ids_cnt", tablet_ids.count());
        } else if (OB_FAIL(tablet_ids_sql.append_fmt("%s%lu",
                   i == start_idx ? "" : ", ", tablet_ids.at(idx).id()))) {
        }
      } // end for

      if (FAILEDx(sql.assign_fmt(
          "SELECT * FROM (SELECT *, row_number() "
          "OVER (PARTITION BY tablet_id ORDER BY schema_version DESC) AS row_num "
          "FROM %s WHERE tablet_id in (%.*s) AND schema_version <= %ld) "
          "WHERE row_num = 1",
          OB_ALL_TABLET_TO_TABLE_HISTORY_TNAME,
          tablet_ids_sql.string().length(),
          tablet_ids_sql.string().ptr(),
          schema_version))) {
        LOG_WARN("fail to assign fmt", KR(ret), K(schema_version));
      } else if (OB_FAIL(sql_proxy_->read(res, sql.ptr()))) {
      } else if (OB_ISNULL(result = res.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("result is null", KR(ret), K(sql));
      } else {
        ObTabletID tablet_id;
        uint64_t id = OB_INVALID_ID;
        uint64_t table_id = OB_INVALID_ID;
        bool is_deleted = false;
        ObTabletCacheKey key;
        ObTabletCacheValue value;
        while (OB_SUCC(ret) && OB_SUCC(result->next())) {
          EXTRACT_INT_FIELD_MYSQL(*result, "is_deleted", is_deleted, bool);
          EXTRACT_INT_FIELD_MYSQL(*result, "tablet_id", id, uint64_t);
          EXTRACT_INT_FIELD_MYSQL(*result, "table_id", table_id, int64_t);
          if (OB_SUCC(ret) && is_deleted) { // tablet has been dropped
            table_id = OB_INVALID_ID;
          }
          tablet_id = id;
          if (FAILEDx(key.init(tablet_id, schema_version))) {
            LOG_WARN("fail to init key", KR(ret), K(tablet_id), K(schema_version));
          } else if (OB_FAIL(tablet_map.set_refactored(tablet_id, table_id))) {
          } else if (OB_FAIL(schema_cache_.put_tablet_cache(key, table_id))) {
          }
        } // end while

        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
        } else {
          ret = OB_SUCC(ret) ? OB_ERR_UNEXPECTED : ret;
          LOG_WARN("fail to get result", KR(ret), K(sql));
        }
      }
    } // end SMART_VAR
  }
  return ret;
}


}//end of namespace schema
}//end of namespace share
}//end of namespace oceanbase
