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
#include "observer/ob_server.h"
#include "rootserver/standby/ob_standby_service.h" // ObStandbyService
#include "sql/resolver/cmd/ob_switch_role_stmt.h" // ObSwitchRoleStmt
#include "share/scheduler/ob_dag_warning_history_mgr.h"
#include "observer/omt/ob_tenant.h" //ObTenant
#include "rootserver/freeze/ob_major_freeze_helper.h" //ObMajorFreezeHelper
#include "pl/pl_cache/ob_pl_cache_mgr.h"
#include "sql/plan_cache/ob_ps_cache.h"

#include "rootserver/ob_tenant_event_def.h"
#include "rootserver/backup/ob_backup_param_operator.h" // ObBackupParamOperator
#include "share/table/ob_redis_importer.h"
#include "share/ob_timezone_importer.h"
#include "share/ob_srs_importer.h"

namespace oceanbase
{
using namespace common;
using namespace obrpc;
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
  obrpc::ObCommonRpcProxy *common_rpc_proxy = NULL;

  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_FAIL(task_exec_ctx->get_common_rpc(common_rpc_proxy))) {
    LOG_WARN("get common rpc proxy failed", K(ret));
  } else if (OB_ISNULL(common_rpc_proxy)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("common_rpc_proxy is null", K(ret));
  } else {
    if (!stmt.is_major_freeze()) {
      const uint64_t local_tenant_id = MTL_ID();
      bool freeze_all = (stmt.is_freeze_all() ||
                         stmt.is_freeze_all_user() ||
                         stmt.is_freeze_all_meta());
      ObRootMinorFreezeArg arg;
      if (OB_FAIL(arg.tenant_ids_.assign(stmt.get_tenant_ids()))) {
        LOG_WARN("failed to assign tenant_ids", K(ret));
      } else if (OB_FAIL(arg.server_list_.assign(stmt.get_server_list()))) {
        LOG_WARN("failed to assign server_list", K(ret));
      } else {
        arg.zone_ = stmt.get_zone();
        arg.tablet_id_ = stmt.get_tablet_id();
        arg.ls_id_ = stmt.get_ls_id();
      }
      if (OB_SUCC(ret)) {
        // get all tenants to freeze
        if (freeze_all) {
          if (OB_ISNULL(GCTX.schema_service_)) {
            ret = OB_INVALID_ARGUMENT;
            LOG_WARN("invalid GCTX", KR(ret));
          } else {
            common::ObSArray<uint64_t> tmp_tenant_ids;
            if (OB_FAIL(GCTX.schema_service_->get_tenant_ids(tmp_tenant_ids))) {
              LOG_WARN("fail to get all tenant ids", KR(ret));
            } else {
              using FUNC_TYPE = bool (*) (const uint64_t);
              FUNC_TYPE func = nullptr;
              // caller guarantees that at most one of
              // freeze_all/freeze_all_user/freeze_all_meta is true.
              if (stmt.is_freeze_all() || stmt.is_freeze_all_user()) {
                func = is_user_tenant;
              } else {
                func = is_meta_tenant;
              }
              arg.tenant_ids_.reset();
              for (int64_t i = 0; OB_SUCC(ret) && (i < tmp_tenant_ids.count()); ++i) {
                uint64_t tmp_tenant_id = tmp_tenant_ids.at(i);
                if (func(tmp_tenant_id)) {
                  if (OB_FAIL(arg.tenant_ids_.push_back(tmp_tenant_id))) {
                    LOG_WARN("failed to push back tenant_id", KR(ret));
                  }
                }
              }
            }
          }
        // get local tenant to freeze if there is no any parameter except server_list
        } else if (arg.tenant_ids_.empty() &&
                   arg.zone_.is_empty() &&
                   !arg.tablet_id_.is_valid() &&
                   !freeze_all) {
          if (!is_sys_tenant(local_tenant_id) || arg.server_list_.empty()) {
            if (OB_FAIL(arg.tenant_ids_.push_back(local_tenant_id))) {
              LOG_WARN("failed to push back tenant_id", KR(ret));
            }
          }
        // get local tenant to freeze if there is no any parameter
        } else if (0 == arg.tenant_ids_.count() &&
                   0 == arg.server_list_.count() &&
                   arg.zone_.is_empty() &&
                   !arg.tablet_id_.is_valid() &&
                   !freeze_all) {
          if (OB_FAIL(arg.tenant_ids_.push_back(local_tenant_id))) {
            LOG_WARN("failed to push back tenant_id", KR(ret));
           }
        }
      }
      // access check:
      // not allow user_tenant to freeze other tenants
      if (OB_SUCC(ret) && !is_sys_tenant(local_tenant_id)) {
        if (arg.tenant_ids_.count() > 1 ||
            (!arg.tenant_ids_.empty() && local_tenant_id != arg.tenant_ids_[0])) {
          ret = OB_ERR_NO_PRIVILEGE;
          LOG_WARN("user_tenant cannot freeze other tenants", K(ret), K(local_tenant_id), K(arg));
        }
      }
      if (OB_SUCC(ret)) {
        int64_t timeout = THIS_WORKER.get_timeout_remain();
        if (OB_FAIL(common_rpc_proxy->timeout(timeout).root_minor_freeze(arg))) {
          LOG_WARN("minor freeze rpc failed", K(arg), K(ret), K(timeout), "dst", common_rpc_proxy->get_server());
        }
      }
    } else if (stmt.get_tablet_id().is_valid()) {
      if (OB_UNLIKELY(1 != stmt.get_tenant_ids().count())) {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("not support schedule tablet major freeze for several tenant", K(ret), K(stmt));
      } else {
        rootserver::ObTabletMajorFreezeParam param;
        param.tenant_id_ = stmt.get_tenant_ids().at(0);
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
      param.transport_ = GCTX.net_frame_->get_req_transport();
      param.freeze_reason_ = rootserver::MF_USER_REQUEST;
      for (int64_t i = 0; i < stmt.get_tenant_ids().count() && OB_SUCC(ret); ++i) {
        uint64_t tenant_id = stmt.get_tenant_ids().at(i);
        if (OB_FAIL(param.add_freeze_info(tenant_id))) {
          LOG_WARN("fail to assign", KR(ret), K(tenant_id));
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
    int64_t tenant_num = stmt.flush_cache_arg_.tenant_ids_.count();
    int64_t db_num = stmt.flush_cache_arg_.db_ids_.count();
    common::ObString sql_id = stmt.flush_cache_arg_.sql_id_;
    switch (stmt.flush_cache_arg_.cache_type_) {
      case CACHE_TYPE_LIB_CACHE: {
        if (stmt.flush_cache_arg_.ns_type_ != ObLibCacheNameSpace::NS_INVALID) {
          ObLibCacheNameSpace ns = stmt.flush_cache_arg_.ns_type_;
          if (0 == tenant_num) { // purge in tenant level, aka. coarse-grained plan evict
            common::ObArray<uint64_t> tenant_ids;
            if (OB_ISNULL(GCTX.omt_)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("unexpected null of GCTX.omt_", K(ret));
            } else if (OB_FAIL(GCTX.omt_->get_mtl_tenant_ids(tenant_ids))) {
              LOG_WARN("fail to get_mtl_tenant_ids", K(ret));
            } else {
              for (int64_t i = 0; i < tenant_ids.size(); i++) {
                MTL_SWITCH(tenant_ids.at(i)) {
                  ObPlanCache* plan_cache = MTL(ObPlanCache*);
                  ret = plan_cache->flush_lib_cache_by_ns(ns);
                }
                // ignore errors at switching tenant
                ret = OB_SUCCESS;
              }
            }
          } else {
            for (int64_t i = 0; i < tenant_num; ++i) { //ignore ret
              MTL_SWITCH(stmt.flush_cache_arg_.tenant_ids_.at(i)) {
                ObPlanCache* plan_cache = MTL(ObPlanCache*);
                ret = plan_cache->flush_lib_cache_by_ns(ns);
              }
            }
          }
        } else {
          if (0 == tenant_num) { // purge in tenant level, aka. coarse-grained plan evict
            common::ObArray<uint64_t> tenant_ids;
            if (OB_ISNULL(GCTX.omt_)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("unexpected null of GCTX.omt_", K(ret));
            } else if (OB_FAIL(GCTX.omt_->get_mtl_tenant_ids(tenant_ids))) {
              LOG_WARN("fail to get_mtl_tenant_ids", K(ret));
            } else {
              for (int64_t i = 0; i < tenant_ids.size(); i++) {
                MTL_SWITCH(tenant_ids.at(i)) {
                  ObPlanCache* plan_cache = MTL(ObPlanCache*);
                  ret = plan_cache->flush_lib_cache();
                }
                // ignore errors at switching tenant
                ret = OB_SUCCESS;
              }
            }
          } else {
            for (int64_t i = 0; i < tenant_num; ++i) { //ignore ret
              MTL_SWITCH(stmt.flush_cache_arg_.tenant_ids_.at(i)) {
                ObPlanCache* plan_cache = MTL(ObPlanCache*);
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
              int64_t t_id = stmt.flush_cache_arg_.tenant_ids_.at(i);
              MTL_SWITCH(t_id) {
                ObPlanCache* plan_cache = MTL(ObPlanCache*);
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
          common::ObArray<uint64_t> tenant_ids;
          if (OB_ISNULL(GCTX.omt_)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected null of GCTX.omt_", K(ret));
          } else if (OB_FAIL(GCTX.omt_->get_mtl_tenant_ids(tenant_ids))) {
            LOG_WARN("fail to get_mtl_tenant_ids", K(ret));
          } else {
            for (int64_t i = 0; i < tenant_ids.size(); i++) {
              MTL_SWITCH(tenant_ids.at(i)) {
                ObPlanCache* plan_cache = MTL(ObPlanCache*);
                ret = plan_cache->flush_plan_cache();
              }
              // ignore errors at switching tenant
              ret = OB_SUCCESS;
            }
          }
        } else {
          for (int64_t i = 0; OB_SUCC(ret) && i < tenant_num; ++i) { //ignore ret
            MTL_SWITCH(stmt.flush_cache_arg_.tenant_ids_.at(i)) {
              ObPlanCache* plan_cache = MTL(ObPlanCache*);
              ret = plan_cache->flush_plan_cache();
            }
          }
        }
        break;
      }
      case CACHE_TYPE_SQL_AUDIT: {
        if (0 == tenant_num) {
          common::ObArray<uint64_t> tenant_ids;
          if (OB_ISNULL(GCTX.omt_)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected null of GCTX.omt_", K(ret));
          } else if (OB_FAIL(GCTX.omt_->get_mtl_tenant_ids(tenant_ids))) {
            LOG_WARN("fail to get_mtl_tenant_ids", K(ret));
          }
          if (OB_SUCC(ret)) {
            for (int64_t i = 0; i < tenant_ids.size(); i++) { // ignore internal error
              const uint64_t tenant_id = tenant_ids.at(i);
              MTL_SWITCH(tenant_id) {
                ObMySQLRequestManager *req_mgr = MTL(ObMySQLRequestManager*);
                req_mgr->clear_queue();
              } else if (OB_TENANT_NOT_IN_SERVER == ret) {
                ret = OB_SUCCESS;
              } else {
                LOG_WARN("fail to switch to tenant", K(ret), K(tenant_id));
              }
            }
          }
        } else {
          for (int64_t i = 0; i < tenant_num; i++) { // ignore ret
            const uint64_t tenant_id = stmt.flush_cache_arg_.tenant_ids_.at(i);
            MTL_SWITCH(tenant_id) {
              ObMySQLRequestManager *req_mgr = MTL(ObMySQLRequestManager*);
              req_mgr->clear_queue();
            } else if (OB_TENANT_NOT_IN_SERVER == ret) {
              ret = OB_SUCCESS;
            } else {
              LOG_WARN("fail to switch to tenant", K(ret), K(tenant_id));
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
              int64_t t_id = stmt.flush_cache_arg_.tenant_ids_.at(i);
              MTL_SWITCH(t_id) {
                ObPlanCache* plan_cache = MTL(ObPlanCache*);
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
          common::ObArray<uint64_t> tenant_ids;
          if (OB_ISNULL(GCTX.omt_)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected null of GCTX.omt_", K(ret));
          } else if (OB_FAIL(GCTX.omt_->get_mtl_tenant_ids(tenant_ids))) {
            LOG_WARN("fail to get_mtl_tenant_ids", K(ret));
          } else {
            for (int64_t i = 0; i < tenant_ids.size(); i++) {
              MTL_SWITCH(tenant_ids.at(i)) {
                ObPlanCache* plan_cache = MTL(ObPlanCache*);
                ret = plan_cache->flush_pl_cache();
              }
              // ignore errors at switching tenant
              ret = OB_SUCCESS;
            }
          }
        } else {
          for (int64_t i = 0; i < tenant_num; i++) { // ignore internal err code
            MTL_SWITCH(stmt.flush_cache_arg_.tenant_ids_.at(i)) {
              ObPlanCache* plan_cache = MTL(ObPlanCache*);
              ret = plan_cache->flush_pl_cache();
            }
          }
        }
        break;
      }
      case CACHE_TYPE_PS_OBJ: {
        if (0 == tenant_num) {
          common::ObArray<uint64_t> tenant_ids;
          if (OB_ISNULL(GCTX.omt_)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected null of GCTX.omt_", K(ret));
          } else if (OB_FAIL(GCTX.omt_->get_mtl_tenant_ids(tenant_ids))) {
            LOG_WARN("fail to get_mtl_tenant_ids", K(ret));
          } else {
            for (int64_t i = 0; i < tenant_ids.size(); i++) {
              MTL_SWITCH(tenant_ids.at(i)) {
                ObPsCache* ps_cache = MTL(ObPsCache*);
                if (ps_cache->is_inited()) {
                  ret = ps_cache->cache_evict_all_ps();
                }
              }
              // ignore errors at switching tenant
              ret = OB_SUCCESS;
            }
          }
        } else {
          for (int64_t i = 0; i < tenant_num; i++) { // ignore internal err code
            MTL_SWITCH(stmt.flush_cache_arg_.tenant_ids_.at(i)) {
              ObPsCache* ps_cache = MTL(ObPsCache*);
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
      //  obrpc::ObCommonRpcProxy *common_rpc_proxy = NULL;

      //  if (OB_ISNULL(task_exec_ctx)) {
      //    ret = OB_NOT_INIT;
      //    LOG_WARN("get task executor context failed");
      //  } else if (OB_FAIL(task_exec_ctx->get_common_rpc(common_rpc_proxy))) {
      //    LOG_WARN("get common rpc proxy failed", K(ret));
      //  } else if (OB_ISNULL(common_rpc_proxy)) {
      //    ret = OB_ERR_UNEXPECTED;
      //    LOG_WARN("common_rpc_proxy is null", K(ret));
      //  } else if (OB_FAIL(common_rpc_proxy->flush_balance_info())) {
      //    LOG_WARN("fail to flush balance info", K(ret));
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
    obrpc::ObCommonRpcProxy *common_rpc = NULL;
    if (OB_ISNULL(task_exec_ctx)) {
      ret = OB_NOT_INIT;
      LOG_WARN("get task executor context failed");
    } else if (OB_ISNULL(common_rpc = task_exec_ctx->get_common_rpc())) {
      ret = OB_NOT_INIT;
      LOG_WARN("get common rpc proxy failed", K(task_exec_ctx));
    } else if (OB_FAIL(common_rpc->admin_flush_cache(
                           stmt.flush_cache_arg_))) {
      LOG_WARN("flush cache rpc failed", K(ret), "rpc_arg", stmt.flush_cache_arg_);
    }
  }
  return ret;
}

int ObFlushKVCacheExecutor::execute(ObExecContext &ctx, ObFlushKVCacheStmt &stmt)
{
  UNUSED(stmt);
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  obrpc::ObCommonRpcProxy *common_rpc = NULL;
  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_ISNULL(common_rpc = task_exec_ctx->get_common_rpc())) {
    ret = OB_NOT_INIT;
    LOG_WARN("get common rpc proxy failed", K(task_exec_ctx));
  } else {
    share::schema::ObSchemaGetterGuard schema_guard;
    if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(
                ctx.get_my_session()->get_effective_tenant_id(),
                schema_guard))) {
      LOG_WARN("get_schema_guard failed", K(ret));
    } else {
      if (stmt.tenant_name_.is_empty() && stmt.cache_name_.is_empty()) {
        if (OB_FAIL(common::ObKVGlobalCache::get_instance().erase_cache())) {
          LOG_WARN("clear kv cache  failed", K(ret));
        } else {
          LOG_INFO("success erase all kvcache", K(ret));
        }
      } else if (!stmt.tenant_name_.is_empty() && stmt.cache_name_.is_empty()) {
        uint64_t tenant_id = OB_INVALID_ID;
        if (OB_FAIL(schema_guard.get_tenant_id(ObString::make_string(stmt.tenant_name_.ptr()), tenant_id)) ||
              OB_INVALID_ID == tenant_id) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("tenant not found", K(ret));
        } else if (OB_FAIL(common::ObKVGlobalCache::get_instance().erase_cache(tenant_id))) {
          LOG_WARN("clear kv cache  failed", K(ret));
        } else {
          LOG_INFO("success erase tenant kvcache", K(ret), K(tenant_id));
        }
      } else if (!stmt.tenant_name_.is_empty() && !stmt.cache_name_.is_empty()) {
        uint64_t tenant_id = OB_INVALID_ID;
        if (OB_FAIL(schema_guard.get_tenant_id(ObString::make_string(stmt.tenant_name_.ptr()), tenant_id)) ||
              OB_INVALID_ID == tenant_id) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("tenant not found", K(ret));
        } else if (OB_FAIL(common::ObKVGlobalCache::get_instance().erase_cache(tenant_id, stmt.cache_name_.ptr()))) {
          LOG_WARN("clear kv cache  failed", K(ret));
        } else {
          LOG_INFO("success erase tenant kvcache", K(ret), K(tenant_id), K(stmt.cache_name_));
        }
      } else if (stmt.tenant_name_.is_empty() && !stmt.cache_name_.is_empty()) {
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

int ObFlushSSMicroCacheExecutor::execute(ObExecContext &ctx, ObFlushSSMicroCacheStmt &stmt)
{
  UNUSED(stmt);
  int ret = OB_SUCCESS;
  return ret;
}

int ObFlushIlogCacheExecutor::execute(ObExecContext &ctx, ObFlushIlogCacheStmt &stmt)
{
  UNUSEDx(ctx, stmt);
  int ret = OB_NOT_SUPPORTED;
  // ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  // obrpc::ObCommonRpcProxy *common_rpc = NULL;
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
  obrpc::ObCommonRpcProxy *common_rpc = NULL;
  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_ISNULL(common_rpc = task_exec_ctx->get_common_rpc())) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task exec ctx error", K(ret), KP(task_exec_ctx));
  } else {
    MTL(ObDagWarningHistoryManager *)->clear();
  }
  return ret;
}

int ObAdminMergeExecutor::execute(ObExecContext &ctx, ObAdminMergeStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  const obrpc::ObAdminMergeArg &arg = stmt.get_rpc_arg();
  obrpc::ObCommonRpcProxy *common_rpc = NULL;
  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_ISNULL(common_rpc = task_exec_ctx->get_common_rpc())) {
    ret = OB_NOT_INIT;
    LOG_WARN("get common rpc proxy failed", K(task_exec_ctx));
  } else if (OB_FAIL(common_rpc->admin_merge(arg))) {
    LOG_WARN("admin merge rpc failed", K(ret), "rpc_arg", arg);
  }
  return ret;
}

int ObAdminRecoveryExecutor::execute(ObExecContext &ctx, ObAdminRecoveryStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  obrpc::ObCommonRpcProxy *common_rpc = NULL;
  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_ISNULL(common_rpc = task_exec_ctx->get_common_rpc())) {
    ret = OB_NOT_INIT;
    LOG_WARN("get common rpc proxy failed", K(task_exec_ctx));
  } else if (OB_FAIL(common_rpc->admin_recovery(
                         stmt.get_rpc_arg()))) {
    LOG_WARN("admin merge rpc failed", K(ret), "rpc_arg", stmt.get_rpc_arg());
  }
  return ret;
}

int ObClearRoottableExecutor::execute(ObExecContext &ctx, ObClearRoottableStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  obrpc::ObCommonRpcProxy *common_rpc = NULL;
  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_ISNULL(common_rpc = task_exec_ctx->get_common_rpc())) {
    ret = OB_NOT_INIT;
    LOG_WARN("get common rpc proxy failed", K(task_exec_ctx));
  } else if (OB_FAIL(common_rpc->admin_clear_roottable(
                         stmt.get_rpc_arg()))) {
    LOG_WARN("clear roottable rpc failed", K(ret), "rpc_arg", stmt.get_rpc_arg());
  }
  return ret;
}

int ObRefreshSchemaExecutor::execute(ObExecContext &ctx, ObRefreshSchemaStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  obrpc::ObCommonRpcProxy *common_rpc = NULL;
  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_ISNULL(common_rpc = task_exec_ctx->get_common_rpc())) {
    ret = OB_NOT_INIT;
    LOG_WARN("get common rpc proxy failed", K(task_exec_ctx));
  } else if (OB_FAIL(common_rpc->admin_refresh_schema(
                         stmt.get_rpc_arg()))) {
    LOG_WARN("refresh schema rpc failed", K(ret), "rpc_arg", stmt.get_rpc_arg());
  }
  return ret;
}

int ObRefreshMemStatExecutor::execute(ObExecContext &ctx, ObRefreshMemStatStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  obrpc::ObCommonRpcProxy *common_rpc = NULL;
  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_ISNULL(common_rpc = task_exec_ctx->get_common_rpc())) {
    ret = OB_NOT_INIT;
    LOG_WARN("get common rpc proxy failed", K(task_exec_ctx));
  } else if (OB_FAIL(common_rpc->admin_refresh_memory_stat(
                         stmt.get_rpc_arg()))) {
    LOG_WARN("refresh memory stat rpc failed", K(ret), "rpc_arg", stmt.get_rpc_arg());
  }
  return ret;
}

int ObWashMemFragmentationExecutor::execute(ObExecContext &ctx, ObWashMemFragmentationStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  obrpc::ObCommonRpcProxy *common_rpc = NULL;
  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_ISNULL(common_rpc = task_exec_ctx->get_common_rpc())) {
    ret = OB_NOT_INIT;
    LOG_WARN("get common rpc proxy failed", K(task_exec_ctx));
  } else if (OB_FAIL(common_rpc->admin_wash_memory_fragmentation(
                         stmt.get_rpc_arg()))) {
    LOG_WARN("sync wash fragment rpc failed", K(ret), "rpc_arg", stmt.get_rpc_arg());
  }
  return ret;
}

int ObRefreshIOCalibraitonExecutor::execute(ObExecContext &ctx, ObRefreshIOCalibraitonStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  obrpc::ObCommonRpcProxy *common_rpc = NULL;
  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_ISNULL(common_rpc = task_exec_ctx->get_common_rpc())) {
    ret = OB_NOT_INIT;
    LOG_WARN("get common rpc proxy failed", K(task_exec_ctx));
  } else if (OB_FAIL(common_rpc->admin_refresh_io_calibration(stmt.get_rpc_arg()))) {
    LOG_WARN("refresh io calibration rpc failed", K(ret), "rpc_arg", stmt.get_rpc_arg());
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

int ObChangeExternalStorageDestExecutor::execute(ObExecContext &ctx, ObChangeExternalStorageDestStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  obrpc::ObSrvRpcProxy *svr_rpc = NULL;
  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_ISNULL(svr_rpc = task_exec_ctx->get_srv_rpc())) {
    ret = OB_NOT_INIT;
    LOG_WARN("get svr rpc proxy failed", K(task_exec_ctx));
  } else if (OB_FAIL(svr_rpc->change_external_storage_dest(stmt.get_rpc_arg()))) {
    LOG_WARN("set config rpc failed", K(ret), "rpc_arg", stmt.get_rpc_arg());
  } else {
    LOG_INFO("change external storage dest rpc", K(stmt.get_rpc_arg()));
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
  const obrpc::ObAdminMergeArg &arg = stmt.get_rpc_arg();
	obrpc::ObCommonRpcProxy *common_rpc = NULL;
	if (OB_ISNULL(task_exec_ctx)) {
		ret = OB_NOT_INIT;
		LOG_WARN("get task executor context failed");
	} else if (OB_ISNULL(common_rpc = task_exec_ctx->get_common_rpc())) {
		ret = OB_NOT_INIT;
		LOG_WARN("get common rpc proxy failed", K(task_exec_ctx));
  } else if (OB_FAIL(common_rpc->admin_clear_merge_error(arg))) {
		LOG_WARN("clear merge error rpc failed", K(ret), "rpc_arg", arg);
	}
  return ret;
}

int ObUpgradeVirtualSchemaExecutor ::execute(
		ObExecContext &ctx, ObUpgradeVirtualSchemaStmt &stmt)
{
  int ret = OB_SUCCESS;
  UNUSED(stmt);
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  obrpc::ObCommonRpcProxy *common_rpc = NULL;
  int64_t timeout = THIS_WORKER.get_timeout_remain();
  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_ISNULL(common_rpc = task_exec_ctx->get_common_rpc())) {
    ret = OB_NOT_INIT;
    LOG_WARN("get common rpc proxy failed", K(task_exec_ctx));
  } else if (OB_FAIL(common_rpc->timeout(timeout).admin_upgrade_virtual_schema())) {
    LOG_WARN("upgrade virtual schema rpc failed", K(ret));
  }
  return ret;
}

int ObAdminUpgradeCmdExecutor::execute(ObExecContext &ctx, ObAdminUpgradeCmdStmt &stmt)
{
  int ret = OB_SUCCESS;
  obrpc::Bool upgrade = true;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  obrpc::ObCommonRpcProxy *common_rpc = NULL;
  int64_t timeout = THIS_WORKER.get_timeout_remain();
  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_ISNULL(common_rpc = task_exec_ctx->get_common_rpc())) {
    ret = OB_NOT_INIT;
    LOG_WARN("get common rpc proxy failed", K(task_exec_ctx));
  } else {
    if (ObAdminUpgradeCmdStmt::BEGIN == stmt.get_op()) {
      upgrade = true;
      if (OB_FAIL(common_rpc->timeout(timeout).admin_upgrade_cmd(upgrade))) {
        LOG_WARN("begin upgrade rpc failed", K(ret));
      }
    } else if (ObAdminUpgradeCmdStmt::END == stmt.get_op()) {
      upgrade = false;
      if (OB_FAIL(common_rpc->timeout(timeout).admin_upgrade_cmd(upgrade))) {
        LOG_WARN("end upgrade rpc failed", K(ret));
      }
    }
  }
  return ret;
}

int ObAdminRollingUpgradeCmdExecutor::execute(ObExecContext &ctx, ObAdminRollingUpgradeCmdStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  obrpc::ObCommonRpcProxy *common_rpc = NULL;
  int64_t timeout = THIS_WORKER.get_timeout_remain();
  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_ISNULL(common_rpc = task_exec_ctx->get_common_rpc())) {
    ret = OB_NOT_INIT;
    LOG_WARN("get common rpc proxy failed", K(task_exec_ctx));
  } else {
    ObAdminRollingUpgradeArg arg;
    if (ObAdminRollingUpgradeCmdStmt::BEGIN == stmt.get_op()) {
      arg.stage_ = obrpc::OB_UPGRADE_STAGE_DBUPGRADE;
      if (OB_FAIL(common_rpc->timeout(timeout).admin_rolling_upgrade_cmd(arg))) {
        LOG_WARN("begin upgrade rpc failed", K(ret));
      }
    } else if (ObAdminRollingUpgradeCmdStmt::END == stmt.get_op()) {
      arg.stage_ = obrpc::OB_UPGRADE_STAGE_POSTUPGRADE;
      if (OB_FAIL(common_rpc->timeout(timeout).admin_rolling_upgrade_cmd(arg))) {
        LOG_WARN("end upgrade rpc failed", K(ret));
      }
    }
  }
  return ret;
}

int ObRunUpgradeJobExecutor::execute(
		ObExecContext &ctx, ObRunUpgradeJobStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  int64_t timeout = THIS_WORKER.get_timeout_remain();
  obrpc::ObCommonRpcProxy *common_rpc = NULL;
  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_ISNULL(common_rpc = task_exec_ctx->get_common_rpc())) {
    ret = OB_NOT_INIT;
    LOG_WARN("get common rpc proxy failed", K(task_exec_ctx));
  } else if (OB_FAIL(common_rpc->timeout(timeout).run_upgrade_job(
                         stmt.get_rpc_arg()))) {
    LOG_WARN("run job rpc failed", K(ret), "rpc_arg", stmt.get_rpc_arg());
  }
  return ret;
}

int ObStopUpgradeJobExecutor::execute(
		ObExecContext &ctx, ObStopUpgradeJobStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  int64_t timeout = THIS_WORKER.get_timeout_remain();
  obrpc::ObCommonRpcProxy *common_rpc = NULL;
  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_ISNULL(common_rpc = task_exec_ctx->get_common_rpc())) {
    ret = OB_NOT_INIT;
    LOG_WARN("get common rpc proxy failed", K(task_exec_ctx));
  } else if (OB_FAIL(common_rpc->timeout(timeout).run_upgrade_job(
                         stmt.get_rpc_arg()))) {
    LOG_WARN("run job rpc failed", K(ret), "rpc_arg", stmt.get_rpc_arg());
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
    if (OB_FAIL(sql_proxy->write(
                    GET_MY_SESSION(ctx)->get_priv_tenant_id(),
                    sql.ptr(),
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
    if (OB_FAIL(sql_proxy->write(
                    GET_MY_SESSION(ctx)->get_priv_tenant_id(),
                    sql.ptr(),
                    affected_rows))) {
      LOG_WARN("execute sql fail", K(sql), K(stmt), K(ret));
    }
  }
  return ret;
}

int ObCancelTaskExecutor::execute(ObExecContext &ctx, ObCancelTaskStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObAddr task_server;
  share::ObTaskId task_id;
  bool is_local_task = false;

	LOG_INFO("cancel sys task log",
		       K(stmt.get_task_id()), K(stmt.get_task_type()), K(stmt.get_cmd_type()));

	if (NULL == GCTX.ob_service_ || NULL == GCTX.srv_rpc_proxy_) {
		ret = OB_ERR_SYS;
		LOG_ERROR("GCTX must not inited", K(ret), KP(GCTX.srv_rpc_proxy_), KP(GCTX.ob_service_));
	} else if (OB_FAIL(parse_task_id(stmt.get_task_id(), task_id))) {
		LOG_WARN("failed to parse task id", K(ret), K(stmt.get_task_id()));
	} else if (OB_FAIL(SYS_TASK_STATUS_MGR.task_exist(task_id, is_local_task))) {
		LOG_WARN("failed to check is local task", K(ret), K(task_id));
	} else if (is_local_task) {
		if (OB_FAIL(GCTX.ob_service_->cancel_sys_task(task_id))) {
		  LOG_WARN("failed to cancel sys task at local", K(ret), K(task_id));
      }
	} else {
		if (OB_FAIL(fetch_sys_task_info(ctx, stmt.get_task_id(), task_server))) {
		  LOG_WARN("failed to fetch sys task info", K(ret));
    } else if (!task_server.is_valid() || task_id.is_invalid()) {
		  ret = OB_INVALID_ARGUMENT;
		  LOG_WARN("invalid task info", K(ret), K(task_server), K(task_id));
		} else {
		  obrpc::ObCancelTaskArg rpc_arg;
		  rpc_arg.task_id_ = task_id;
		  if (OB_FAIL(GCTX.srv_rpc_proxy_->to(task_server).cancel_sys_task(rpc_arg))) {
			LOG_WARN("failed to cancel remote sys task", K(ret), K(task_server), K(rpc_arg));
		  } else {
			LOG_INFO("succeed to cancel sys task at remote", K(task_server), K(rpc_arg));
		  }
		}
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

int ObSetDiskValidExecutor::execute(ObExecContext &ctx, ObSetDiskValidStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = NULL;
  obrpc::ObSrvRpcProxy *srv_rpc_proxy = NULL;
  ObAddr server = stmt.server_;
  ObSetDiskValidArg arg;

  LOG_INFO("set_disk_valid", K(server));
  if (OB_ISNULL(task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx))) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor failed");
  } else if (OB_ISNULL(srv_rpc_proxy = task_exec_ctx->get_srv_rpc())) {
    ret = OB_NOT_INIT;
    LOG_WARN("get srv rpc proxy failed");
  } else if (OB_FAIL(srv_rpc_proxy->to(GCTX.self_addr()).set_disk_valid(arg))) {
    LOG_WARN("rpc proxy set_disk_valid failed", K(ret));
  } else {
    LOG_INFO("set_disk_valid success", K(server));
  }

  return ret;
}

int ObAddDiskExecutor::execute(ObExecContext &ctx, ObAddDiskStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  obrpc::ObSrvRpcProxy *srv_rpc_proxy = NULL;

  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_ISNULL(srv_rpc_proxy = task_exec_ctx->get_srv_rpc())) {
    ret = OB_NOT_INIT;
    LOG_WARN("get server rpc proxy failed", K(task_exec_ctx));
  } else if (OB_FAIL(srv_rpc_proxy->to(stmt.arg_.server_).add_disk(stmt.arg_))) {
    LOG_WARN("failed to send add disk rpc", K(ret), "arg", stmt.arg_);
  } else {
    FLOG_INFO("succeed to send add disk rpc", "arg", stmt.arg_);
  }
  return ret;
}

int ObDropDiskExecutor::execute(ObExecContext &ctx, ObDropDiskStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  obrpc::ObSrvRpcProxy *srv_rpc_proxy = NULL;

  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_ISNULL(srv_rpc_proxy = task_exec_ctx->get_srv_rpc())) {
    ret = OB_NOT_INIT;
    LOG_WARN("get server rpc proxy failed", K(task_exec_ctx));
  } else if (OB_FAIL(srv_rpc_proxy->to(stmt.arg_.server_).drop_disk(stmt.arg_))) {
    LOG_WARN("failed to send drop disk rpc", K(ret), "arg", stmt.arg_);
  } else {
    FLOG_INFO("succeed to send drop disk rpc", "arg", stmt.arg_);
  }
  return ret;
}

int ObArchiveLogExecutor::execute(ObExecContext &ctx, ObArchiveLogStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  obrpc::ObCommonRpcProxy *common_rpc_proxy = NULL;
  common::ObCurTraceId::mark_user_request();
  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_FAIL(task_exec_ctx->get_common_rpc(common_rpc_proxy))) {
    LOG_WARN("get common rpc proxy failed", K(ret));
  } else if (OB_ISNULL(common_rpc_proxy)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("common_rpc_proxy is null", K(ret));
  } else {
    FLOG_INFO("ObArchiveLogExecutor::execute", K(stmt), K(ctx));
    obrpc::ObArchiveLogArg arg;
    arg.enable_ = stmt.is_enable();
    arg.tenant_id_ = stmt.get_tenant_id();
    if (OB_FAIL(arg.archive_tenant_ids_.assign(stmt.get_archive_tenant_ids()))) {
      LOG_WARN("failed to assign archive tenant ids", K(ret), K(stmt));
    } else if (OB_FAIL(common_rpc_proxy->archive_log(arg))) {
      LOG_WARN("archive_tenant rpc failed", K(ret), K(arg), "dst", common_rpc_proxy->get_server());
    }
  }
  return ret;
}

int ObBackupDatabaseExecutor::execute(ObExecContext &ctx, ObBackupDatabaseStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  ObSQLSessionInfo *session_info = ctx.get_my_session();
  ObCommonRpcProxy *common_proxy = NULL;
  common::ObCurTraceId::mark_user_request();
  ObString passwd;
  ObObj value;
  obrpc::ObBackupDatabaseArg arg;
  // rs will attempt to update the schema_version of the freeze point every 5s
  const int64_t SECOND = 1* 1000 * 1000; //1s
  const int64_t MAX_RETRY_NUM = UPDATE_SCHEMA_ADDITIONAL_INTERVAL / SECOND + 1;
  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_ISNULL(common_proxy = task_exec_ctx->get_common_rpc())) {
    ret = OB_NOT_INIT;
    LOG_WARN("get common rpc proxy failed", K(ret));
  } else if (OB_ISNULL(session_info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session_info must not null", K(ret));
  } else if (!session_info->user_variable_exists(OB_BACKUP_ENCRYPTION_MODE_SESSION_STR)) {
    arg.encryption_mode_ = ObBackupEncryptionMode::NONE;
    arg.passwd_.reset();
    LOG_INFO("no backup encryption mode is specified", K(stmt));
  } else {
    if (OB_FAIL(session_info->get_user_variable_value(OB_BACKUP_ENCRYPTION_MODE_SESSION_STR,
        value))) {
      LOG_WARN("failed to get encryption mode", K(ret));
    } else if (FALSE_IT(arg.encryption_mode_ = ObBackupEncryptionMode::parse_str(value.get_varchar()))) {
    } else if (!ObBackupEncryptionMode::is_valid(arg.encryption_mode_)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid mode", K(ret), K(arg), K(value));
    } else if (OB_FAIL(session_info->get_user_variable_value(OB_BACKUP_ENCRYPTION_PASSWD_SESSION_STR,
        value))) {
      LOG_WARN("failed to get passwd", K(ret));
    } else if (OB_FAIL(arg.passwd_.assign(value.get_varchar()))) {
      LOG_WARN("failed to assign passwd", K(ret));
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(arg.backup_dest_.assign(stmt.get_backup_dest()))) {
    LOG_WARN("failed to assign backup dest", K(ret));
  } else if (OB_FAIL(arg.backup_tenant_ids_.assign(stmt.get_backup_tenant_ids()))) {
    LOG_WARN("failed to assign backup tenant ids", K(ret));
  } else if (OB_FAIL(arg.backup_description_.assign(stmt.get_backup_description()))) {
    LOG_WARN("failed to assign backup description", K(ret));
  } else {
    arg.tenant_id_ = stmt.get_tenant_id();
    arg.is_incremental_ = stmt.get_incremental();
    arg.is_compl_log_ = stmt.get_compl_log();
    arg.initiator_tenant_id_ = stmt.get_tenant_id();
    LOG_INFO("ObBackupDatabaseExecutor::execute", K(stmt), K(arg), K(ctx));

    if (!arg.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid args", K(ret), K(arg));
    } else {
      int32_t retry_cnt = 0;
      while (retry_cnt < MAX_RETRY_NUM) {
        ret = OB_SUCCESS;
        if (OB_FAIL(common_proxy->backup_database(arg))) {
          if (OB_EAGAIN == ret) {
            LOG_WARN("backup_database rpc failed, need retry", K(ret), K(arg),
                "dst", common_proxy->get_server(), "retry_cnt", retry_cnt);
            ob_usleep(SECOND); //1s
          } else {
            LOG_WARN("backup_database rpc failed", K(ret), K(arg), "dst", common_proxy->get_server());
            break;
          }
        } else {
          break;
        }
        ++retry_cnt;
      }
    }
  }
  return ret;
}

int ObBackupManageExecutor::execute(ObExecContext &ctx, ObBackupManageStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  ObCommonRpcProxy *common_proxy = NULL;
  common::ObCurTraceId::mark_user_request();

  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_ISNULL(common_proxy = task_exec_ctx->get_common_rpc())) {
    ret = OB_NOT_INIT;
    LOG_WARN("get common rpc proxy failed", K(ret));
  } else {
    LOG_INFO("ObBackupManageExecutor::execute", K(stmt), K(ctx));
    obrpc::ObBackupManageArg arg;
    arg.tenant_id_ = stmt.get_tenant_id();
    arg.type_ = stmt.get_type();
    arg.value_ = stmt.get_value();
    arg.copy_id_ = stmt.get_copy_id();
    if (OB_FAIL(append(arg.managed_tenant_ids_, stmt.get_managed_tenant_ids()))) {
      LOG_WARN("failed to append managed tenants", K(ret), K(stmt));
    } else if (OB_FAIL(common_proxy->backup_manage(arg))) {
      LOG_WARN("backup_manage rpc failed", K(ret), K(arg), "dst", common_proxy->get_server());
    }
  }
  return ret;
}

int ObBackupCleanExecutor::execute(ObExecContext &ctx, ObBackupCleanStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  ObCommonRpcProxy *common_proxy = NULL;
  common::ObCurTraceId::mark_user_request();

  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_ISNULL(common_proxy = task_exec_ctx->get_common_rpc())) {
    ret = OB_NOT_INIT;
    LOG_WARN("get common rpc proxy failed", K(ret));
  } else {
    LOG_INFO("ObBackupCleanExecutor::execute", K(stmt), K(ctx));
    obrpc::ObBackupCleanArg arg;
    arg.initiator_tenant_id_ = stmt.get_tenant_id();
    arg.type_ = stmt.get_type();
    arg.value_ = stmt.get_value();
    arg.dest_id_ = stmt.get_copy_id();
    if (OB_FAIL(arg.description_.assign(stmt.get_description()))) {
      LOG_WARN("set clean description failed", K(ret));
    } else if (OB_FAIL(arg.clean_tenant_ids_.assign(stmt.get_clean_tenant_ids()))) {
      LOG_WARN("set clean tenant ids failed", K(ret));
    } else if (OB_FAIL(common_proxy->backup_delete(arg))) {
      LOG_WARN("backup clean rpc failed", K(ret), K(arg), "dst", common_proxy->get_server());
    }
  }
  FLOG_INFO("ObBackupCleanExecutor::execute");
  return ret;
}

int ObDeletePolicyExecutor::execute(ObExecContext &ctx, ObDeletePolicyStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  ObCommonRpcProxy *common_proxy = NULL;
  common::ObCurTraceId::mark_user_request();

  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_ISNULL(common_proxy = task_exec_ctx->get_common_rpc())) {
    ret = OB_NOT_INIT;
    LOG_WARN("get common rpc proxy failed", K(ret));
  } else {
    LOG_INFO("ObDeletePolicyExecutor::execute", K(stmt), K(ctx));
    obrpc::ObDeletePolicyArg arg;
    arg.initiator_tenant_id_ = stmt.get_tenant_id();
    arg.type_ = stmt.get_type();
    arg.redundancy_ = stmt.get_redundancy();
    arg.backup_copies_ = stmt.get_backup_copies();
    if (OB_FAIL(databuff_printf(arg.policy_name_, sizeof(arg.policy_name_), "%s", stmt.get_policy_name()))) {
      LOG_WARN("failed to set policy name", K(ret), K(stmt));
    } else if (OB_FAIL(databuff_printf(arg.recovery_window_, sizeof(arg.recovery_window_), "%s", stmt.get_recovery_window()))) {
      LOG_WARN("failed to set recovery window", K(ret), K(stmt));
    } else if (OB_FAIL(arg.clean_tenant_ids_.assign(stmt.get_clean_tenant_ids()))) {
      LOG_WARN("set clean tenant ids failed", K(ret));
    } else if (OB_FAIL(common_proxy->delete_policy(arg))) {
      LOG_WARN("delete policy rpc failed", K(ret), K(arg), "dst", common_proxy->get_server());
    }
  }
  FLOG_INFO("ObDeletePolicyExecutor::execute");
  return ret;
}

int ObBackupClusterParamExecutor::execute(ObExecContext &ctx, ObBackupClusterParamStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  ObSQLSessionInfo *session_info = ctx.get_my_session();
  ObCommonRpcProxy *common_proxy = NULL;
  uint64_t login_tenant_id = OB_INVALID_TENANT_ID;
  const share::ObBackupPathString &backup_dest = stmt.get_backup_dest();

  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("task exec ctx is null", KR(ret));
  } else if (OB_ISNULL(common_proxy = task_exec_ctx->get_common_rpc())) {
    ret = OB_NOT_INIT;
    LOG_WARN("get common rpc proxy failed", K(ret));
  } else if (FALSE_IT(login_tenant_id = session_info->get_login_tenant_id())) {
  } else if (OB_SYS_TENANT_ID != login_tenant_id) {
    ret = OB_OP_NOT_ALLOW;
    LOG_WARN("non-sys tenant backup cluster parameters not allowed", KR(ret), K(login_tenant_id));
    LOG_USER_ERROR(OB_OP_NOT_ALLOW, "operation from regular user tenant");
  } else if (OB_FAIL(backup::ObBackupParamOperator::backup_cluster_parameters(backup_dest))) {
    LOG_WARN("failed to backup cluster parameters", KR(ret), K(backup_dest));
  } else {
    LOG_INFO("backup cluster parameters", KR(ret), K(stmt));
  }
  return ret;
}

int ObBackupBackupsetExecutor::execute(ObExecContext &ctx, ObBackupBackupsetStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  ObCommonRpcProxy *common_proxy = NULL;
  common::ObCurTraceId::reset();
  common::ObCurTraceId::mark_user_request();

  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("task exec ctx is null", KR(ret));
  } else if (OB_ISNULL(common_proxy = task_exec_ctx->get_common_rpc())) {
    ret = OB_NOT_INIT;
    LOG_WARN("get common rpc proxy failed", K(ret));
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_ERROR("not support now", K(ret));
  }
  return ret;
}

int ObBackupArchiveLogExecutor::execute(ObExecContext &ctx, ObBackupArchiveLogStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  obrpc::ObCommonRpcProxy *common_rpc_proxy = NULL;
  common::ObCurTraceId::mark_user_request();
  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_FAIL(task_exec_ctx->get_common_rpc(common_rpc_proxy))) {
    LOG_WARN("get common rpc proxy failed", K(ret));
  } else if (OB_ISNULL(common_rpc_proxy)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("common_rpc_proxy is null", K(ret));
  } else {
    FLOG_INFO("ObBackupArchiveLogExecutor::execute", K(stmt), K(ctx));
//    obrpc::ObBackupArchiveLogArg arg;
//    arg.enable_ = stmt.is_enable();

    ret = OB_NOT_SUPPORTED;
    LOG_ERROR("not support now", K(ret));
//    if (OB_FAIL(common_rpc_proxy->backup_archive_log(arg))) {
//      LOG_WARN("archive_log rpc failed", K(ret), K(arg), "dst", common_rpc_proxy->get_server());
//    }
  }
  return ret;
}

int ObBackupBackupPieceExecutor::execute(ObExecContext &ctx, ObBackupBackupPieceStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  ObCommonRpcProxy *common_proxy = NULL;
  common::ObCurTraceId::reset();
  common::ObCurTraceId::mark_user_request();

  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("task exec ctx is null", KR(ret));
  } else if (OB_ISNULL(common_proxy = task_exec_ctx->get_common_rpc())) {
    ret = OB_NOT_INIT;
    LOG_WARN("get common rpc proxy failed", K(ret));
  } else {
    LOG_INFO("ObBackupBackupPieceExecutor::execute", K(stmt), K(ctx));
//    obrpc::ObBackupBackupPieceArg arg;
//    arg.tenant_id_ = stmt.get_tenant_id();
//    arg.piece_id_ = stmt.get_piece_id();
//    arg.max_backup_times_ = stmt.get_max_backup_times();
//    arg.backup_all_ = stmt.is_backup_all();
//    MEMCPY(arg.backup_backup_dest_, stmt.get_backup_backup_dest().ptr(), stmt.get_backup_backup_dest().length());
//    arg.with_active_piece_ = stmt.with_active_piece();

    ret = OB_NOT_SUPPORTED;
    LOG_ERROR("not support now", K(ret));
//    if (OB_FAIL(common_proxy->backup_backuppiece(arg))) {
//      LOG_WARN("backup archive log rpc failed", KR(ret), K(arg), "dst", common_proxy->get_server());
//    }
  }
  return ret;
}

int ObBackupSetEncryptionExecutor::execute(ObExecContext &ctx, ObBackupSetEncryptionStmt &stmt)
{
  int ret = OB_SUCCESS;
  common::ObCurTraceId::mark_user_request();
  ObSQLSessionInfo *session_info = ctx.get_my_session();
  ObSessionVariable encryption_mode;
  ObSessionVariable encryption_passwd;
  if (OB_ISNULL(session_info)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), KP(session_info));
  } else {
    encryption_mode.value_.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
    encryption_mode.value_.set_varchar(ObBackupEncryptionMode::to_str(stmt.get_mode()));
    encryption_mode.meta_.set_meta(encryption_mode.value_.meta_);

    encryption_passwd.value_.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
    encryption_passwd.value_.set_varchar(stmt.get_passwd().ptr(), stmt.get_passwd().length());
    encryption_passwd.meta_.set_meta(encryption_passwd.value_.meta_);

    if (OB_FAIL(session_info->replace_user_variable(OB_BACKUP_ENCRYPTION_MODE_SESSION_STR, encryption_mode))) {
      LOG_WARN("failed to set encryption mode", K(ret), K(encryption_mode));
    } else if (OB_FAIL(session_info->replace_user_variable(OB_BACKUP_ENCRYPTION_PASSWD_SESSION_STR, encryption_passwd))) {
      LOG_WARN("failed to set encryption passwd", K(ret), K(encryption_passwd));
    } else {
      LOG_INFO("ObBackupSetEncryptionExecutor::execute", K(encryption_mode), K(encryption_passwd));
    }
  }

  return ret;
}

int ObBackupSetDecryptionExecutor::execute(ObExecContext &ctx, ObBackupSetDecryptionStmt &stmt)
{
  int ret = OB_SUCCESS;
  common::ObCurTraceId::mark_user_request();
  ObSQLSessionInfo *session_info = ctx.get_my_session();
  ObSessionVariable decryption_passwd;
  if (OB_ISNULL(session_info)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), KP(session_info));
  } else {
    decryption_passwd.value_.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
    decryption_passwd.value_.set_varchar(stmt.get_passwd_array().ptr(), stmt.get_passwd_array().length());
    decryption_passwd.meta_.set_meta(decryption_passwd.value_.meta_);

    if (OB_FAIL(session_info->replace_user_variable(OB_BACKUP_DECRYPTION_PASSWD_ARRAY_SESSION_STR, decryption_passwd))) {
      LOG_WARN("failed to set decryption passwd", K(ret), K(decryption_passwd));
    } else {
      LOG_INFO("ObBackupSetDecryptionExecutor::execute", K(decryption_passwd));
    }
  }

  return ret;
}

int ObAddRestoreSourceExecutor::execute(ObExecContext &ctx, ObAddRestoreSourceStmt &stmt)
{
  int ret = OB_SUCCESS;
  common::ObCurTraceId::mark_user_request();
  ObSQLSessionInfo *session_info = ctx.get_my_session();
  ObObj value;
  ObSessionVariable new_value;

  if (OB_ISNULL(session_info)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), KP(session_info));
  } else if (!session_info->user_variable_exists(OB_RESTORE_SOURCE_NAME_SESSION_STR)) {
    LOG_INFO("no restore source specified before");
  } else {
    if (OB_FAIL(session_info->get_user_variable_value(OB_RESTORE_SOURCE_NAME_SESSION_STR, value))) {
      LOG_WARN("failed to get user variable value", KR(ret));
    } else if (OB_FAIL(stmt.add_restore_source(value.get_char()))) {
      LOG_WARN("failed to add restore source", KR(ret), K(value));
    }
  }

  if (OB_FAIL(ret)) {
  } else {
    new_value.value_.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
    new_value.value_.set_varchar(stmt.get_restore_source_array().ptr(), stmt.get_restore_source_array().length());
    new_value.meta_.set_meta(new_value.value_.meta_);

    if (OB_FAIL(session_info->replace_user_variable(OB_RESTORE_SOURCE_NAME_SESSION_STR, new_value))) {
      LOG_WARN("failed to set user variable", K(ret), K(new_value));
    } else {
      LOG_INFO("ObAddRestoreSourceExecutor::execute", K(stmt), K(new_value));
    }
  }

  return ret;
}

int ObClearRestoreSourceExecutor::execute(ObExecContext &ctx, ObClearRestoreSourceStmt &stmt)
{
  int ret = OB_SUCCESS;
  common::ObCurTraceId::mark_user_request();
  ObSQLSessionInfo *session_info = ctx.get_my_session();
  if (OB_ISNULL(session_info)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), KP(session_info));
  } else {
    if (session_info->user_variable_exists(OB_RESTORE_SOURCE_NAME_SESSION_STR)) {
      if (OB_FAIL(session_info->remove_user_variable(OB_RESTORE_SOURCE_NAME_SESSION_STR))) {
        LOG_WARN("failed to remove user variable", KR(ret));
      }
    }
  }
  UNUSED(stmt);
  return ret;
}

int ObCheckpointSlogExecutor::execute(ObExecContext &ctx, ObCheckpointSlogStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = NULL;
  obrpc::ObSrvRpcProxy *srv_rpc_proxy = NULL;
  const ObAddr server = stmt.server_;
  ObCheckpointSlogArg arg;
  arg.tenant_id_ = stmt.tenant_id_;

  if (OB_ISNULL(task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx))) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor failed");
  } else if (OB_ISNULL(srv_rpc_proxy = task_exec_ctx->get_srv_rpc())) {
    ret = OB_NOT_INIT;
    LOG_WARN("get srv rpc proxy failed");
  } else if (OB_FAIL(srv_rpc_proxy->to(GCTX.self_addr()).timeout(THIS_WORKER.get_timeout_remain()).checkpoint_slog(arg))) {
    LOG_WARN("rpc proxy checkpoint slog failed", K(ret));
  }

  LOG_INFO("checkpoint slog execute finish", K(ret), K(arg.tenant_id_), K(server));

  return ret;
}

int ObRecoverTableExecutor::execute(ObExecContext &ctx, ObRecoverTableStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = nullptr;
  ObCommonRpcProxy *common_proxy = nullptr;
  ObAddr server;
  if (OB_ISNULL(task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx))) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor failed");
  } else if (OB_ISNULL(common_proxy = task_exec_ctx->get_common_rpc())) {
    ret = OB_NOT_INIT;
    LOG_WARN("get common rpc proxy failed");
  } else if (OB_FAIL(common_proxy->recover_table(stmt.get_rpc_arg()))) {
    LOG_WARN("failed to send recover table rpc", K(ret));
  } else {
    const obrpc::ObRecoverTableArg &recover_table_rpc_arg = stmt.get_rpc_arg();
    LOG_INFO("send recover table rpc finish", K(recover_table_rpc_arg));
  }
  return ret;
}

int ObSwitchRoleExecutor::execute(ObExecContext &ctx, ObSwitchRoleStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObString first_stmt;
  if (OB_FAIL(stmt.get_first_stmt(first_stmt))) {
    LOG_WARN("fail to get first stmt", KR(ret), K(stmt));
  } else {
    obrpc::ObSwitchRoleArg &arg = stmt.get_arg();
    // Always use sys tenant, arg already initialized in resolver with OB_SYS_TENANT_ID
    arg.set_stmt_str(first_stmt);
    if (OB_FAIL(OB_STANDBY_SERVICE.switch_role(arg))) {
      LOG_WARN("failed to switch_tenant", KR(ret), K(arg));
    }
  }
  return ret;
}

int ObCancelRestoreExecutor::execute(ObExecContext &ctx, ObCancelRestoreStmt &stmt)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObTableTTLExecutor::execute(ObExecContext& ctx, ObTableTTLStmt& stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx* task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  obrpc::ObCommonRpcProxy* common_rpc_proxy = NULL;

  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_FAIL(task_exec_ctx->get_common_rpc(common_rpc_proxy))) {
    LOG_WARN("get common rpc proxy failed", K(ret));
  } else if (OB_ISNULL(common_rpc_proxy)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("common_rpc_proxy is null", K(ret));
  } else {
    FLOG_INFO("ObTableTTLExecutor::execute", K(stmt), K(ctx));
    common::ObTTLParam param;
    ObSEArray<common::ObSimpleTTLInfo, 32> ttl_info_array;
    param.ttl_all_ = stmt.is_ttl_all();
    param.transport_ = GCTX.net_frame_->get_req_transport();
    param.type_ = stmt.get_type();
    for (int64_t i = 0; (i < stmt.get_tenant_ids().count()) && OB_SUCC(ret); i++) {
      uint64_t tenant_id = stmt.get_tenant_ids().at(i);
      if (OB_FAIL(param.add_ttl_info(tenant_id))) {
        LOG_WARN("fail to assign ttl info", KR(ret), K(tenant_id));
      }
    }
    if (OB_FAIL(ret)) {
      // do nothing
    } else if (OB_UNLIKELY(!param.ttl_all_ && param.ttl_info_array_.empty())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(ret), K(param), KR(ret));
    } else if (OB_FAIL(ObTTLUtil::dispatch_ttl_cmd(param))) {
      LOG_WARN("fail to dispatch ttl cmd", K(ret), K(param));
    }
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

int ObModuleDataExecutor::execute(ObExecContext &ctx, ObModuleDataStmt &stmt)
{
  int ret = OB_SUCCESS;
  int64_t start_time = ObTimeUtility::current_time();
  const int64_t INNER_SQL_TIMEOUT = GCONF.internal_sql_execute_timeout;
  ObTimeoutCtx timeout_ctx;
  const table::ObModuleDataArg &arg = stmt.get_arg();
  LOG_INFO("start to handle module_data", K(arg), K(INNER_SQL_TIMEOUT), K(start_time));
  if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid ObModuleDataArg", K(ret), K(arg));
  } else if (OB_FAIL(ObShareUtil::set_default_timeout_ctx(timeout_ctx, INNER_SQL_TIMEOUT))) {
    LOG_WARN("failed to set default timeout ctx", K(ret), K(INNER_SQL_TIMEOUT));
  } else {
    switch (arg.module_) {
      case table::ObModuleDataArg::REDIS: {
        table::ObRedisImporter importer(arg.target_tenant_id_, ctx);
        if (OB_FAIL(importer.exec_op(arg.op_))) {
          LOG_WARN("fail to exec op", K(ret), K(arg.op_));
        }
         break;
      }
      case table::ObModuleDataArg::GIS: {
        table::ObSRSImporter importer(arg.target_tenant_id_, ctx);
        if (OB_FAIL(importer.exec_op(arg))) {
          LOG_WARN("fail to exec op", K(ret), K(arg.op_));
        }
        break;
      }
      case table::ObModuleDataArg::TIMEZONE: {
        table::ObTimezoneImporter importer(arg.target_tenant_id_, ctx);
        if (OB_FAIL(importer.exec_op(arg))) {
          LOG_WARN("fail to exec op", K(ret), K(arg.op_));
        }
        break;
      }
      // add other module before here
      default: {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "specified module");
        LOG_WARN("modules except 'redis'/'gis'/'timezone' are not supported yet", K(ret), K(arg.module_));
      }
    }
  }
  LOG_INFO("handle module data ended",
      K(ret), K(arg), "cost_time", ObTimeUtility::current_time() - start_time);
  return ret;
}

} // end namespace sql
} // end namespace oceanbase
