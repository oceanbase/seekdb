#include "observer/ob_ex_rpc.h"
#include "share/rc/ob_module_provider.h"
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

#define USING_LOG_PREFIX RS


#include "ob_system_admin_util.h"
#include "observer/ob_srv_network_frame.h"
#include "ob_root_service.h"
#include "rootserver/freeze/ob_major_freeze_helper.h"
#include "share/ob_cluster_event_history_table_operator.h"//CLUSTER_EVENT_INSTANCE
#include "sql/plan_cache/ob_plan_cache.h"
#include "sql/plan_cache/ob_ps_cache.h"
#include "share/rc/ob_tenant_base.h"
#include "pl/ob_pl.h"
#include "pl/pl_cache/ob_pl_cache_mgr.h"
#include "share/cache/ob_cache_name_define.h"
#include "observer/omt/ob_multi_tenant.h"
namespace oceanbase
{
using namespace common;
using namespace common::hash;
using namespace share;
using namespace share::schema;
using namespace obcall;

namespace rootserver
{

int ObAdminCallServer::get_server_list(const ObServerZoneArg &arg, ObIArray<ObAddr> &server_list)
{
  int ret = OB_SUCCESS;
  server_list.reset();
  if (!ctx_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), KR(ret));
  } else if (OB_FAIL(server_list.push_back(GCTX.self_addr()))) {
    LOG_WARN("fail to push back server", KR(ret));
  }
  return ret;
}

int ObAdminCallServer::call_all(const ObServerZoneArg &arg)
{
  int ret = OB_SUCCESS;
  ObArray<ObAddr> server_list;
  if (OB_FAIL(get_server_list(arg, server_list))) {
    LOG_WARN("get server list failed", K(ret), K(arg));
  } else {
    FOREACH_CNT(server, server_list) {
      int tmp_ret = call_server(*server);
      if (OB_SUCCESS != tmp_ret) {
        LOG_WARN("call server failed", KR(ret), "server", *server);
        ret = OB_SUCCESS == ret ? tmp_ret : ret;
      }
    }
  }
  return ret;
}

int ObAdminClearMergeError::execute(const obcall::ObAdminMergeArg &arg)
{
  LOG_INFO("execute clear merge error request", K(arg));
  int ret = OB_SUCCESS;
  if (!ctx_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), KR(ret));
  } else {
    ObTenantAdminMergeParam param;
    if (arg.affect_all_ || arg.affect_all_user_ || arg.affect_all_meta_) {
      if ((true == arg.affect_all_ && true == arg.affect_all_user_) ||
          (true == arg.affect_all_ && true == arg.affect_all_meta_) ||
          (true == arg.affect_all_user_ && true == arg.affect_all_meta_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("only one of affect_all,affect_all_user,affect_all_meta can be true",
                 KR(ret), "affect_all", arg.affect_all_, "affect_all_user",
                 arg.affect_all_user_, "affect_all_meta", arg.affect_all_meta_);
      } else {
        if (arg.affect_all_) {
          param.need_all_ = true;
        } else if (arg.affect_all_user_) {
          param.need_all_user_ = true;
        } else {
          param.need_all_meta_ = true;
        }
      }
    } else {
      param.specified_ = true;
    }
    if (FAILEDx(ObMajorFreezeHelper::clear_merge_error(param))) {
      LOG_WARN("fail to clear merge error", KR(ret), K(param));
    }
  }
  return ret;
}

int ObAdminMerge::execute(const obcall::ObAdminMergeArg &arg)
{
  LOG_INFO("execute merge admin request", K(arg));
  int ret = OB_SUCCESS;
  if (!ctx_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), KR(ret));
  } else {
    switch(arg.type_) {
      case ObAdminMergeArg::START_MERGE: {
        /* if (OB_FAIL(ctx_.daily_merge_scheduler_->manual_start_merge(arg.zone_))) {
          LOG_WARN("start merge zone failed", K(ret), K(arg));
        }*/
        break;
      }
      case ObAdminMergeArg::SUSPEND_MERGE: {
        ObTenantAdminMergeParam param;
        if (arg.affect_all_ || arg.affect_all_user_ || arg.affect_all_meta_) {
          if ((true == arg.affect_all_ && true == arg.affect_all_user_) ||
              (true == arg.affect_all_ && true == arg.affect_all_meta_) ||
              (true == arg.affect_all_user_ && true == arg.affect_all_meta_)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("only one of affect_all,affect_all_user,affect_all_meta can be true",
                     KR(ret), "affect_all", arg.affect_all_, "affect_all_user",
                     arg.affect_all_user_, "affect_all_meta", arg.affect_all_meta_);
          } else {
            if (arg.affect_all_) {
              param.need_all_ = true;
            } else if (arg.affect_all_user_) {
              param.need_all_user_ = true;
            } else {
              param.need_all_meta_ = true;
            }
          }
        } else {
          param.specified_ = true;
        }
        if (FAILEDx(ObMajorFreezeHelper::suspend_merge(param))) {
          LOG_WARN("fail to suspend merge", KR(ret), K(param));
        }
        break;
      }
      case ObAdminMergeArg::RESUME_MERGE: {
        ObTenantAdminMergeParam param;
        if (arg.affect_all_ || arg.affect_all_user_ || arg.affect_all_meta_) {
          if ((true == arg.affect_all_ && true == arg.affect_all_user_) ||
              (true == arg.affect_all_ && true == arg.affect_all_meta_) ||
              (true == arg.affect_all_user_ && true == arg.affect_all_meta_)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("only one of affect_all,affect_all_user,affect_all_meta can be true",
                     KR(ret), "affect_all", arg.affect_all_, "affect_all_user",
                     arg.affect_all_user_, "affect_all_meta", arg.affect_all_meta_);
          } else {
            if (arg.affect_all_) {
              param.need_all_ = true;
            } else if (arg.affect_all_user_) {
              param.need_all_user_ = true;
            } else {
              param.need_all_meta_ = true;
            }
          }
        } else {
          param.specified_ = true;
        }
        if (FAILEDx(ObMajorFreezeHelper::resume_merge(param))) {
          LOG_WARN("fail to resume merge", KR(ret), K(param));
        }
        break;
      }
      default: {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("unsupported merge admin type", "type", arg.type_, KR(ret));
      }
    }
  }
  return ret;
}

int ObAdminClearRoottable::execute(const obcall::ObAdminClearRoottableArg &arg)
{
  int ret = OB_NOT_SUPPORTED;
  UNUSED(arg);
  return ret;
}

//FIXME: flush schemas of all tenants
int ObAdminRefreshSchema::execute(const obcall::ObAdminRefreshSchemaArg &arg)
{
  LOG_INFO("execute refresh schema", K(arg));
  int ret = OB_SUCCESS;
  if (!ctx_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), KR(ret));
  } else if (OB_FAIL(ctx_.ddl_service_->refresh_schema())) {
    LOG_WARN("refresh schema failed", KR(ret));
  } else {
    if (OB_FAIL(ctx_.schema_service_->get_tenant_schema_version(schema_version_))) {
      LOG_WARN("fail to get schema version", KR(ret));
     } else if (OB_FAIL(ctx_.schema_service_->get_refresh_schema_info(schema_info_))) {
       LOG_WARN("fail to get refresh schema info", KR(ret), K(schema_info_));
     } else if (!schema_info_.is_valid()) {
       schema_info_.set_schema_version(schema_version_);
     }
     if (OB_FAIL(ret)) {
     } else if (OB_FAIL(call_all(arg))) {
      LOG_WARN("execute notify refresh schema failed", KR(ret), K(arg));
    }
  }
  return ret;
}

int ObAdminRefreshSchema::call_server(const ObAddr &server)
{
  int ret = OB_SUCCESS;
  ObTimeoutCtx ctx;
  if (OB_UNLIKELY(!ctx_.is_inited())) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(!server.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid server", KR(ret), K(server));
  } else if (OB_FAIL(ObShareUtil::set_default_timeout_ctx(ctx, GCONF.rpc_timeout))) {
    LOG_WARN("fail to set timeout ctx", KR(ret));
  } else {
    ObSwitchSchemaArg arg;
    arg.schema_info_ = schema_info_;
    // RPC removed: target is self on single replica; dispatch in-process.
    // Wrapped in ex_rpc::sync_call for greppability (was proxy.call + wait_all).
    obcall::ObSwitchSchemaResult result;
    ret = ex_rpc::sync_call([&]{ return GCTX.ob_service_->switch_schema(arg, result); });
  }
  return ret;
}

int ObAdminRefreshMemStat::execute(const ObAdminRefreshMemStatArg &arg)
{
  LOG_INFO("execute refresh memory stat");
  int ret = OB_SUCCESS;
  if (!ctx_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_FAIL(call_all(arg))) {
   LOG_WARN("execute notify refresh memory stat failed", KR(ret));
  }
  return ret;
}

int ObAdminRefreshMemStat::call_server(const ObAddr &server)
{
  int ret = OB_SUCCESS;
  if (!ctx_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (!server.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid server", K(server), KR(ret));
  } else if (OB_FAIL(GCTX.ob_service_->refresh_memory_stat())) {
    LOG_WARN("notify refresh memory stat failed", KR(ret), K(server));
  }
  return ret;
}

int ObAdminWashMemFragmentation::execute(const ObAdminWashMemFragmentationArg &arg)
{
  LOG_INFO("execute sync wash fragment");
  int ret = OB_SUCCESS;
  if (!ctx_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(call_all(arg))) {
    LOG_WARN("execute notify sync wash fragment failed", K(ret));
  }
  return ret;
}

int ObAdminWashMemFragmentation::call_server(const ObAddr &server)
{
  int ret = OB_SUCCESS;
  if (!ctx_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!server.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid server", K(server), K(ret));
  } else if (OB_FAIL(GCTX.ob_service_->wash_memory_fragmentation())) {
    LOG_WARN("notify sync wash fragment failed", K(ret), K(server));
  }
  return ret;
}

int ObAdminSetConfig::verify_config(obcall::ObAdminSetConfigArg &arg)
{
  int ret = OB_SUCCESS;
  void *ptr = nullptr, *cfg_ptr = nullptr;
  ObServerConfigChecker *cfg = nullptr;

  if (!ctx_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), KR(ret));
  }

  if (nullptr == cfg) {
    if (OB_ISNULL(ptr = ob_malloc(sizeof(ObServerConfigChecker),
                                ObModIds::OB_RS_PARTITION_TABLE_TEMP))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc memory", KR(ret));
    } else if (OB_ISNULL(cfg = new (ptr) ObServerConfigChecker)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("new cfg failed", KR(ret));
    }
  }

  FOREACH_X(item, arg.items_, OB_SUCCESS == ret) {
    if (item->name_.is_empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("empty config name", "item", *item, KR(ret));
    } else {
      ObConfigItem *ci = nullptr;
      ObString config_name(item->name_.size(), item->name_.ptr());
      if (false || item->tenant_name_.size() > 0) {
        // tenants(user or sys tenants) modify tenant level configuration
        item->want_to_set_tenant_config_ = true;

        if (OB_SUCC(ret)) {
          ObConfigItem * const *ci_ptr = cfg->get_container().get(
                                          ObConfigStringKey(item->name_.ptr()));
          if (OB_ISNULL(ci_ptr)) {
            ret = OB_ERR_SYS_CONFIG_UNKNOWN;
            LOG_WARN("can't found config item", KR(ret), "item", *item);
          } else {
            ci = *ci_ptr;
            share::schema::ObSchemaGetterGuard schema_guard;
            const char *const NAME_ALL = "all";
            const char *const NAME_ALL_USER = "all_user";
            const char *const NAME_ALL_META = "all_meta";
            if (OB_ISNULL(GCTX.schema_service_)) {
              ret = OB_INVALID_ARGUMENT;
              LOG_WARN("invalid argument", KR(ret), KP(GCTX.schema_service_));
            } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
              LOG_WARN("fail to get sys tenant schema guard", KR(ret));
            } else if (true &&
                      (0 == item->tenant_name_.str().case_compare(NAME_ALL) ||
                       0 == item->tenant_name_.str().case_compare(NAME_ALL_USER) ||
                       0 == item->tenant_name_.str().case_compare(NAME_ALL_META))) {
              // lite: no user/meta tenants -> ALL/ALL_USER/ALL_META applies to nothing
            } else if (true
                       && item->tenant_name_ == ObFixedLengthString<common::OB_MAX_TENANT_NAME_LENGTH + 1>("seed")) {
              
              if (OB_FAIL(item->batch_ids_.push_back(1UL))) {
                LOG_WARN("add seed id failed", KR(ret));
                break;
              }
            } else {
              const ObTenantSchema *tenant_schema = nullptr;
              if (OB_FAIL(ret)) {
              } else if (OB_FAIL(schema_guard.get_tenant_info(tenant_schema))) {
                LOG_WARN("fail to get tenant info", KR(ret));
              } else if (OB_ISNULL(tenant_schema)) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("tenant_schema is null", KR(ret));
              } else if (OB_FAIL(item->batch_ids_.push_back(1UL))) {
                LOG_WARN("add id failed", KR(ret));
              }
            } // else
          } // else
        } // if
      } else {
        if (OB_SUCC(ret)) {
          ObConfigItem * const *sys_ci_ptr = cfg->get_container().get(
                                             ObConfigStringKey(item->name_.ptr()));
          if (OB_NOT_NULL(sys_ci_ptr)) {
            ci = *sys_ci_ptr;
          } else {
            ret = OB_ERR_SYS_CONFIG_UNKNOWN;
            LOG_WARN("can't found config item", KR(ret), "item", *item);
          }
        } // if
      } // else

      if (OB_SUCC(ret)) {
        const char *err = NULL;
        if (ci->is_not_editable() && !arg.is_inner_) {
          ret = OB_INVALID_CONFIG; //TODO: specific report not editable
          LOG_WARN("config is not editable", "item", *item, KR(ret));
        } else if (!ci->check_unit(item->value_.ptr())) {
          ret = OB_INVALID_CONFIG;
          LOG_ERROR("invalid config", "item", *item, KR(ret));
        } else if (!ci->set_value_unsafe(item->value_.ptr())) {
          ret = OB_INVALID_CONFIG;
          LOG_WARN("invalid config", "item", *item, KR(ret));
        } else if (!ci->check()) {
          ret = OB_INVALID_CONFIG;
          LOG_WARN("invalid value range", "item", *item, KR(ret));
        } else if (!ctx_.root_service_->check_config(*ci, err)) {
          ret = OB_INVALID_CONFIG;
          LOG_WARN("invalid value range", "item", *item, KR(ret));
        }
        if (OB_FAIL(ret)) {
          if (nullptr != err) {
            LOG_USER_ERROR(OB_INVALID_CONFIG, err);
          }
        }
      } // if
    } // else
  } // FOREACH_X

  if (nullptr != cfg) {
    cfg->~ObServerConfigChecker();
    ob_free(cfg);
    cfg = nullptr;
    ptr = nullptr;
  } else if (nullptr != ptr) {
    ob_free(ptr);
    ptr = nullptr;
  }
  if (nullptr != cfg_ptr) {
    ob_free(cfg_ptr);
    cfg_ptr = nullptr;
  }
  return ret;
}

ERRSIM_POINT_DEF(ERRSIM_UPDATE_MIN_CONFIG_VERSION_ERROR);
int ObAdminSetConfig::update_config(obcall::ObAdminSetConfigArg &arg)
{
  int ret = OB_SUCCESS;
  if (!ctx_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), KR(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < arg.items_.count(); ++i) {
      const ObAdminSetConfigItem &item = arg.items_.at(i);
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(update_sys_config_(item))) {
        LOG_WARN("fail to update sys config", KR(ret), K(item));
      }
    } // end for each item
  }

  return ret;
}

int ObAdminSetConfig::update_sys_config_(const obcall::ObAdminSetConfigItem &item)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (OB_ISNULL(GCTX.config_mgr_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(GCTX.config_mgr_));
  } else {
    if (OB_SUCC(ret) && OB_NOT_NULL(GCTX.config_mgr_)) {
      if (OB_FAIL(GCTX.config_mgr_->save_config(
                    item.name_.ptr(), item.value_.ptr()))) {
        LOG_WARN("failed to save config", KR(ret), K(item));
      }
    }
  }
  // try update local memory and trigger remote server to refresh this change
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(GCTX.config_mgr_->got_version())) {
    LOG_WARN("config mgr got version failed", KR(ret));
  } else if (OB_FAIL(GCTX.config_mgr_->reload_config())) {
    LOG_WARN("reload configuration failed", K(ret));
  } else {
    LOG_INFO("got new sys config", K(item));
  }
  return ret;
}

int ObAdminSetConfig::execute(obcall::ObAdminSetConfigArg &arg)
{
  LOG_INFO("execute set config request", K(arg));
  DEBUG_SYNC(BEFORE_EXECUTE_ADMIN_SET_CONFIG);
  int ret = OB_SUCCESS;
  if (!ctx_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (!arg.is_valid() || OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), KR(ret), KP(GCTX.sql_proxy_));
  } else if (OB_FAIL(verify_config(arg))) {
    LOG_WARN("verify config failed", KR(ret), K(arg));
  } else {
    if (OB_FAIL(ctx_.root_service_->set_config_pre_hook(arg))) {
      LOG_WARN("fail to process pre hook", K(arg), KR(ret));
    } else if (OB_FAIL(update_config(arg))) {
      LOG_WARN("update config failed", KR(ret), K(arg));
    } else {
      LOG_INFO("set config succ", K(arg));
    }
  }
  return ret;
}

DEFINE_ENUM_FUNC(ObInnerJob, inner_job, OB_INNER_JOB_DEF);

int ObAdminRefreshIOCalibration::execute(const obcall::ObAdminRefreshIOCalibrationArg &arg)
{
  int ret = OB_SUCCESS;
  ObArray<ObAddr> server_list;
  if (OB_UNLIKELY(!ctx_.is_inited())) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(arg));
  } else if (OB_FAIL(get_server_list(arg, server_list))) {
    LOG_WARN("get server list failed", K(ret), K(arg));
  } else if (arg.only_refresh_) {
    // do nothing
  } else {
    ObIOAbility io_ability;
    for (int64_t i = 0; OB_SUCC(ret) && i < arg.calibration_list_.count(); ++i) {
      const ObIOBenchResult &item = arg.calibration_list_.at(i);
      if (OB_FAIL(io_ability.add_measure_item(item))) {
        LOG_WARN("add item failed", K(ret), K(item));
      }
    }
    if (OB_SUCC(ret)) {
      if (arg.calibration_list_.count() > 0 && !io_ability.is_valid()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid calibration list", K(ret), K(arg), K(io_ability));
      }
    }
    // write_into_table is removed; io ability is refreshed on each observer via RPC below.
  }
  if (OB_SUCC(ret)) {
    ObRefreshIOCalibrationArg refresh_arg;
    refresh_arg.storage_name_ = arg.storage_name_;
    refresh_arg.only_refresh_ = arg.only_refresh_;
    if (OB_FAIL(refresh_arg.calibration_list_.assign(arg.calibration_list_))) {
      LOG_WARN("assign calibration list failed", K(ret), K(arg.calibration_list_));
    } else {
      int64_t succ_count = 0;
      FOREACH_CNT(server, server_list) {
        int tmp_ret = ObIOCalibration::get_instance().refresh(refresh_arg.only_refresh_, refresh_arg.calibration_list_);
        if (OB_UNLIKELY(OB_SUCCESS != tmp_ret)) {
          LOG_WARN("request io calibration failed", KR(tmp_ret), K(*server), K(refresh_arg));
        } else {
          ++succ_count;
        }
      }
      if (server_list.count() != succ_count) {
        ret = OB_PARTIAL_FAILED;
        LOG_USER_ERROR(OB_PARTIAL_FAILED, "Partial failed");
      }
    }
  }
  LOG_INFO("admin refresh io calibration", K(ret), K(arg), K(server_list));
  return ret;
}

int ObAdminRefreshIOCalibration::call_server(const common::ObAddr &server)
{
  // should never go here
  UNUSED(server);
  return OB_NOT_SUPPORTED;
}

int ObAdminFlushCache::execute(const obcall::ObAdminFlushCacheArg &arg)
{
  int ret = OB_SUCCESS;
  int64_t tenant_num = arg.batch_ids_.count();
  ObSEArray<ObAddr, 8> server_list;
  ObFlushCacheArg fc_arg;
  // fine-grained plan evict only will pass this way.
  // This because fine-grained plan evict must specify tenant
  // if tenant num is 0, flush all tenant, else, flush appointed tenant
  if (tenant_num != 0) { //flush appointed tenant
    for (int64_t i = 0; OB_SUCC(ret) && i < tenant_num; ++i) {
      //get tenant server list;
      if (OB_FAIL(get_tenant_servers(server_list))) {
        LOG_WARN("fail to get tenant servers", K(i));
      } else {
        //call tenant servers;
        fc_arg.is_all_tenant_ = false;
        fc_arg.cache_type_ = arg.cache_type_;
        fc_arg.ns_type_ = arg.ns_type_;
        // fine-grained plan evict args
        if (arg.is_fine_grained_) {
          fc_arg.sql_id_ = arg.sql_id_;
          fc_arg.is_fine_grained_ = arg.is_fine_grained_;
          fc_arg.schema_id_ = arg.schema_id_;
          for(int64_t j=0; OB_SUCC(ret) && j<arg.db_ids_.count(); j++) {
            if (OB_FAIL(fc_arg.push_database(arg.db_ids_.at(j)))) {
              LOG_WARN("fail to add db ids", KR(ret));
            }
          }
        }
        for (int64_t j = 0; OB_SUCC(ret) && j < server_list.count(); ++j) {
        
          
          LOG_INFO("flush server cache", K(fc_arg), K(server_list.at(j)));
          if (OB_FAIL(call_server(server_list.at(j), fc_arg))) {
            LOG_WARN("fail to call tenant server",
                     "server addr", server_list.at(j));
          }
        }
      }
      server_list.reset();
    }
  } else { // flush all tenant
    //get all server list, server_mgr_.get_alive_servers
    if (OB_FAIL(get_all_servers(server_list))) {
      LOG_WARN("fail to get all servers", KR(ret));
    } else {
      fc_arg.is_all_tenant_ = true;
      
      
      fc_arg.cache_type_ = arg.cache_type_;
      fc_arg.ns_type_ = arg.ns_type_;
      for (int64_t j = 0; OB_SUCC(ret) && j < server_list.count(); ++j) {
        LOG_INFO("flush server cache", K(fc_arg), K(server_list.at(j)));
        if (OB_FAIL(call_server(server_list.at(j), fc_arg))) {
          LOG_WARN("fail to call tenant server",
                   "server addr", server_list.at(j));
        }
      }
    }
  }
  return ret;
}

int ObTenantServerAdminUtil::get_tenant_servers(common::ObIArray<ObAddr> &servers)
{
  int ret = OB_SUCCESS;
  servers.reset();
  if (OB_UNLIKELY(false)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret));
  } else if (OB_FAIL(servers.push_back(GCTX.self_addr()))) {
    LOG_WARN("fail to push back self addr to array", KR(ret));
  }

  return ret;
}

int ObTenantServerAdminUtil::get_all_servers(common::ObIArray<ObAddr> &servers)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(servers.push_back(GCTX.self_addr()))) {
    LOG_WARN("fail to get all servers", KR(ret));
  }
  return ret;
}

namespace {
int do_flush_cache_local(obcall::ObFlushCacheArg arg)
{
  int ret = OB_SUCCESS;
  switch (arg.cache_type_) {
    case CACHE_TYPE_LIB_CACHE: {
      if (arg.ns_type_ != ObLibCacheNameSpace::NS_INVALID) {
        ObLibCacheNameSpace ns = arg.ns_type_;
        if (arg.is_all_tenant_) { //flush all tenant cache
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
        } else {  // flush appointed tenant cache
          MOD_SCOPE {
            ObPlanCache* plan_cache = share::g_mp->plan_cache();
            ret = plan_cache->flush_lib_cache_by_ns(ns);
          }
        }
      } else {
        if (arg.is_all_tenant_) { //flush all tenant cache
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
        } else {  // flush appointed tenant cache
          MOD_SCOPE {
            ObPlanCache* plan_cache = share::g_mp->plan_cache();
            ret = plan_cache->flush_lib_cache();
          }
        }
      }
      break;
    }
    case CACHE_TYPE_PLAN: {
      if (arg.is_fine_grained_) { // fine-grained plan cache evict
        MOD_SCOPE {
          ObPlanCache* plan_cache = share::g_mp->plan_cache();
          if (arg.db_ids_.count() == 0) {
            ret = plan_cache->flush_plan_cache_by_sql_id(OB_INVALID_ID, arg.sql_id_);
          } else {
            for (uint64_t i=0; i<arg.db_ids_.count(); i++) {
              ret = plan_cache->flush_plan_cache_by_sql_id(arg.db_ids_.at(i), arg.sql_id_);
            }
          }
        }
      } else if (arg.is_all_tenant_) { //flush all tenant cache
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
      } else {  // flush appointed tenant cache
        MOD_SCOPE {
          ObPlanCache* plan_cache = share::g_mp->plan_cache();
          ret = plan_cache->flush_plan_cache();
        }
      }
      break;
    }
    case CACHE_TYPE_PL_OBJ: {
      if (arg.is_fine_grained_) { // fine-grained plan cache evict
        bool is_evict_by_schema_id = common::OB_INVALID_ID != arg.schema_id_;
        MOD_SCOPE {
          ObPlanCache* plan_cache = share::g_mp->plan_cache();
          if (arg.db_ids_.count() == 0) {
            if (is_evict_by_schema_id) {
              ret = plan_cache->flush_pl_cache_single_cache_obj<pl::ObGetPLKVEntryBySchemaIdOp, uint64_t>(OB_INVALID_ID, arg.schema_id_);
            } else {
              ret = plan_cache->flush_pl_cache_single_cache_obj<pl::ObGetPLKVEntryBySQLIDOp, ObString>(OB_INVALID_ID, arg.sql_id_);
            }
          } else {
            for (uint64_t i=0; i<arg.db_ids_.count(); i++) {
              if (is_evict_by_schema_id) {
                ret = plan_cache->flush_pl_cache_single_cache_obj<pl::ObGetPLKVEntryBySchemaIdOp, uint64_t>(arg.db_ids_.at(i), arg.schema_id_);
              } else if (OB_ISNULL(arg.sql_id_)) {
                ret = plan_cache->flush_pl_cache_single_cache_obj<pl::ObGetPLKVEntryByDbIdOp, uint64_t>(arg.db_ids_.at(i), arg.schema_id_);
              } else {
                ret = plan_cache->flush_pl_cache_single_cache_obj<pl::ObGetPLKVEntryBySQLIDOp, ObString>(arg.db_ids_.at(i), arg.sql_id_);
              }
            }
          }
        }
      } else if (arg.is_all_tenant_) {
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
        MOD_SCOPE {
          ObPlanCache* plan_cache = share::g_mp->plan_cache();
          ret = plan_cache->flush_pl_cache();
        }
      }
      break;
    }
    case CACHE_TYPE_PS_OBJ: {
      if (arg.is_all_tenant_) {
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
        MOD_SCOPE {
          ObPsCache* ps_cache = share::g_mp->ps_cache();
          if (ps_cache->is_inited()) {
            ret = ps_cache->cache_evict_all_ps();
          }
        }
      }
      break;
    }
    case CACHE_TYPE_SCHEMA: {
      // this option is only used for upgrade now
      if (OB_FAIL(common::ObKVGlobalCache::get_instance().erase_cache(OB_SCHEMA_CACHE_NAME))) {
        LOG_WARN("clear kv cache failed", K(ret));
      } else {
        LOG_INFO("success erase kvcache", K(ret), K(OB_SCHEMA_CACHE_NAME));
      }
      break;
    }
    case CACHE_TYPE_ALL:
    case CACHE_TYPE_COLUMN_STAT:
    case CACHE_TYPE_BLOCK_INDEX:
    case CACHE_TYPE_BLOCK:
    case CACHE_TYPE_ROW:
    case CACHE_TYPE_BLOOM_FILTER:
    case CACHE_TYPE_LOCATION:
    case CACHE_TYPE_CLOG:
    case CACHE_TYPE_ILOG: {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("cache type not supported flush", "type", arg.cache_type_, K(ret));
    } break;
    default: {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid cache type", "type", arg.cache_type_);
    }
  }
  return ret;
}
} // anonymous namespace

int ObAdminFlushCache::call_server(const common::ObAddr &server, const obcall::ObFlushCacheArg &arg)
{
  int ret = OB_SUCCESS;
  if (!ctx_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (!server.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid server", K(server), KR(ret));
  } else if (OB_FAIL(ex_rpc::sync_call([&]{ return do_flush_cache_local(arg); }))) {
    LOG_WARN("request server flush cache failed", KR(ret), K(server));
  }
  return ret;
}

int ObAdminSetTP::execute(const obcall::ObAdminSetTPArg &arg)
{
  LOG_INFO("start execute set_tp request", K(arg));
  int ret = OB_SUCCESS;
  if (!ctx_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), KR(ret));
  } else if (OB_FAIL(call_all(arg))) {
    LOG_WARN("execute report replica failed", KR(ret), K(arg));
  }
  LOG_INFO("end execute set_tp request", K(arg));
  return ret;
}

int ObAdminSetTP::call_server(const ObAddr &server)
{
  int ret = OB_SUCCESS;
  if (!ctx_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (!server.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid server", K(server), KR(ret));
  } else if (OB_FAIL(GCTX.ob_service_->set_tracepoint(arg_))) {
    LOG_WARN("request server report replica failed", KR(ret), K(server));
  }
  return ret;
}


} // end namespace rootserver
} // end namespace oceanbase
