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
#include "rootserver/ob_local_ddl_serial_call.h"
#include "share/ob_ex_rpc.h"
#include "share/io/ob_io_manager.h"
#include "share/io/ob_io_calibration.h"
#include "storage/meta_store/ob_server_storage_meta_service.h"
#include "storage/meta_store/ob_local_storage_meta_service.h"
#include "observer/ob_server.h"
#include "observer/scheduler/ob_dag_warning_history_mgr.h"
#include "observer/omt/ob_server_runtime.h" //ObServerRuntime
#include "rootserver/freeze/ob_major_freeze_helper.h" //ObMajorFreezeHelper
#include "pl/pl_cache/ob_pl_cache_mgr.h"
#include "sql/plan_cache/ob_ps_cache.h"

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
      arg.tablet_id_ = stmt.get_tablet_id();
      if (OB_FAIL(GCTX.local_management_service_->root_minor_freeze(arg))) {
        LOG_WARN("minor freeze failed", K(arg), K(ret), "dst", GCTX.self_addr());
      }
    } else if (stmt.get_tablet_id().is_valid()) {
      rootserver::ObTabletMajorFreezeParam param;
      param.tablet_id_ = stmt.get_tablet_id();
      if (OB_FAIL(rootserver::ObMajorFreezeHelper::tablet_major_freeze(param))) {
        LOG_WARN("failed to schedule tablet major freeze", K(ret), K(param));
      }
    } else {
      rootserver::ObMajorFreezeParam param;
      param.freeze_reason_ = rootserver::MF_USER_REQUEST;
      if (OB_FAIL(rootserver::ObMajorFreezeHelper::major_freeze(param))) {
        if (OB_FROZEN_INFO_ALREADY_EXIST == ret
            || OB_MAJOR_FREEZE_NOT_FINISHED == ret) {
          if (!stmt.has_runtime_selector()) {
            const char *warn_buf =
                "larger frozen_scn already exist, prev merge may not finish";
            LOG_USER_WARN(OB_FROZEN_INFO_ALREADY_EXIST, warn_buf);
          }
          ret = OB_SUCCESS;
        } else {
          LOG_WARN("failed to launch major freeze", KR(ret), K(param));
        }
      }
      LOG_INFO("major freeze request finished", KR(ret), K(param));
    }
  }
  return ret;
}

int ObFlushCacheExecutor::execute(ObExecContext &ctx, ObFlushCacheStmt &stmt)
{
  UNUSED(ctx);
  int ret = OB_SUCCESS;
  const int64_t db_num = stmt.flush_cache_arg_.db_ids_.count();
  common::ObString sql_id = stmt.flush_cache_arg_.sql_id_;
  switch (stmt.flush_cache_arg_.cache_type_) {
      case CACHE_TYPE_LIB_CACHE: {
        SERVER_MODULE_SCOPE {
          ObPlanCache *plan_cache = share::g_mp->plan_cache();
          if (OB_ISNULL(plan_cache)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("plan cache is null", K(ret));
          } else if (stmt.flush_cache_arg_.ns_type_ != ObLibCacheNameSpace::NS_INVALID) {
            ret = plan_cache->flush_lib_cache_by_ns(stmt.flush_cache_arg_.ns_type_);
          } else {
            ret = plan_cache->flush_lib_cache();
          }
        }
        break;
      }
      case CACHE_TYPE_PLAN: {
        SERVER_MODULE_SCOPE {
          ObPlanCache *plan_cache = share::g_mp->plan_cache();
          if (OB_ISNULL(plan_cache)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("plan cache is null", K(ret));
          } else if (!stmt.flush_cache_arg_.is_fine_grained_) {
            ret = plan_cache->flush_plan_cache();
          } else if (0 == db_num) {
            ret = plan_cache->flush_plan_cache_by_sql_id(OB_INVALID_ID, sql_id);
          } else {
            for (int64_t i = 0; OB_SUCC(ret) && i < db_num; ++i) {
              ret = plan_cache->flush_plan_cache_by_sql_id(
                  stmt.flush_cache_arg_.db_ids_.at(i), sql_id);
            }
          }
        }
        break;
      }
      case CACHE_TYPE_PL_OBJ: {
        SERVER_MODULE_SCOPE {
          ObPlanCache *plan_cache = share::g_mp->plan_cache();
          const bool by_schema_id =
              common::OB_INVALID_ID != stmt.flush_cache_arg_.schema_id_;
          if (OB_ISNULL(plan_cache)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("plan cache is null", K(ret));
          } else if (!stmt.flush_cache_arg_.is_fine_grained_) {
            ret = plan_cache->flush_pl_cache();
          } else if (0 == db_num) {
            if (by_schema_id) {
              ret = plan_cache->flush_pl_cache_single_cache_obj<
                  pl::ObGetPLKVEntryBySchemaIdOp>(
                      OB_INVALID_ID, stmt.flush_cache_arg_.schema_id_);
            } else {
              ret = plan_cache->flush_pl_cache_single_cache_obj<
                  pl::ObGetPLKVEntryBySQLIDOp>(OB_INVALID_ID, sql_id);
            }
          } else {
            for (int64_t i = 0; OB_SUCC(ret) && i < db_num; ++i) {
              const uint64_t db_id = stmt.flush_cache_arg_.db_ids_.at(i);
              if (by_schema_id) {
                ret = plan_cache->flush_pl_cache_single_cache_obj<
                    pl::ObGetPLKVEntryBySchemaIdOp>(
                        db_id, stmt.flush_cache_arg_.schema_id_);
              } else if (sql_id.empty()) {
                ret = plan_cache->flush_pl_cache_single_cache_obj<
                    pl::ObGetPLKVEntryByDbIdOp, uint64_t>(
                        db_id, stmt.flush_cache_arg_.schema_id_);
              } else {
                ret = plan_cache->flush_pl_cache_single_cache_obj<
                    pl::ObGetPLKVEntryBySQLIDOp>(db_id, sql_id);
              }
            }
          }
        }
        break;
      }
      case CACHE_TYPE_PS_OBJ: {
        SERVER_MODULE_SCOPE {
          ObPsCache *ps_cache = share::g_mp->ps_cache();
          if (OB_ISNULL(ps_cache)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("ps cache is null", K(ret));
          } else if (ps_cache->is_inited()) {
            ret = ps_cache->cache_evict_all_ps();
          }
        }
        break;
      }
      default: {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid cache type", "type", stmt.flush_cache_arg_.cache_type_);
        break;
      }
  }
  return ret;
}

int ObFlushKVCacheExecutor::execute(ObExecContext &ctx, ObFlushKVCacheStmt &stmt)
{
  UNUSED(ctx);
  int ret = OB_SUCCESS;
  if (stmt.cache_name_.is_empty()) {
    if (OB_FAIL(common::ObKVGlobalCache::get_instance().erase_cache())) {
      LOG_WARN("clear kv cache failed", K(ret));
    } else {
      LOG_INFO("success erase all kvcache");
    }
  } else if (OB_FAIL(common::ObKVGlobalCache::get_instance().erase_cache(
                 stmt.cache_name_.ptr()))) {
    LOG_WARN("clear kv cache failed", K(ret), K(stmt.cache_name_));
  } else {
    LOG_INFO("success erase kvcache", K(stmt.cache_name_));
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
  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else {
    switch (stmt.get_merge_type()) {
      case ObAdminMergeStmt::MergeType::SUSPEND:
        if (OB_FAIL(rootserver::ObMajorFreezeHelper::suspend_merge())) {
          LOG_WARN("fail to suspend merge", KR(ret));
        }
        break;
      case ObAdminMergeStmt::MergeType::RESUME:
        if (OB_FAIL(rootserver::ObMajorFreezeHelper::resume_merge())) {
          LOG_WARN("fail to resume merge", KR(ret));
        }
        break;
      default:
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid merge type", KR(ret), K(stmt));
        break;
    }
  }
  return ret;
}




int ObRefreshMemStatExecutor::execute(ObExecContext &ctx, ObRefreshMemStatStmt &stmt)
{
  int ret = OB_SUCCESS;
  UNUSED(stmt);
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_ISNULL(GCTX.ob_service_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ob service is null", K(ret));
  } else if (OB_FAIL(GCTX.ob_service_->refresh_memory_stat())) {
    LOG_WARN("refresh memory stat failed", K(ret));
  }
  return ret;
}

int ObRefreshIOCalibraitonExecutor::execute(ObExecContext &ctx, ObRefreshIOCalibraitonStmt &stmt)
{
  int ret = OB_SUCCESS;
  const ObRefreshIOCalibrationParam &param = stmt.get_param();
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_UNLIKELY(!param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid refresh io calibration parameter", K(ret), K(param));
  } else if (!param.only_refresh_) {
    ObIOAbility io_ability;
    for (int64_t i = 0; OB_SUCC(ret) && i < param.calibration_list_.count(); ++i) {
      const ObIOBenchResult &item = param.calibration_list_.at(i);
      if (OB_FAIL(io_ability.add_measure_item(item))) {
        LOG_WARN("add io calibration item failed", K(ret), K(item));
      }
    }
    if (OB_SUCC(ret) && param.calibration_list_.count() > 0 && !io_ability.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid calibration list", K(ret), K(param), K(io_ability));
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(ObIOCalibration::get_instance().refresh(
                                 param.only_refresh_, param.calibration_list_))) {
    LOG_WARN("refresh local io calibration failed", K(ret), K(param));
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
  } else if (OB_FAIL(GCTX.local_management_service_->admin_set_config(stmt.get_rpc_arg()))) {
    LOG_WARN("set config rpc failed", K(ret), "rpc_arg", stmt.get_rpc_arg());
  }
  return ret;
}

int ObSetTPExecutor::execute(ObExecContext &ctx, ObSetTPStmt &stmt)
{
  int ret = OB_SUCCESS;
  UNUSED(ctx);
  if (OB_ISNULL(GCTX.ob_service_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ob_service_ is null", K(ret));
  } else if (OB_FAIL(GCTX.ob_service_->set_tracepoint(stmt.get_param()))) {
    LOG_WARN("set tracepoint failed", K(ret), K(stmt.get_param()));
  } else {
    LOG_INFO("set tracepoint locally", K(stmt.get_param()));
  }
  return ret;
}

int ObClearMergeErrorExecutor::execute(ObExecContext &ctx, ObClearMergeErrorStmt &stmt)
{
  int ret = OB_SUCCESS;
  UNUSED(stmt);
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);
  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_FAIL(rootserver::ObMajorFreezeHelper::clear_merge_error())) {
    LOG_WARN("clear merge error failed", K(ret));
  }
  return ret;
}





int ObCancelTaskExecutor::execute(ObExecContext &ctx, ObCancelTaskStmt &stmt)
{
  int ret = OB_SUCCESS;
  share::ObTaskId task_id;

  LOG_INFO("cancel sys task log", K(stmt.get_task_id()), K(stmt.get_cmd_type()));

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

int ObResetConfigExecutor::execute(ObExecContext &ctx, ObResetConfigStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx);

  if (OB_ISNULL(task_exec_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_FAIL(GCTX.local_management_service_->admin_set_config(stmt.get_rpc_arg()))) {
    LOG_WARN("set config rpc failed", K(ret), "rpc_arg", stmt.get_rpc_arg());
  }
  return ret;
}


} // end namespace sql
} // end namespace oceanbase
