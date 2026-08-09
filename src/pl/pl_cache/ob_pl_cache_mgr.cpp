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

#define USING_LOG_PREFIX PL_CACHE
#include "ob_pl_cache_mgr.h"
#include "src/sql/plan_cache/ob_plan_cache_util.h"
using namespace oceanbase::observer;

namespace oceanbase
{
namespace pl
{
/* INFLUENCE_PL system variables that still affect current MySQL behavior. */
static constexpr int64_t PL_CACHE_SYS_VAR_COUNT = 1;
static constexpr share::ObSysVarClassType InfluencePLMap[PL_CACHE_SYS_VAR_COUNT + 1] = {
  share::SYS_VAR_DIV_PRECISION_INCREMENT,
  share::SYS_VAR_INVALID
};

int ObPLCacheMgr::get_sys_var_in_pl_cache_str(ObBasicSessionInfo &session,
                                              ObIAllocator &allocator,
                                              ObString &sys_var_str)
{
  int ret = OB_SUCCESS;
  const int64_t MAX_SYS_VARS_STR_SIZE = 256;
  ObObj val;
  ObSysVarInPC sys_vars;
  char *buf = nullptr;
  int64_t pos = 0;

  for (int64_t i = 0; OB_SUCC(ret) && i < PL_CACHE_SYS_VAR_COUNT; ++i) {
    val.reset();
    if (OB_FAIL(session.get_sys_variable(InfluencePLMap[i], val))) {
    } else if (OB_FAIL(sys_vars.push_back(val))) {
    }
  }
  if (OB_SUCC(ret)) {
    int64_t sys_var_encode_max_size = MAX_SYS_VARS_STR_SIZE;
    if (nullptr == (buf = (char *)allocator.alloc(MAX_SYS_VARS_STR_SIZE))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to allocator memory", K(ret), K(MAX_SYS_VARS_STR_SIZE));
    } else if (OB_FAIL(sys_vars.serialize_sys_vars(buf, sys_var_encode_max_size, pos))) {
      if (OB_BUF_NOT_ENOUGH == ret || OB_SIZE_OVERFLOW ==ret) {
        ret = OB_SUCCESS;
        // expand MAX_SYS_VARS_STR_SIZE 3 times.
        for (int64_t i = 0; OB_SUCC(ret) && i < 3; ++i) {
          sys_var_encode_max_size = 2 * sys_var_encode_max_size;
          if (NULL == (buf = (char *)allocator.alloc(sys_var_encode_max_size))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("fail to allocator memory", K(ret), K(sys_var_encode_max_size));
          } else if (OB_FAIL(sys_vars.serialize_sys_vars(buf, sys_var_encode_max_size, pos))) {
            if (i != 2 && (OB_BUF_NOT_ENOUGH == ret || OB_SIZE_OVERFLOW ==ret)) {
              ret = OB_SUCCESS;
            } else {
              LOG_WARN("fail to serialize system vars", K(ret));
            }
          } else {
            break;
          }
        }
      } else {
        LOG_WARN("fail to serialize system vars", K(ret));
      }
      if (OB_SUCC(ret)) {
        (void)sys_var_str.assign(buf, int32_t(pos));
      }
    } else {
      (void)sys_var_str.assign(buf, int32_t(pos));
    }
  }

  return ret;
}

int ObPLCacheMgr::get_pl_object(ObPlanCache *lib_cache, ObILibCacheCtx &ctx, ObCacheObjGuard& guard)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator tmp_alloc(GET_PL_MOD_STRING(PL_MOD_IDX::OB_PL_ARENA), OB_MALLOC_NORMAL_BLOCK_SIZE);
  ObPLCacheCtx &pc_ctx = static_cast<ObPLCacheCtx&>(ctx);
  ObGlobalReqTimeService::check_req_timeinfo();
  if (OB_ISNULL(lib_cache) || OB_ISNULL(pc_ctx.session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("lib cache is null");
  } else if (OB_FAIL(get_sys_var_in_pl_cache_str(*pc_ctx.session_info_, tmp_alloc, pc_ctx.key_.sys_vars_str_))) {
  } else {
    if (OB_FAIL(lib_cache->get_cache_obj(ctx, &pc_ctx.key_, guard))) {
      // if schema expired, update pl cache;
      if (OB_OLD_SCHEMA_VERSION == ret) {
        PL_CACHE_LOG(WARN, "start to remove pl object", K(ret), K(pc_ctx.key_));
        if (OB_FAIL(lib_cache->remove_cache_node(&pc_ctx.key_))) {
        } else {
          ret = OB_SQL_PC_NOT_EXIST;
        }
      }
    } else if (OB_ISNULL(guard.get_cache_obj()) ||
              (!guard.get_cache_obj()->is_prcr() &&
                !guard.get_cache_obj()->is_sfc() &&
                !guard.get_cache_obj()->is_pkg() &&
                !guard.get_cache_obj()->is_anon() &&
                guard.get_cache_obj()->get_ns() != ObLibCacheNameSpace::NS_CALLSTMT)) {
      ret = OB_ERR_UNEXPECTED;
      PL_CACHE_LOG(WARN, "cache obj is invalid", KPC(guard.get_cache_obj()));
    }

    if (OB_FAIL(ret) && OB_NOT_NULL(guard.get_cache_obj())) {
      int tmp_ret = guard.force_early_release(lib_cache);
      if (OB_SUCCESS != tmp_ret) {
      }
    }
    if (OB_SUCC(ret) && OB_NOT_NULL(guard.get_cache_obj())) {
      lib_cache->inc_hit_and_access_cnt();
    } else {
      lib_cache->inc_access_cnt();
    }
    pc_ctx.key_.sys_vars_str_.reset();
  }
  return ret;
}

int ObPLCacheMgr::get_pl_cache(ObPlanCache *lib_cache, ObCacheObjGuard& guard, ObPLCacheCtx &pc_ctx)
{
  int ret = OB_SUCCESS;
  ObGlobalReqTimeService::check_req_timeinfo();
  if (OB_NOT_NULL(pc_ctx.session_info_) &&
      false == pc_ctx.session_info_->get_local_ob_enable_pl_cache()) {
    // do nothing
  } else if (OB_FAIL(pc_ctx.adjust_definer_database_id())) {
  } else if (OB_FAIL(get_pl_object(lib_cache, pc_ctx, guard))) {
  } else if (OB_ISNULL(guard.get_cache_obj())) {
    ret = OB_ERR_UNEXPECTED;
    PL_CACHE_LOG(WARN, "cache obj is invalid", KPC(guard.get_cache_obj()));
  } else {
    // update pl func/package stat
    pl::PLCacheObjStat *stat = NULL;
    int64_t current_time = ObTimeUtility::current_time();
    pl::ObPLCacheObject* pl_object = static_cast<pl::ObPLFunction*>(guard.get_cache_obj());
    stat = &pl_object->get_stat_for_update();
    ATOMIC_INC(&(stat->hit_count_));
    ATOMIC_STORE(&(stat->last_active_time_), current_time);
  }
  return ret;
}

int ObPLCacheMgr::add_pl_object(ObPlanCache *lib_cache,
                                      ObILibCacheCtx &ctx,
                                      ObILibCacheObject *cache_obj)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator tmp_alloc(GET_PL_MOD_STRING(PL_MOD_IDX::OB_PL_ARENA), OB_MALLOC_NORMAL_BLOCK_SIZE);
  ObPLCacheCtx &pc_ctx = static_cast<ObPLCacheCtx&>(ctx);
  if (OB_ISNULL(cache_obj)) {
    ret = OB_INVALID_ARGUMENT;
    PL_CACHE_LOG(WARN, "invalid cache obj", K(ret));
  } else if (OB_ISNULL(lib_cache) || OB_ISNULL(pc_ctx.session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("lib cache is null");
  } else if (OB_FAIL(get_sys_var_in_pl_cache_str(*pc_ctx.session_info_, tmp_alloc, pc_ctx.key_.sys_vars_str_))) {
  } else {
    pl::PLCacheObjStat *stat = NULL;
    pl::ObPLCacheObject* pl_object = static_cast<pl::ObPLCacheObject*>(cache_obj);
    stat = &pl_object->get_stat_for_update();
    ATOMIC_STORE(&(stat->db_id_), pc_ctx.key_.db_id_);
    do {
      if (OB_FAIL(lib_cache->add_cache_obj(ctx, &pc_ctx.key_, cache_obj)) && OB_OLD_SCHEMA_VERSION == ret) {
        PL_CACHE_LOG(INFO, "schema in pl cache value is old, start to remove pl object", K(ret), K(pc_ctx.key_));
      }
      if (ctx.need_destroy_node_) {
        PL_CACHE_LOG(WARN, "fail to add cache obj, need destroy node", K(ret), K(pc_ctx.key_));
        int tmp_ret = OB_SUCCESS;
        if (OB_SUCCESS != (tmp_ret = lib_cache->remove_cache_node(&pc_ctx.key_))) {
          ret = tmp_ret;
          PL_CACHE_LOG(WARN, "fail to remove lib cache node", K(ret));
        }
      }
    } while (OB_OLD_SCHEMA_VERSION == ret);
    pc_ctx.key_.sys_vars_str_.reset();
  }
  return ret;
}

int ObPLCacheMgr::add_pl_cache(ObPlanCache *lib_cache, ObILibCacheObject *pl_object, ObPLCacheCtx &pc_ctx)
{
  int ret = OB_SUCCESS;
  ObGlobalReqTimeService::check_req_timeinfo();
  if (OB_ISNULL(lib_cache)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("lib cache is null");
  } else if (OB_NOT_NULL(pc_ctx.session_info_) &&
              false == pc_ctx.session_info_->get_local_ob_enable_pl_cache()) {
    // do nothing
  } else if (OB_ISNULL(pl_object)) {
     ret = OB_INVALID_ARGUMENT;
     PL_CACHE_LOG(WARN, "invalid physical plan", K(ret));
  } else if (lib_cache->get_mem_hold() > lib_cache->get_mem_limit()) {
     ret = OB_REACH_MEMORY_LIMIT;
     PL_CACHE_LOG(WARN, "lib cache memory used reach the high water mark",
     K(lib_cache->get_mem_used()), K(lib_cache->get_mem_limit()), K(ret));
  } else if (pl_object->get_mem_size() >= lib_cache->get_mem_high()) {
    // do nothing
  } else {
    ObLibCacheNameSpace ns = NS_INVALID;
    switch (pl_object->get_ns()) {
      case NS_PRCR:
      case NS_SFC: {
        ns = NS_PRCR;
      }
        break;
      case NS_PKG:
      case NS_ANON:
      case NS_CALLSTMT: {
        ns = pl_object->get_ns();
      }
        break;
      default: {
        ret = OB_ERR_UNEXPECTED;
        PL_CACHE_LOG(WARN, "pl object to cache is not valid", K(pl_object->get_ns()), K(ret));
      }
      break;
    }
    if (OB_FAIL(ret)) {
    } else if (FALSE_IT(pc_ctx.key_.namespace_ = ns)) {
    } else if (OB_FAIL(pc_ctx.adjust_definer_database_id())) {
    } else if (OB_FAIL(add_pl_object(lib_cache, pc_ctx, pl_object))) {
      if (!is_not_supported_err(ret)
          && OB_SQL_PC_PLAN_DUPLICATE != ret) {
        PL_CACHE_LOG(WARN, "fail to add pl function", K(ret));
      }
    }
  }
  return ret;
}

int ObPLCacheMgr::flush_pl_cache_by_sql(
                                  uint64_t key_id,
                                  uint64_t db_id,
                                  share::schema::ObMultiVersionSchemaService & schema_service)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard runtime_schema_guard;
  ObString db_name;
  if (OB_FAIL(schema_service.get_runtime_schema_guard(runtime_schema_guard))) {
  }

  const ObSimpleDatabaseSchema *database_schema = NULL;
  if (OB_FAIL(ret)) {
    // do nothing
  } else if (OB_FAIL(runtime_schema_guard.get_database_schema( db_id, database_schema))) {
  } else if (OB_ISNULL(database_schema)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("database schema is null", K(ret));
  } else {
    db_name = database_schema->get_database_name();
  }

  ObMySQLProxy *sql_proxy = nullptr;
  ObSqlString sql;
  int64_t affected_rows = 0;
  if (OB_FAIL(ret)) {
    // do nothing
  } else if (OB_ISNULL(sql_proxy = GCTX.sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected sql proxy", K(ret));
  } else if (OB_FAIL(sql.assign_fmt("alter system flush pl cache schema_id = %lu databases = \"%.*s\"",
                                      key_id, db_name.length(), db_name.ptr()))) {
  } else {
    if (OB_FAIL(sql_proxy->write(sql.ptr(), affected_rows))) {
    } else {
      // do nothing
      LOG_INFO("succ to flush pl cache", K(key_id), K(affected_rows));
    }
  }
  return ret;
}  

// delete all pl cache obj
int ObPLCacheMgr::cache_evict_all_pl(ObPlanCache *lib_cache)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(lib_cache)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("lib cache is null");
  } else {
    LCKeyValueArray to_evict_keys;
    ObGetPLKVEntryOp get_ids_op(&to_evict_keys);
    if (OB_FAIL(lib_cache->foreach_cache_evict(get_ids_op))) {
    }
  }

  return ret;
}

template<typename GETPLKVEntryOp, typename EvictAttr>
int ObPLCacheMgr::cache_evict_pl_cache_single(ObPlanCache *lib_cache, uint64_t db_id, EvictAttr &attr)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(lib_cache)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("lib cache is null");
  } else {
    LCKeyValueArray to_evict_keys;
    GETPLKVEntryOp get_ids_op(db_id, attr, &to_evict_keys);
    if (OB_FAIL(lib_cache->foreach_cache_evict(get_ids_op))) {
    }
  }
  return ret;
}

template int ObPLCacheMgr::cache_evict_pl_cache_single<ObGetPLKVEntryBySchemaIdOp, uint64_t>(ObPlanCache *lib_cache, uint64_t db_id, uint64_t &schema_id);
template int ObPLCacheMgr::cache_evict_pl_cache_single<ObGetPLKVEntryByDbIdOp, uint64_t>(ObPlanCache *lib_cache, uint64_t db_id, uint64_t &schema_id);
template int ObPLCacheMgr::cache_evict_pl_cache_single<ObGetPLKVEntryBySQLIDOp, common::ObString>(ObPlanCache *lib_cache, uint64_t db_id, common::ObString &sql_id);

}
}
