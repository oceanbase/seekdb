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

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/cmd/ob_alter_system_executor.h"
#include "share/rc/ob_module_provider.h"
#include "rootserver/ob_rs_serial_call.h"
#include "share/ob_ex_rpc.h"
#include "share/io/ob_io_manager.h"
#include "storage/meta_store/ob_server_storage_meta_service.h"
#include "storage/meta_store/ob_tenant_storage_meta_service.h"
#include "observer/ob_server.h"
#include "observer/scheduler/ob_dag_warning_history_mgr.h"
#include "observer/omt/ob_tenant.h" //ObTenant
#include "rootserver/freeze/ob_major_freeze_helper.h" //ObMajorFreezeHelper
#include "pl/pl_cache/ob_pl_cache_mgr.h"
#include "sql/plan_cache/ob_ps_cache.h"
#include "storage/fts/ob_fts_plugin_helper.h"
#include "storage/fts/dict/ob_ft_dict_hub.h"

#include "rootserver/ob_tenant_event_def.h"
#include "sql/engine/cmd/ob_redis_importer.h"
#include "sql/engine/cmd/ob_timezone_importer.h"
#include "sql/engine/cmd/ob_srs_importer.h"
#include "share/ob_internal_table_change_notifier.h"

namespace oceanbase
{
using namespace common;
using namespace obcall;
using namespace share;
using namespace omt;
using namespace obmysql;
using namespace tenant_event;
namespace sql
{
int ObFreezeExecutor::execute(ObExecContext &ctx, ObFreezeStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else {
    if (!stmt.is_major_freeze()) {
      ObRootMinorFreezeArg arg;
      if (OB_FAIL(arg.server_list_.assign(stmt.get_server_list()))) {
        LOG_WARN("failed to assign server_list", K(ret));
      } else {
        arg.zone_ = stmt.get_zone();
        arg.tablet_id_ = stmt.get_tablet_id();
        arg.ls_id_ = stmt.get_ls_id();
      }
      // access check:
      // not allow user_tenant to freeze other tenants
      if (OB_SUCC(ret)) {
        if (OB_FAIL(GCTX.root_service_->root_minor_freeze(arg))) {
          LOG_WARN("minor freeze failed", K(arg), K(ret), "dst", GCTX.self_addr());
        }
      }
    } else if (stmt.get_tablet_id().is_valid()) {
      if (OB_UNLIKELY(1 != stmt.get_tenant_count())) {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("not support schedule tablet major freeze for several tenant", K(ret), K(stmt));
      } else {
        rootserver::ObTabletMajorFreezeParam param;
        
        param.tablet_id_ = stmt.get_tablet_id();
        param.is_rebuild_column_group_ = stmt.is_rebuild_column_group();
        if (OB_FAIL(rootserver::ObMajorFreezeHelper::tablet_major_freeze(param))) {
          LOG_WARN("failed to schedule tablet major freeze", K(ret), K(param));
        }
      }
    } else { // tenant major freeze
      rootserver::ObMajorFreezeParam param;
      param.freeze_all_ = stmt.is_freeze_all();
      param.freeze_all_user_ = stmt.is_freeze_all_user();
      param.freeze_all_meta_ = stmt.is_freeze_all_meta();
      param.freeze_reason_ = rootserver::MF_USER_REQUEST;
      for (int64_t i = 0; i < stmt.get_tenant_count() && OB_SUCC(ret); ++i) {
        if (OB_FAIL(param.add_freeze_info())) {
          LOG_WARN("fail to assign", KR(ret));
        }
      }
      if (OB_SUCC(ret)) {
        ObArray<int> merge_results; // save each tenant's major_freeze result, so use 'int' type
        if (OB_FAIL(rootserver::ObMajorFreezeHelper::major_freeze(param, merge_results))) {
          LOG_WARN("fail to major freeze", KR(ret), K(param), K(merge_results));
        } else if (merge_results.count() > 0) {
          bool is_frozen_exist = false;
          bool is_merge_not_finish = false;
          for (int64_t i = 0; i < merge_results.count(); ++i) {
            if (OB_FROZEN_INFO_ALREADY_EXIST == merge_results.at(i)) {
              is_frozen_exist = true;
            } else if (OB_MAJOR_FREEZE_NOT_FINISHED == merge_results.at(i)) {
              is_merge_not_finish = true;
            }
          }

          if (is_frozen_exist || is_merge_not_finish) {
            char buf[1024] = "larger frozen_scn already exist, some tenants' prev merge may not finish";
            if (merge_results.count() > 1) {
              LOG_USER_WARN(OB_FROZEN_INFO_ALREADY_EXIST, buf);
            } else {
              STRCPY(buf, "larger frozen_scn already exist, prev merge may not finish");
              LOG_USER_WARN(OB_FROZEN_INFO_ALREADY_EXIST, buf);
            }
          }
        }
        LOG_INFO("finish do major freeze", KR(ret), K(param), K(merge_results));
      }
    }
  }
  return ret;
}

int ObFlushCacheExecutor::execute(ObExecContext &ctx, ObFlushCacheStmt &stmt)
{
  int ret = OB_SUCCESS;
  if (!stmt.is_global_) { // flush local
    int64_t tenant_num = stmt.flush_cache_arg_.batch_ids_.count();
    int64_t db_num = stmt.flush_cache_arg_.db_ids_.count();
    common::ObString sql_id = stmt.flush_cache_arg_.sql_id_;
    switch (stmt.flush_cache_arg_.cache_type_) {
      case CACHE_TYPE_LIB_CACHE: {
        if (stmt.flush_cache_arg_.ns_type_ != ObLibCacheNameSpace::NS_INVALID) {
          ObLibCacheNameSpace ns = stmt.flush_cache_arg_.ns_type_;
          if (0 == tenant_num) { // purge in tenant level, aka. coarse-grained plan evict
            if (OB_ISNULL(GCTX.omt_)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("unexpected null of GCTX.omt_", K(ret));
            } else {
              MOD_SCOPE {
                ObPlanCache* plan_cache = share::g_mp->plan_cache();
                ret = plan_cache->flush_lib_cache_by_ns(ns);
              }
              // ignore errors at switching tenant
              ret = OB_SUCCESS;
            }
          } else {
            for (int64_t i = 0; i < tenant_num; ++i) { //ignore ret
              MOD_SCOPE {
                ObPlanCache* plan_cache = share::g_mp->plan_cache();
                ret = plan_cache->flush_lib_cache_by_ns(ns);
              }
            }
          }
        } else {
          if (0 == tenant_num) { // purge in tenant level, aka. coarse-grained plan evict
            if (OB_ISNULL(GCTX.omt_)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("unexpected null of GCTX.omt_", K(ret));
            } else {
              MOD_SCOPE {
                ObPlanCache* plan_cache = share::g_mp->plan_cache();
                ret = plan_cache->flush_lib_cache();
              }
              // ignore errors at switching tenant
              ret = OB_SUCCESS;
            }
          } else {
            for (int64_t i = 0; i < tenant_num; ++i) { //ignore ret
              MOD_SCOPE {
                ObPlanCache* plan_cache = share::g_mp->plan_cache();
                ret = plan_cache->flush_lib_cache();
              }
            }
          }
        }
        break;
      }
      case CACHE_TYPE_PLAN: {
        if (stmt.flush_cache_arg_.is_fine_grained_) {
          // purge in sql_id level, aka. fine-grained plan evict
          // we assume tenant_list must not be empty and this will be checked in resolve phase
          if (0 == tenant_num) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected tenant_list in fine-grained plan evict", K(tenant_num));
          } else {
            for (int64_t i = 0; i < tenant_num; i++) { // ignore ret
              int64_t t_id = stmt.flush_cache_arg_.batch_ids_.at(i);
              MOD_SCOPE {
                ObPlanCache* plan_cache = share::g_mp->plan_cache();
                // not specified db_name, evict all dbs
                if (db_num == 0) {
                  ret = plan_cache->flush_plan_cache_by_sql_id(OB_INVALID_ID, sql_id);
                } else { // evict db by db
                  for(int64_t j = 0; j < db_num; j++) { // ignore ret
                    ret = plan_cache->flush_plan_cache_by_sql_id(stmt.flush_cache_arg_.db_ids_.at(j), sql_id);
                  }
                }
              }
            }
          }
        } else if (0 == tenant_num) { // purge in tenant level, aka. coarse-grained plan evict
          if (OB_ISNULL(GCTX.omt_)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected null of GCTX.omt_", K(ret));
          } else {
            MOD_SCOPE {
              ObPlanCache* plan_cache = share::g_mp->plan_cache();
              ret = plan_cache->flush_plan_cache();
            }
            // ignore errors at switching tenant
            ret = OB_SUCCESS;
          }
        } else {
          for (int64_t i = 0; OB_SUCC(ret) && i < tenant_num; ++i) { //ignore ret
            MOD_SCOPE {
              ObPlanCache* plan_cache = share::g_mp->plan_cache();
              ret = plan_cache->flush_plan_cache();
            }
          }
        }
        break;
      }
      case CACHE_TYPE_PL_OBJ: {
        if (stmt.flush_cache_arg_.is_fine_grained_) {
          // purge in sql_id level, aka. fine-grained plan evict
          // we assume tenant_list must not be empty and this will be checked in resolve phase
          if (0 == tenant_num) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected tenant_list in fine-grained plan evict", K(tenant_num));
          } else {
            bool is_evict_by_schema_id = common::OB_INVALID_ID != stmt.flush_cache_arg_.schema_id_;
            for (int64_t i = 0; i < tenant_num; i++) { // ignore ret
              int64_t t_id = stmt.flush_cache_arg_.batch_ids_.at(i);
              MOD_SCOPE {
                ObPlanCache* plan_cache = share::g_mp->plan_cache();
                // not specified db_name, evict all dbs
                if (db_num == 0) {
                  if (is_evict_by_schema_id) {
                    ret = plan_cache->flush_pl_cache_single_cache_obj<pl::ObGetPLKVEntryBySchemaIdOp>(OB_INVALID_ID, stmt.flush_cache_arg_.schema_id_);
                  } else {
                    ret = plan_cache->flush_pl_cache_single_cache_obj<pl::ObGetPLKVEntryBySQLIDOp>(OB_INVALID_ID, sql_id);
                  }
                } else { // evict db by db
                  for(int64_t j = 0; j < db_num; j++) { // ignore ret
                    if (is_evict_by_schema_id) {
                      ret = plan_cache->flush_pl_cache_single_cache_obj<pl::ObGetPLKVEntryBySchemaIdOp>(stmt.flush_cache_arg_.db_ids_.at(j), stmt.flush_cache_arg_.schema_id_);
                    } else if(OB_ISNULL(sql_id)){
                      ret = plan_cache->flush_pl_cache_single_cache_obj<pl::ObGetPLKVEntryByDbIdOp, uint64_t>(stmt.flush_cache_arg_.db_ids_.at(j), stmt.flush_cache_arg_.schema_id_);
                    } else {
                      ret = plan_cache->flush_pl_cache_single_cache_obj<pl::ObGetPLKVEntryBySQLIDOp>(stmt.flush_cache_arg_.db_ids_.at(j), sql_id);
                    }
                  }
                }
              }
            }
          }
        } else if (0 == tenant_num) {
          if (OB_ISNULL(GCTX.omt_)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected null of GCTX.omt_", K(ret));
          } else {
            MOD_SCOPE {
              ObPlanCache* plan_cache = share::g_mp->plan_cache();
              ret = plan_cache->flush_pl_cache();
            }
            // ignore errors at switching tenant
            ret = OB_SUCCESS;
          }
        } else {
          for (int64_t i = 0; i < tenant_num; i++) { // ignore internal err code
            MOD_SCOPE {
              ObPlanCache* plan_cache = share::g_mp->plan_cache();
              ret = plan_cache->flush_pl_cache();
            }
          }
        }
        break;
      }
      case CACHE_TYPE_PS_OBJ: {
        if (0 == tenant_num) {
          if (OB_ISNULL(GCTX.omt_)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected null of GCTX.omt_", K(ret));
          } else {
            MOD_SCOPE {
              ObPsCache* ps_cache = share::g_mp->ps_cache();
              if (ps_cache->is_inited()) {
                ret = ps_cache->cache_evict_all_ps();
              }
            }
            // ignore errors at switching tenant
            ret = OB_SUCCESS;
          }
        } else {
          for (int64_t i = 0; i < tenant_num; i++) { // ignore internal err code
            MOD_SCOPE {
              ObPsCache* ps_cache = share::g_mp->ps_cache();
              if (ps_cache->is_inited()) {
                ret = ps_cache->cache_evict_all_ps();
              }
            }
          }
        }
        break;
      }
      //case CACHE_TYPE_BALANCE: {
      //  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
      //  ObCommonRpcProxy *common_rpc_proxy = NULL;

      //  if (OB_ISNULL(task_exec_ctx)) {
      //    ret = OB_NOT_INIT;
      //    LOG_WARN("get task executor context failed");
      //  } else if (OB_FAIL(task_exec_ctx->get_common_rpc(common_rpc_proxy))) {
      //    LOG_WARN("get common rpc proxy failed", K(ret));
      //  } else if (OB_ISNULL(common_rpc_proxy)) {
      //    ret = OB_ERR_UNEXPECTED;
      //    LOG_WARN("common_rpc_proxy is null", K(ret));
      //  }
      //  break;
      //}
      case CACHE_TYPE_ALL:
      case CACHE_TYPE_COLUMN_STAT:
      case CACHE_TYPE_BLOCK_INDEX:
      case CACHE_TYPE_BLOCK:
      case CACHE_TYPE_ROW:
      case CACHE_TYPE_BLOOM_FILTER:
      case CACHE_TYPE_CLOG:
      case CACHE_TYPE_ILOG:
      case CACHE_TYPE_SCHEMA: {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("cache type not supported flush",
                 "type", stmt.flush_cache_arg_.cache_type_,
                 K(ret));
      } break;
      case CACHE_TYPE_LOCATION: {
        // TODO: @wangzhennan.wzn
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("location cache not supported to flush");
      } break;
      default: {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid cache type", "type", stmt.flush_cache_arg_.cache_type_);
      }
    }
  } else { // flush global
    ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
    if (OB_ISNULL(task_exec_ctx)) {
      ret = OB_NOT_INIT;
      LOG_WARN("get task executor context failed");
    } else if (OB_FAIL(GCTX.root_service_->admin_flush_cache(
                           stmt.flush_cache_arg_))) {
      LOG_WARN("flush cache failed", K(ret), "rpc_arg", stmt.flush_cache_arg_);
    }
  }
  return ret;
}

int ObRefreshFullTextDictExecutor::execute(ObExecContext &ctx, ObRefreshFullTextDictStmt &stmt)
{
  UNUSED(ctx);
  int ret = OB_SUCCESS;
  storage::ObFTDictHub *hub = NULL;
  if (OB_UNLIKELY(stmt.dict_table_name_.is_empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("dict table name is empty", K(ret));
  } else if (OB_FAIL(storage::ObFTParsePluginData::instance().get_dict_hub(hub))) {
    LOG_WARN("fail to get dict hub", K(ret));
  } else if (OB_ISNULL(hub)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dict hub is null", K(ret));
  } else if (OB_FAIL(hub->invalidate_dict(stmt.dict_table_name_.str()))) {
    LOG_WARN("fail to invalidate dict", K(ret), K(stmt.dict_table_name_));
  } else {
    LOG_INFO("success to refresh fulltext dict", K(stmt.dict_table_name_));
  }
  return ret;
}

int ObFlushKVCacheExecutor::execute(ObExecContext &ctx, ObFlushKVCacheStmt &stmt)
{
  UNUSED(stmt);
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else {
    share::schema::ObSchemaGetterGuard schema_guard;
    if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(
                schema_guard))) {
      LOG_WARN("get_schema_guard failed", K(ret));
    } else {
      if (stmt.cache_name_.is_empty()) {
        if (OB_FAIL(common::ObKVGlobalCache::get_instance().erase_cache())) {
          LOG_WARN("clear kv cache  failed", K(ret));
        } else {
          LOG_INFO("success erase all kvcache", K(ret));
        }
      } else {
        if (OB_FAIL(common::ObKVGlobalCache::get_instance().erase_cache(stmt.cache_name_.ptr()))) {
          LOG_WARN("clear kv cache  failed", K(ret));
        } else {
          LOG_INFO("success erase kvcache", K(ret), K(stmt.cache_name_));
        }
      }
    }
  }
  return ret;
}


int ObFlushIlogCacheExecutor::execute(ObExecContext &ctx, ObFlushIlogCacheStmt &stmt)
{
  UNUSEDx(ctx, stmt);
  int ret = OB_NOT_SUPPORTED;
  // ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  // ObCommonRpcProxy *common_rpc = NULL;
  // if (OB_ISNULL(task_exec_ctx)) {
  //   ret = OB_NOT_INIT;
  //   LOG_WARN("get task executor context failed");
  // } else if (OB_ISNULL(common_rpc = task_exec_ctx->get_common_rpc())) {
  //   ret = OB_NOT_INIT;
  //   LOG_WARN("get task exec ctx error", K(ret), KP(task_exec_ctx));
  // } else {
  //   int32_t file_id = stmt.file_id_;
  //   if (file_id < 0) {
  //     ret = OB_INVALID_ARGUMENT;
  //     LOG_ERROR("invalid file_id when execute flush ilogcache", K(ret), K(file_id));
  //   } else if (NULL == GCTX.par_ser_) {
  //     ret = OB_ERR_UNEXPECTED;
  //     LOG_ERROR("par_ser is null", K(ret), KP(GCTX.par_ser_));
  //   } else {
  //     // flush all file if file_id is default value 0
  //     if (0 == file_id) {
  //       if (OB_FAIL(GCTX.par_ser_->admin_wash_ilog_cache())) {
  //         LOG_WARN("cursor cache wash ilog error", K(ret));
  //       }
  //     } else {
  //       if (OB_FAIL(GCTX.par_ser_->admin_wash_ilog_cache(file_id))) {
  //         LOG_WARN("cursor cache wash ilog error", K(ret), K(file_id));
  //       }
  //     }
  //   }
  // }
  return ret;
}

int ObFlushDagWarningsExecutor::execute(ObExecContext &ctx, ObFlushDagWarningsStmt &stmt)
{
  UNUSED(stmt);
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else {
    share::g_mp->dag_warning_history_manager()->clear();
  }
  return ret;
}

int ObAdminMergeExecutor::execute(ObExecContext &ctx, ObAdminMergeStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  const ObAdminMergeArg &arg = stmt.get_rpc_arg();
  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_FAIL(GCTX.root_service_->admin_merge(arg))) {
    LOG_WARN("admin merge failed", K(ret), "rpc_arg", arg);
  }
  return ret;
}




int ObRefreshMemStatExecutor::execute(ObExecContext &ctx, ObRefreshMemStatStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_FAIL(GCTX.root_service_->admin_refresh_memory_stat(
                         stmt.get_rpc_arg()))) {
    LOG_WARN("refresh memory stat failed", K(ret), "rpc_arg", stmt.get_rpc_arg());
  }
  return ret;
}

int ObWashMemFragmentationExecutor::execute(ObExecContext &ctx, ObWashMemFragmentationStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_FAIL(GCTX.root_service_->admin_wash_memory_fragmentation(
                         stmt.get_rpc_arg()))) {
    LOG_WARN("wash memory fragmentation failed", K(ret), "rpc_arg", stmt.get_rpc_arg());
  }
  return ret;
}

int ObRefreshIOCalibraitonExecutor::execute(ObExecContext &ctx, ObRefreshIOCalibraitonStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_FAIL(GCTX.root_service_->admin_refresh_io_calibration(stmt.get_rpc_arg()))) {
    LOG_WARN("refresh io calibration failed", K(ret), "rpc_arg", stmt.get_rpc_arg());
  }
  return ret;
}

int ObSetConfigExecutor::execute(ObExecContext &ctx, ObSetConfigStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);

  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_FAIL(GCTX.root_service_->admin_set_config(stmt.get_rpc_arg()))) {
    if (stmt.get_rpc_arg().is_backup_config_) {
      LOG_WARN("set backup config rpc failed", K(ret));
    } else {
      LOG_WARN("set config rpc failed", K(ret), "rpc_arg", stmt.get_rpc_arg());
    }
  }
  return ret;
}

int ObSetTPExecutor::execute(ObExecContext &ctx, ObSetTPStmt &stmt)
{
  int ret = OB_SUCCESS;
  // Directly call ObService::set_tracepoint locally instead of via RPC
  // This avoids RS dependency issues in standby clusters
  if (OB_ISNULL(GCTX.ob_service_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ob_service_ is null", K(ret));
  } else if (OB_FAIL(GCTX.ob_service_->set_tracepoint(stmt.get_rpc_arg()))) {
    LOG_WARN("set tracepoint failed", K(ret), "rpc_arg", stmt.get_rpc_arg());
  } else {
    LOG_INFO("set tracepoint locally", K(stmt.get_rpc_arg()));
  }
  return ret;
}

int ObClearMergeErrorExecutor::execute(ObExecContext &ctx, ObClearMergeErrorStmt &stmt)
{
	int ret = OB_SUCCESS;
	UNUSED(stmt);
	ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  const ObAdminMergeArg &arg = stmt.get_rpc_arg();
	if (OB_ISNULL(task_exec_ctx)) {
		ret = OB_NOT_INIT;
		LOG_WARN("get task executor context failed");
  } else if (OB_FAIL(GCTX.root_service_->admin_clear_merge_error(arg))) {
		LOG_WARN("clear merge error failed", K(ret), "rpc_arg", arg);
	}
  return ret;
}

int ObUpgradeVirtualSchemaExecutor ::execute(
		ObExecContext &ctx, ObUpgradeVirtualSchemaStmt &stmt)
{
  int ret = OB_SUCCESS;
  UNUSED(stmt);
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_FAIL(rootserver::serial_call([&]{ return GCTX.root_service_->admin_upgrade_virtual_schema(); }))) {
    LOG_WARN("upgrade virtual schema failed", K(ret));
  }
  return ret;
}





int ObEnableSqlThrottleExecutor::execute(ObExecContext &ctx, ObEnableSqlThrottleStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObMySQLProxy *sql_proxy = ctx.get_sql_proxy();
  ObSqlString sql;
  if (OB_ISNULL(sql_proxy)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get sql proxy from ctx fail", K(ret));
  } else if (OB_FAIL(sql.assign_fmt(
                         "SET "
                         "GLOBAL sql_throttle_priority=%ld,"
                         "GLOBAL sql_throttle_rt=%.6lf,"
                         "GLOBAL sql_throttle_cpu=%.6lf,"
                         "GLOBAL sql_throttle_io=%ld,"
                         "GLOBAL sql_throttle_network=%.6lf,"
                         "GLOBAL sql_throttle_logical_reads=%ld",
                         stmt.get_priority(),
                         stmt.get_rt(),
                         stmt.get_cpu(),
                         stmt.get_io(),
                         stmt.get_queue_time(),
                         stmt.get_logical_reads()))) {
    LOG_WARN("assign_fmt failed", K(stmt), K(ret));
  } else {
    int64_t affected_rows = 0;
    if (OB_FAIL(sql_proxy->write(sql.ptr(),
                    affected_rows))) {
      LOG_WARN("execute sql fail", K(sql), K(stmt), K(ret));
    }
  }
  return ret;
}

int ObDisableSqlThrottleExecutor::execute(ObExecContext &ctx, ObDisableSqlThrottleStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObMySQLProxy *sql_proxy = ctx.get_sql_proxy();
  ObSqlString sql;
  if (OB_ISNULL(sql_proxy)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get sql proxy from ctx fail", K(ret));
  } else if (OB_FAIL(sql.assign_fmt(
                         "SET "
                         "GLOBAL sql_throttle_priority=%ld,"
                         "GLOBAL sql_throttle_rt=%.6lf,"
                         "GLOBAL sql_throttle_cpu=%.6lf,"
                         "GLOBAL sql_throttle_io=%ld,"
                         "GLOBAL sql_throttle_network=%.6lf,"
                         "GLOBAL sql_throttle_logical_reads=%ld",
                         -1L,
                         -1.0,
                         -1.0,
                         -1L,
                         -1.0,
                         -1L))) {
    LOG_WARN("assign_fmt failed", K(stmt), K(ret));
  } else {
    int64_t affected_rows = 0;
    if (OB_FAIL(sql_proxy->write(sql.ptr(),
                    affected_rows))) {
      LOG_WARN("execute sql fail", K(sql), K(stmt), K(ret));
    }
  }
  return ret;
}

int ObCancelTaskExecutor::execute(ObExecContext &ctx, ObCancelTaskStmt &stmt)
{
  int ret = OB_SUCCESS;
  share::ObTaskId task_id;

  LOG_INFO("cancel sys task log",
           K(stmt.get_task_id()), K(stmt.get_task_type()), K(stmt.get_cmd_type()));

  if (NULL == GCTX.ob_service_) {
    ret = OB_ERR_SYS;
    LOG_ERROR("GCTX must not inited", K(ret), KP(GCTX.ob_service_));
  } else if (OB_FAIL(parse_task_id(stmt.get_task_id(), task_id))) {
    LOG_WARN("failed to parse task id", K(ret), K(stmt.get_task_id()));
  } else if (OB_FAIL(ex_rpc::sync_call([&]{
    return GCTX.ob_service_->cancel_sys_task(task_id);
  }))) {
    LOG_WARN("failed to cancel sys task", K(ret), K(task_id));
  }
  return ret;
}

int ObCancelTaskExecutor::fetch_sys_task_info(
		ObExecContext &ctx,
		const common::ObString &task_id,
		common::ObAddr &task_server)
{
	int ret = OB_SUCCESS;
	SMART_VAR(ObMySQLProxy::MySQLResult, res) {
	  ObMySQLProxy *sql_proxy = ctx.get_sql_proxy();
	  sqlclient::ObMySQLResult *result_set = NULL;
	  ObSQLSessionInfo *cur_sess = ctx.get_my_session();
	  ObSqlString read_sql;
	  int64_t tmp_real_str_len = 0;
	  const char *sql_str = "select task_type "
	  							" from oceanbase.__all_virtual_sys_task_status "
	  							" where task_id = '%.*s'";
	  char task_type_str[common::OB_SYS_TASK_TYPE_LENGTH] = "";

	  task_server.reset();
          task_server = GCTX.self_addr();

	    //execute sql
	  if (OB_ISNULL(sql_proxy) || OB_ISNULL(cur_sess)) {
	  	ret = OB_ERR_UNEXPECTED;
	  	LOG_WARN("sql proxy or session from exec context is NULL", K(ret), K(sql_proxy), K(cur_sess));
	  } else if (OB_FAIL(read_sql.append_fmt(sql_str, task_id.length(), task_id.ptr()))) {
	  	LOG_WARN("fail to generate sql", K(ret), K(read_sql), K(*cur_sess), K(task_id));
	  } else if (OB_FAIL(sql_proxy->read(res, read_sql.ptr()))) {
	  	LOG_WARN("fail to read by sql proxy", K(ret), K(read_sql));
	  } else if (OB_ISNULL(result_set = res.get_result())) {
	  	ret = OB_ERR_UNEXPECTED;
	  	LOG_WARN("result set is NULL", K(ret), K(read_sql));
	  } else if (OB_FAIL(result_set->next())) {
	  	if (OB_LIKELY(OB_ITER_END == ret)) {
	  	  ret = OB_ENTRY_NOT_EXIST;
	  	  LOG_WARN("task id not exist", K(ret), K(result_set), K(task_id));
      } else {
	  	  LOG_WARN("fail to get next row", K(ret), K(result_set));
	  	}
	  } else {
	  	EXTRACT_STRBUF_FIELD_MYSQL(*result_set, "task_type", task_type_str, OB_SYS_TASK_TYPE_LENGTH, tmp_real_str_len);
	  	UNUSED(tmp_real_str_len);
	  }

	    //set addr
	  if (OB_SUCC(ret)) {
	  	if (OB_UNLIKELY(OB_ITER_END != result_set->next())) {
	  	  ret = OB_ERR_UNEXPECTED;
	  	  LOG_WARN("more than one sessid record", K(ret), K(read_sql));
	  	}
	  }
  }

	return ret;
}

int ObCancelTaskExecutor::parse_task_id(
    const common::ObString &task_id_str, share::ObTaskId &task_id)
{
  int ret = OB_SUCCESS;
  char task_id_buf[common::OB_TRACE_STAT_BUFFER_SIZE] = "";
  task_id.reset();

	int n = snprintf(task_id_buf, sizeof(task_id_buf), "%.*s",
		  task_id_str.length(), task_id_str.ptr());
	if (n < 0 || n >= sizeof(task_id_buf)) {
		ret = common::OB_BUF_NOT_ENOUGH;
		LOG_WARN("task id buf not enough", K(ret), K(n), K(task_id_str));
	} else if (OB_FAIL(task_id.parse_from_buf(task_id_buf))) {
		ret = OB_INVALID_ARGUMENT;
		LOG_WARN("invalid task id", K(ret), K(n), K(task_id_buf));
	} else {

	  // double check
    ObCStringHelper helper;
	  n = snprintf(task_id_buf, sizeof(task_id_buf), "%s", helper.convert(task_id));
		if (n < 0 || n >= sizeof(task_id_buf)) {
		  ret = OB_BUF_NOT_ENOUGH;
		  LOG_WARN("invalid task id", K(ret), K(n), K(task_id), K(task_id_buf));
		} else if (0 != task_id_str.case_compare(task_id_buf)) {
		  ret = OB_INVALID_ARGUMENT;
		  LOG_WARN("task id is not valid",
			  K(ret), K(task_id_str), K(task_id_buf), K(task_id_str.length()), K(strlen(task_id_buf)));
		}
	}
	return ret;
}

int ObAddDiskExecutor::execute(ObExecContext &ctx, ObAddDiskStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_FAIL(ex_rpc::sync_call([]{ return OB_NOT_SUPPORTED; }))) {
    LOG_WARN("add_disk failed", K(ret), "arg", stmt.arg_);
  }
  return ret;
}

int ObDropDiskExecutor::execute(ObExecContext &ctx, ObDropDiskStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_FAIL(ex_rpc::sync_call([]{ return OB_NOT_SUPPORTED; }))) {
    LOG_WARN("drop_disk failed", K(ret), "arg", stmt.arg_);
  }
  return ret;
}



int ObResetConfigExecutor::execute(ObExecContext &ctx, ObResetConfigStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);

  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_FAIL(GCTX.root_service_->admin_set_config(stmt.get_rpc_arg()))) {
    LOG_WARN("set config rpc failed", K(ret), "rpc_arg", stmt.get_rpc_arg());
  }
  return ret;
}


} // end namespace sql
} // end namespace oceanbase
