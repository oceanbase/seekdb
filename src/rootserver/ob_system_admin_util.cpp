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
#include "logservice/leader_coordinator/table_accessor.h"
#include "rootserver/freeze/ob_major_freeze_helper.h"
#include "share/ob_cluster_event_history_table_operator.h"//CLUSTER_EVENT_INSTANCE
namespace oceanbase
{
using namespace common;
using namespace common::hash;
using namespace share;
using namespace share::schema;
using namespace obrpc;

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

int ObAdminClearMergeError::execute(const obrpc::ObAdminMergeArg &arg)
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
    param.transport_ = GCTX.net_frame_->get_req_transport();
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
    } else if (OB_FAIL(param.tenant_array_.assign(arg.tenant_ids_))) {
      LOG_WARN("fail to assign tenant_ids", KR(ret), K(arg));
    }
    if (FAILEDx(ObMajorFreezeHelper::clear_merge_error(param))) {
      LOG_WARN("fail to clear merge error", KR(ret), K(param));
    }
  }
  return ret;
}

int ObAdminMerge::execute(const obrpc::ObAdminMergeArg &arg)
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
        param.transport_ = GCTX.net_frame_->get_req_transport();
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
        } else if (OB_FAIL(param.tenant_array_.assign(arg.tenant_ids_))) {
          LOG_WARN("fail to assign tenant_ids", KR(ret), K(arg));
        }
        if (FAILEDx(ObMajorFreezeHelper::suspend_merge(param))) {
          LOG_WARN("fail to suspend merge", KR(ret), K(param));
        }
        break;
      }
      case ObAdminMergeArg::RESUME_MERGE: {
        ObTenantAdminMergeParam param;
        param.transport_ = GCTX.net_frame_->get_req_transport();
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
        } else if (OB_FAIL(param.tenant_array_.assign(arg.tenant_ids_))) {
          LOG_WARN("fail to assign tenant_ids", KR(ret), K(arg));
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

int ObAdminClearRoottable::execute(const obrpc::ObAdminClearRoottableArg &arg)
{
  int ret = OB_NOT_SUPPORTED;
  UNUSED(arg);
  return ret;
}

//FIXME: flush schemas of all tenants
int ObAdminRefreshSchema::execute(const obrpc::ObAdminRefreshSchemaArg &arg)
{
  LOG_INFO("execute refresh schema", K(arg));
  int ret = OB_SUCCESS;
  if (!ctx_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), KR(ret));
  } else if (OB_FAIL(ctx_.ddl_service_->refresh_schema(OB_SYS_TENANT_ID))) {
    LOG_WARN("refresh schema failed", KR(ret));
  } else {
    if (OB_FAIL(ctx_.schema_service_->get_tenant_schema_version(OB_SYS_TENANT_ID, schema_version_))) {
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
  } else if (OB_ISNULL(GCTX.srv_rpc_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("GCTX.srv_rpc_proxy_ is null", KR(ret));
  } else if (OB_FAIL(ObShareUtil::set_default_timeout_ctx(ctx, GCONF.rpc_timeout))) {
    LOG_WARN("fail to set timeout ctx", KR(ret));
  } else {
    ObSwitchSchemaArg arg;
    arg.schema_info_ = schema_info_;
    ObArray<int> return_code_array;
    ObSwitchSchemaProxy proxy(*GCTX.srv_rpc_proxy_, &ObSrvRpcProxy::switch_schema);
    int tmp_ret = OB_SUCCESS;
    const int64_t timeout_ts = ctx.get_timeout(0);
    if (OB_FAIL(proxy.call(server, timeout_ts, arg))) {
      LOG_WARN("notify switch schema failed", KR(ret), K(server), K_(schema_version), K_(schema_info));
    }
    if (OB_TMP_FAIL(proxy.wait_all(return_code_array))) {
      ret = OB_SUCC(ret) ? tmp_ret : ret;
      LOG_WARN("fail to wait all", KR(ret), KR(tmp_ret), K(server));
    } else if (OB_FAIL(ret)) {
    } else if (OB_FAIL(proxy.check_return_cnt(return_code_array.count()))) {
      LOG_WARN("fail to check return cnt", KR(ret), K(server), "return_cnt", return_code_array.count());
    } else if (OB_UNLIKELY(1 != return_code_array.count())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("return_code_array count shoud be 1", KR(ret), K(server), "return_cnt", return_code_array.count());
    } else {
      ret = return_code_array.at(0);
    }
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
  } else if (OB_FAIL(ctx_.rpc_proxy_->to(server).refresh_memory_stat())) {
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
  } else if (OB_FAIL(ctx_.rpc_proxy_->to(server).wash_memory_fragmentation())) {
    LOG_WARN("notify sync wash fragment failed", K(ret), K(server));
  }
  return ret;
}

int ObAdminSetConfig::verify_config(obrpc::ObAdminSetConfigArg &arg)
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
      bool is_default_table_organization_config = (0 == config_name.case_compare(DEFAULT_TABLE_ORGANIZATION));
      if (OB_SYS_TENANT_ID != item->exec_tenant_id_ || item->tenant_name_.size() > 0) {
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
            } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(OB_SYS_TENANT_ID, schema_guard))) {
              LOG_WARN("fail to get sys tenant schema guard", KR(ret));
            } else if (OB_SYS_TENANT_ID == item->exec_tenant_id_ &&
                      (0 == item->tenant_name_.str().case_compare(NAME_ALL) ||
                       0 == item->tenant_name_.str().case_compare(NAME_ALL_USER) ||
                       0 == item->tenant_name_.str().case_compare(NAME_ALL_META))) {
              common::ObArray<uint64_t> tenant_ids;
              if (OB_FAIL(schema_guard.get_tenant_ids(tenant_ids))) {
                LOG_WARN("get_tenant_ids failed", KR(ret));
              } else {
                using FUNC_TYPE = bool (*) (const uint64_t);
                FUNC_TYPE condition_func = nullptr;
                if (0 == item->tenant_name_.str().case_compare(NAME_ALL) ||
                    0 == item->tenant_name_.str().case_compare(NAME_ALL_USER)) {
                    condition_func = is_user_tenant;
                } else {
                  condition_func = is_meta_tenant;
                }
                if (OB_SUCC(ret) && (nullptr != condition_func)) {
                  const ObTenantSchema *tenant_schema = nullptr;
                  for (const uint64_t tenant_id: tenant_ids) {
                    if (OB_FAIL(schema_guard.get_tenant_info(tenant_id, tenant_schema))) {
                      LOG_WARN("fail to get tenant info", KR(ret), K(tenant_id));
                    } else if (OB_ISNULL(tenant_schema)) {
                      ret = OB_ERR_UNEXPECTED;
                      LOG_WARN("tenant_schema is null", KR(ret), K(tenant_id));
                    } else if (condition_func(tenant_id) &&
                              (is_default_table_organization_config ? !tenant_schema->is_oracle_tenant() : true) &&
                              OB_FAIL(item->tenant_ids_.push_back(tenant_id))) {
                      LOG_WARN("add tenant_id failed", K(tenant_id), KR(ret));
                      break;
                    }
                  } // for
                }
              }
            } else if (OB_SYS_TENANT_ID == item->exec_tenant_id_
                       && item->tenant_name_ == ObFixedLengthString<common::OB_MAX_TENANT_NAME_LENGTH + 1>("seed")) {
              uint64_t tenant_id = OB_PARAMETER_SEED_ID;
              if (OB_FAIL(item->tenant_ids_.push_back(tenant_id))) {
                LOG_WARN("add seed tenant_id failed", KR(ret));
                break;
              }
            } else {
              uint64_t tenant_id = OB_INVALID_TENANT_ID;
              const ObTenantSchema *tenant_schema = nullptr;
              if (OB_SYS_TENANT_ID != item->exec_tenant_id_) {
                tenant_id = item->exec_tenant_id_;
              } else {
                if (OB_FAIL(schema_guard.get_tenant_id(
                                   ObString(item->tenant_name_.ptr()), tenant_id))
                                   || OB_INVALID_ID == tenant_id) {
                  ret = OB_ERR_INVALID_TENANT_NAME;
                  LOG_WARN("get_tenant_id failed", KR(ret), "tenant", item->tenant_name_);
                }
              }
              if (OB_FAIL(ret)) {
              } else if (OB_FAIL(schema_guard.get_tenant_info(tenant_id, tenant_schema))) {
                LOG_WARN("fail to get tenant info", KR(ret), K(tenant_id));
              } else if (OB_ISNULL(tenant_schema)) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("tenant_schema is null", KR(ret), K(tenant_id));
              } else if ((is_default_table_organization_config ?
                          !tenant_schema->is_oracle_tenant()
                          : true) && OB_FAIL(item->tenant_ids_.push_back(tenant_id))) {
                LOG_WARN("add tenant_id failed", K(tenant_id), KR(ret));
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
int ObAdminSetConfig::update_config(obrpc::ObAdminSetConfigArg &arg)
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

int ObAdminSetConfig::update_sys_config_(const obrpc::ObAdminSetConfigItem &item)
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

int ObAdminSetConfig::execute(obrpc::ObAdminSetConfigArg &arg)
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
      if (OB_ISNULL(ctx_.root_service_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("error inner stat", KR(ret), K(ctx_.root_service_));
      } else if (OB_FAIL(ctx_.root_service_->set_config_post_hook(arg))) {
        LOG_WARN("fail to set config callback", KR(ret));
      } else {
        LOG_INFO("set config succ", K(arg));
      }
    }
  }
  return ret;
}

int ObAdminUpgradeCmd::execute(const Bool &upgrade)
{
  int ret = OB_SUCCESS;
  // set enable_upgrade_mode
  HEAP_VAR(ObAdminSetConfigItem, item) {
    obrpc::ObAdminSetConfigArg set_config_arg;
    set_config_arg.is_inner_ = true;
    const char *enable_upgrade_name = "enable_upgrade_mode";
    ObAdminSetConfig admin_set_config(ctx_);
    char min_server_version[OB_SERVER_VERSION_LENGTH] = {'\0'};
    uint64_t cluster_version = GET_MIN_CLUSTER_VERSION();

    if (OB_INVALID_INDEX == ObClusterVersion::print_version_str(
        min_server_version, OB_SERVER_VERSION_LENGTH, cluster_version)) {
       ret = OB_INVALID_ARGUMENT;
       LOG_WARN("fail to print version str", KR(ret), K(cluster_version));
    } else if (OB_FAIL(item.name_.assign(enable_upgrade_name))) {
      LOG_WARN("assign enable_upgrade_mode config name failed", KR(ret));
    } else if (OB_FAIL(item.value_.assign((upgrade ? "true" : "false")))) {
      LOG_WARN("assign enable_upgrade_mode config value failed", KR(ret));
    } else if (OB_FAIL(set_config_arg.items_.push_back(item))) {
      LOG_WARN("add enable_upgrade_mode config item failed", KR(ret));
    } else {
      const char *upgrade_stage_name = "_upgrade_stage";
      obrpc::ObUpgradeStage stage = upgrade ?
                                    obrpc::OB_UPGRADE_STAGE_PREUPGRADE :
                                    obrpc::OB_UPGRADE_STAGE_NONE;
      if (OB_FAIL(item.name_.assign(upgrade_stage_name))) {
        LOG_WARN("assign _upgrade_stage config name failed", KR(ret), K(upgrade));
      } else if (OB_FAIL(item.value_.assign(obrpc::get_upgrade_stage_str(stage)))) {
        LOG_WARN("assign _upgrade_stage config value failed", KR(ret), K(stage), K(upgrade));
      } else if (OB_FAIL(set_config_arg.items_.push_back(item))) {
        LOG_WARN("add _upgrade_stage config item failed", KR(ret), K(stage), K(upgrade));
      }
    }
    share::ObBuildVersion build_version;
    if (FAILEDx(admin_set_config.execute(set_config_arg))) {
      LOG_WARN("execute set config failed", KR(ret));
    } else if (OB_FAIL(observer::ObService::get_build_version(build_version))) {
      LOG_WARN("fail to get build version", KR(ret));
    } else {
      CLUSTER_EVENT_SYNC_ADD("UPGRADE",
                             upgrade ? "BEGIN_UPGRADE" : "END_UPGRADE",
                             "cluster_version", min_server_version,
                             "build_version", build_version.ptr());
      LOG_INFO("change upgrade parameters",
               "enable_upgrade_mode", upgrade,
               "in_major_version_upgrade_mode", GCONF.in_major_version_upgrade_mode());

    }
  }
  return ret;
}

int ObAdminRollingUpgradeCmd::execute(const obrpc::ObAdminRollingUpgradeArg &arg)
{
  int ret = OB_SUCCESS;
  uint64_t max_server_id = 0;
  HEAP_VAR(ObAdminSetConfigItem, item) {
    obrpc::ObAdminSetConfigArg set_config_arg;
    set_config_arg.is_inner_ = true;
    const char *upgrade_stage_name = "_upgrade_stage";
    ObAdminSetConfig admin_set_config(ctx_);
    char ori_min_server_version[OB_SERVER_VERSION_LENGTH] = {'\0'};
    char min_server_version[OB_SERVER_VERSION_LENGTH] = {'\0'};
    uint64_t ori_cluster_version = GET_MIN_CLUSTER_VERSION();

    if (!arg.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid arg", KR(ret), K(arg));
    } else if (OB_INVALID_INDEX == ObClusterVersion::print_version_str(
               ori_min_server_version, OB_SERVER_VERSION_LENGTH, ori_cluster_version)) {
       ret = OB_INVALID_ARGUMENT;
       LOG_WARN("fail to print version str", KR(ret), K(ori_cluster_version));
    } else if (OB_FAIL(item.name_.assign(upgrade_stage_name))) {
      LOG_WARN("assign _upgrade_stage config name failed", KR(ret), K(arg));
    } else if (OB_FAIL(item.value_.assign(obrpc::get_upgrade_stage_str(arg.stage_)))) {
      LOG_WARN("assign _upgrade_stage config value failed", KR(ret), K(arg));
    } else if (OB_FAIL(set_config_arg.items_.push_back(item))) {
      LOG_WARN("add _upgrade_stage config item failed", KR(ret), K(arg));
    } else if (obrpc::OB_UPGRADE_STAGE_POSTUPGRADE == arg.stage_) {
      // wait min_observer_version to report to inner table
      ObTimeoutCtx ctx;
      if (OB_FAIL(ObShareUtil::set_default_timeout_ctx(ctx, GCONF.rpc_timeout))) {
        LOG_WARN("fail to set default timeout", KR(ret));
      }

      // end rolling upgrade, should raise min_observer_version
      const char *min_obs_version_name = "min_observer_version";
      if (FAILEDx(item.name_.assign(min_obs_version_name))) {
        LOG_WARN("assign min_observer_version config name failed",
                 KR(ret), K(min_obs_version_name));
      } else if (OB_FAIL(item.value_.assign(min_server_version))) {
        LOG_WARN("assign min_observer_version config value failed",
                 KR(ret), K(min_server_version));
      } else if (OB_FAIL(set_config_arg.items_.push_back(item))) {
        LOG_WARN("add min_observer_version config item failed", KR(ret), K(item));
      }
    }
    if (FAILEDx(admin_set_config.execute(set_config_arg))) {
      LOG_WARN("execute set config failed", KR(ret));
    } else {
      share::ObBuildVersion build_version;
      if (OB_FAIL(observer::ObService::get_build_version(build_version))) {
        LOG_WARN("fail to get build version", KR(ret));
      } else if (obrpc::OB_UPGRADE_STAGE_POSTUPGRADE != arg.stage_) {
        CLUSTER_EVENT_SYNC_ADD("UPGRADE", "BEGIN_ROLLING_UPGRADE",
                               "cluster_version", ori_min_server_version,
                               "build_version", build_version.ptr());
      } else {
        CLUSTER_EVENT_SYNC_ADD("UPGRADE", "END_ROLLING_UPGRADE",
                               "cluster_version", min_server_version,
                               "ori_cluster_version", ori_min_server_version,
                               "build_version", build_version.ptr());
      }
      LOG_INFO("change upgrade parameters", KR(ret), "_upgrade_stage", arg.stage_);
    }
  }
  return ret;
}

DEFINE_ENUM_FUNC(ObInnerJob, inner_job, OB_INNER_JOB_DEF);

int ObAdminRefreshIOCalibration::execute(const obrpc::ObAdminRefreshIOCalibrationArg &arg)
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
    if (OB_SUCC(ret)) {
      ObMySQLTransaction trans;
      if (OB_FAIL(trans.start(ctx_.sql_proxy_, OB_SYS_TENANT_ID))) {
        LOG_WARN("start transaction failed", K(ret));
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < server_list.count(); ++i) {
          if (OB_FAIL(ObIOCalibration::get_instance().write_into_table(trans, server_list.at(i), io_ability))) {
            LOG_WARN("write io ability failed", K(ret), K(io_ability), K(server_list.at(i)));
          }
        }
        bool is_commit = OB_SUCCESS == ret;
        int tmp_ret = trans.end(is_commit);
        if (OB_UNLIKELY(OB_SUCCESS != tmp_ret)) {
          LOG_WARN("end transaction failed", K(tmp_ret), K(is_commit));
          ret = OB_SUCC(ret) ? tmp_ret : ret;
        }
      }
    }
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
        int tmp_ret = ctx_.rpc_proxy_->to(*server).refresh_io_calibration(refresh_arg);
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

int ObAdminFlushCache::execute(const obrpc::ObAdminFlushCacheArg &arg)
{
  int ret = OB_SUCCESS;
  int64_t tenant_num = arg.tenant_ids_.count();
  ObSEArray<ObAddr, 8> server_list;
  ObFlushCacheArg fc_arg;
  // fine-grained plan evict only will pass this way.
  // This because fine-grained plan evict must specify tenant
  // if tenant num is 0, flush all tenant, else, flush appointed tenant
  if (tenant_num != 0) { //flush appointed tenant
    for (int64_t i = 0; OB_SUCC(ret) && i < tenant_num; ++i) {
      //get tenant server list;
      if (OB_FAIL(get_tenant_servers(arg.tenant_ids_.at(i), server_list))) {
        LOG_WARN("fail to get tenant servers", "tenant_id", arg.tenant_ids_.at(i));
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
          fc_arg.tenant_id_ = arg.tenant_ids_.at(i);
          LOG_INFO("flush server cache", K(fc_arg), K(server_list.at(j)));
          if (OB_FAIL(call_server(server_list.at(j), fc_arg))) {
            LOG_WARN("fail to call tenant server",
                     "tenant_id", arg.tenant_ids_.at(i),
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
      fc_arg.tenant_id_ = common::OB_INVALID_TENANT_ID;
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

int ObTenantServerAdminUtil::get_tenant_servers(const uint64_t tenant_id, common::ObIArray<ObAddr> &servers)
{
  int ret = OB_SUCCESS;
  servers.reset();
  if (OB_UNLIKELY(OB_INVALID_TENANT_ID == tenant_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id));
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

int ObAdminFlushCache::call_server(const common::ObAddr &server, const obrpc::ObFlushCacheArg &arg)
{
  int ret = OB_SUCCESS;
  if (!ctx_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (!server.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid server", K(server), KR(ret));
  } else if (OB_FAIL(ctx_.rpc_proxy_->to(server).flush_cache(arg))) {
    LOG_WARN("request server flush cache failed", KR(ret), K(server));
  }
  return ret;
}

int ObAdminSetTP::execute(const obrpc::ObAdminSetTPArg &arg)
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
  } else if (OB_FAIL(ctx_.rpc_proxy_->to(server).set_tracepoint(arg_))) {
    LOG_WARN("request server report replica failed", KR(ret), K(server));
  }
  return ret;
}

int ObAdminSyncRewriteRules::execute(const obrpc::ObSyncRewriteRuleArg &arg)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObAddr, 8> server_list;
  if (OB_FAIL(get_tenant_servers(arg.tenant_id_, server_list))) {
    LOG_WARN("fail to get tenant servers", "tenant_id", arg.tenant_id_, KR(ret));
  } else {
    //call tenant servers;
    for (int64_t j = 0; OB_SUCC(ret) && j < server_list.count(); ++j) {
      if (OB_FAIL(call_server(server_list.at(j), arg))) {
        LOG_WARN("fail to call tenant server",
                 "tenant_id", arg.tenant_id_,
                 "server addr", server_list.at(j),
                 KR(ret));
      }
    }
  }
  server_list.reset();
  return ret;
}

int ObAdminSyncRewriteRules::call_server(const common::ObAddr &server,
                                         const obrpc::ObSyncRewriteRuleArg &arg)
{
  int ret = OB_SUCCESS;
  if (!ctx_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (!server.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid server", K(server), KR(ret));
  } else if (OB_ISNULL(ctx_.rpc_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret));
  } else if (OB_FAIL(ctx_.rpc_proxy_->to(server)
                                     .by(arg.tenant_id_)
                                     .as(arg.tenant_id_)
                                     .sync_rewrite_rules(arg))) {
    LOG_WARN("request server sync rewrite rules failed", KR(ret), K(server));
  }
  return ret;
}

} // end namespace rootserver
} // end namespace oceanbase
