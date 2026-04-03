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
#define USING_LOG_PREFIX OBLOG
#include "ob_log_route_service.h"
#include "lib/thread/thread_mgr.h"  // MTL
#include "share/ob_thread_mgr.h"    // TG*
#include "share/ob_server_struct.h"  // GCTX
#include "share/config/ob_server_config.h"  // GCONF
#include "share/backup/ob_log_restore_struct.h"  // ObRestoreSourceServiceAttr
#include "lib/string/ob_sql_string.h"  // ObSqlString
#include "ob_log_route_struct.h"    // ObLSRouterKey, ObLSRouterValue, LSSvrList

using namespace oceanbase::share;
using namespace oceanbase::common;

namespace oceanbase
{
namespace logservice
{
ObLogRouteService::ObLogRouteService() :
    is_inited_(false),
    cluster_id_(OB_INVALID_CLUSTER_ID),
    source_tenant_id_(OB_INVALID_TENANT_ID),
    self_tenant_id_(OB_INVALID_TENANT_ID),
    is_stopped_(true),
    ls_route_timer_task_(*this),
    next_server_called_(false)
{
}

ObLogRouteService::~ObLogRouteService()
{
  destroy();
}

#ifdef ERRSIM
ERRSIM_POINT_DEF(LOG_ROUTE_TIMER_INIT_FAIL);
ERRSIM_POINT_DEF(LOG_ROUTE_HANDLER_INIT_FAIL);
ERRSIM_POINT_DEF(LOG_ROUTE_HANDLER_START_FAIL);
#endif
int ObLogRouteService::init(ObISQLClient *proxy,
    const common::ObRegion &prefer_region,
    const int64_t cluster_id,
    const bool is_across_cluster,
    void *err_handler,
    const char *external_server_blacklist,
    const int64_t background_refresh_time_sec,
    const int64_t all_server_cache_update_interval_sec,
    const int64_t all_zone_cache_update_interval_sec,
    const int64_t blacklist_survival_time_sec,
    const int64_t blacklist_survival_time_upper_limit_min,
    const int64_t blacklist_survival_time_penalty_period_min,
    const int64_t blacklist_history_overdue_time_min,
    const int64_t blacklist_history_clear_interval_min,
    const bool is_tenant_mode,
    const uint64_t source_tenant_id,
    const uint64_t self_tenant_id)
{
  int ret = OB_SUCCESS;
  UNUSED(proxy);
  UNUSED(prefer_region);
  UNUSED(is_across_cluster);
  UNUSED(err_handler);
  UNUSED(external_server_blacklist);
  UNUSED(background_refresh_time_sec);
  UNUSED(all_server_cache_update_interval_sec);
  UNUSED(all_zone_cache_update_interval_sec);
  UNUSED(blacklist_survival_time_sec);
  UNUSED(blacklist_survival_time_upper_limit_min);
  UNUSED(blacklist_survival_time_penalty_period_min);
  UNUSED(blacklist_history_overdue_time_min);
  UNUSED(blacklist_history_clear_interval_min);
  UNUSED(is_tenant_mode);

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObLogRouteService has been inited twice", KR(ret));
  } else if (OB_UNLIKELY(OB_INVALID_CLUSTER_ID == cluster_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid cluster_id", KR(ret), K(cluster_id));
  } else {
    cluster_id_ = cluster_id;
    source_tenant_id_ = source_tenant_id;
    self_tenant_id_ = self_tenant_id;
    is_stopped_ = true;
    is_inited_ = true;
    LOG_INFO("ObLogRouteService init succ (simplified for single machine)", K(cluster_id));
  }

  return ret;
}

int ObLogRouteService::start()
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLogRouteService has not been inited", KR(ret));
  } else {
    is_stopped_ = false;
    LOG_INFO("ObLogRouteService start succ (simplified for single machine)");
  }

  return ret;
}

void ObLogRouteService::stop()
{
  LOG_INFO("ObLogRouteService stop begin");
  is_stopped_ = true;
  LOG_INFO("ObLogRouteService stop finish");
}

void ObLogRouteService::wait()
{
  LOG_INFO("ObLogRouteService wait begin");
  LOG_INFO("ObLogRouteService wait finish");
}

void ObLogRouteService::destroy()
{
  LOG_INFO("ObLogRouteService destroy begin");
  cluster_id_ = OB_INVALID_CLUSTER_ID;
  self_tenant_id_ = OB_INVALID_TENANT_ID;
  source_tenant_id_ = OB_INVALID_TENANT_ID;
  is_inited_ = false;
  LOG_INFO("ObLogRouteService destroy finish");
}

void ObLogRouteService::free_mem_()
{
  // For single machine: no memory to free
}

void ObLogRouteService::handle(void *task)
{
  UNUSED(task);
  // For single machine: no async task to handle
  LOG_TRACE("handle ignored for single machine");
}

int ObLogRouteService::update_background_refresh_time(const int64_t background_refresh_time_sec)
{
  UNUSED(background_refresh_time_sec);
  // For single machine: no-op
  return OB_SUCCESS;
}

int ObLogRouteService::get_background_refresh_time(int64_t &background_refresh_time_sec)
{
  if (IS_NOT_INIT) {
    return OB_NOT_INIT;
  }
  background_refresh_time_sec = 0;
  return OB_SUCCESS;
}

int ObLogRouteService::update_preferred_upstream_log_region(const common::ObRegion &prefer_region)
{
  UNUSED(prefer_region);
  // For single machine: no-op
  return OB_SUCCESS;
}

int ObLogRouteService::get_preferred_upstream_log_region(common::ObRegion &prefer_region)
{
  if (IS_NOT_INIT) {
    return OB_NOT_INIT;
  }
  prefer_region.reset();
  return OB_SUCCESS;
}

int ObLogRouteService::update_cache_update_interval(const int64_t all_server_cache_update_interval_sec,
    const int64_t all_zone_cache_update_interval_sec)
{
  UNUSED(all_server_cache_update_interval_sec);
  UNUSED(all_zone_cache_update_interval_sec);
  // For single machine: no-op
  return OB_SUCCESS;
}

int ObLogRouteService::get_cache_update_interval(int64_t &all_server_cache_update_interval_sec,
    int64_t &all_zone_cache_update_interval_sec)
{
  if (IS_NOT_INIT) {
    return OB_NOT_INIT;
  }
  all_server_cache_update_interval_sec = 0;
  all_zone_cache_update_interval_sec = 0;
  return OB_SUCCESS;
}

int ObLogRouteService::update_blacklist_parameter(
    const int64_t blacklist_survival_time_sec,
    const int64_t blacklist_survival_time_upper_limit_min,
    const int64_t blacklist_survival_time_penalty_period_min,
    const int64_t blacklist_history_overdue_time_min,
    const int64_t blacklist_history_clear_interval_min)
{
  UNUSED(blacklist_survival_time_sec);
  UNUSED(blacklist_survival_time_upper_limit_min);
  UNUSED(blacklist_survival_time_penalty_period_min);
  UNUSED(blacklist_history_overdue_time_min);
  UNUSED(blacklist_history_clear_interval_min);
  // For single machine: no-op
  return OB_SUCCESS;
}

int ObLogRouteService::get_blacklist_parameter(
    int64_t &blacklist_survival_time_sec,
    int64_t &blacklist_survival_time_upper_limit_min,
    int64_t &blacklist_survival_time_penalty_period_min,
    int64_t &blacklist_history_overdue_time_min,
    int64_t &blacklist_history_clear_interval_min)
{
  if (IS_NOT_INIT) {
    return OB_NOT_INIT;
  }
  blacklist_survival_time_sec = 0;
  blacklist_survival_time_upper_limit_min = 0;
  blacklist_survival_time_penalty_period_min = 0;
  blacklist_history_overdue_time_min = 0;
  blacklist_history_clear_interval_min = 0;
  return OB_SUCCESS;
}

int ObLogRouteService::registered(
    const uint64_t tenant_id,
    const share::ObLSID &ls_id)
{
  int ret = OB_SUCCESS;
  UNUSED(tenant_id);
  UNUSED(ls_id);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_ERROR("ObLogRouteService has not been inited", KR(ret));
  } else {
    // For single machine, single tenant, single LS: no need to register async task
    LOG_TRACE("registered ignored for single machine");
  }

  return ret;
}

int ObLogRouteService::remove(
    const uint64_t tenant_id,
    const share::ObLSID &ls_id)
{
  int ret = OB_SUCCESS;
  UNUSED(tenant_id);
  UNUSED(ls_id);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_ERROR("ObLogRouteService has not been inited", KR(ret));
  } else {
    // For single machine, single tenant, single LS: no need to remove
    LOG_TRACE("remove ignored for single machine");
  }

  return ret;
}

int ObLogRouteService::get_all_ls(
    const uint64_t tenant_id,
    ObIArray<share::ObLSID> &ls_ids)
{
  int ret = OB_SUCCESS;
  UNUSED(tenant_id);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_ERROR("ObLogRouteService has not been inited", KR(ret));
  } else {
    // For single machine, single tenant, single LS: hardcode to return sys LS
    ls_ids.reset();
    if (OB_FAIL(ls_ids.push_back(share::ObLSID(share::SYS_LS)))) {
      LOG_WARN("push_back SYS_LS failed", KR(ret));
    } else {
      LOG_TRACE("get_all_ls return SYS_LS");
    }
  }

  return ret;
}

int ObLogRouteService::next_server(
    const uint64_t tenant_id,
    const share::ObLSID &ls_id,
    const palf::LSN &next_lsn,
    common::ObAddr &svr)
{
  int ret = OB_SUCCESS;
  UNUSED(tenant_id);
  UNUSED(ls_id);
  UNUSED(next_lsn);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_ERROR("ObLogRouteService has not been inited", KR(ret));
  } else {
    // Read restore source from config parameter
    common::ObAddr restore_source;
    if (OB_FAIL(get_restore_source_addr_(restore_source))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        ret = OB_ITER_END;
        LOG_WARN("restore_source is not configured, return OB_ITER_END", KR(ret));
      }
    } else if (next_server_called_) {
      // Already returned once, return OB_ITER_END to indicate iteration complete
      ret = OB_ITER_END;
      next_server_called_ = false; // Reset for next iteration round
      LOG_TRACE("next_server return OB_ITER_END (iteration complete)");
    } else {
      svr = restore_source;
      next_server_called_ = true;
      LOG_TRACE("next_server return restore_source", K(svr));
    }
  }

  return ret;
}

int ObLogRouteService::get_restore_source_addr_(common::ObAddr &addr) const
{
  int ret = OB_SUCCESS;
  const common::ObString config_value = GCONF.log_restore_source.str();

  if (config_value.empty()) {
    ret = OB_ENTRY_NOT_EXIST;
  } else {
    share::ObRestoreSourceServiceAttr service_attr;
    common::ObSqlString config_str;
    if (OB_FAIL(config_str.assign(config_value))) {
      LOG_WARN("failed to assign config value", K(ret), K(config_value));
    } else if (OB_FAIL(service_attr.parse_service_attr_from_str(config_str))) {
      LOG_WARN("failed to parse service attr", K(ret), K(config_str));
    } else if (service_attr.addr_.empty()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("parsed service_attr has no address", K(ret), K(service_attr));
    } else {
      addr = service_attr.addr_.at(0);
    }
  }
  return ret;
}

int ObLogRouteService::get_server_array_for_locate_start_lsn(
    const uint64_t tenant_id,
    const share::ObLSID &ls_id,
    ObIArray<common::ObAddr> &svr_array)
{
  int ret = OB_SUCCESS;
  UNUSED(tenant_id);
  UNUSED(ls_id);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_ERROR("ObLogRouteService has not been inited", KR(ret));
  } else {
    svr_array.reset();
    common::ObAddr restore_source;
    if (OB_FAIL(get_restore_source_addr_(restore_source))) {
      LOG_WARN("restore_source is not configured", KR(ret));
    } else if (OB_FAIL(svr_array.push_back(restore_source))) {
      LOG_WARN("push_back restore_source failed", KR(ret), K(restore_source));
    } else {
      LOG_TRACE("get_server_array_for_locate_start_lsn return restore_source", K(restore_source));
    }
  }

  return ret;
}

int ObLogRouteService::get_leader(
    const uint64_t tenant_id,
    const share::ObLSID &ls_id,
    common::ObAddr &leader)
{
  int ret = OB_SUCCESS;
  UNUSED(tenant_id);
  UNUSED(ls_id);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_ERROR("ObLogRouteService has not been inited", KR(ret));
  } else {
    if (OB_FAIL(get_restore_source_addr_(leader))) {
      ret = OB_NOT_MASTER;
      LOG_WARN("restore_source is not configured", KR(ret));
    } else {
      LOG_TRACE("get_leader return restore_source", K(leader));
    }
  }

  return ret;
}

bool ObLogRouteService::need_switch_server(
    const uint64_t tenant_id,
    const share::ObLSID &ls_id,
    const palf::LSN &next_lsn,
    const common::ObAddr &cur_svr)
{
  UNUSED(tenant_id);
  UNUSED(ls_id);
  UNUSED(next_lsn);

  // For single machine, single tenant, single LS: never need to switch server
  // Only return true if current server is not restore_source
  common::ObAddr restore_source;
  bool bool_ret = false;
  if (OB_SUCCESS == get_restore_source_addr_(restore_source)) {
    bool_ret = (restore_source.is_valid() && cur_svr != restore_source);
  }
  LOG_TRACE("need_switch_server", K(bool_ret), K(cur_svr), K(restore_source));
  return bool_ret;
}

int ObLogRouteService::get_server_count(
    const uint64_t tenant_id,
    const share::ObLSID &ls_id,
    int64_t &avail_svr_count) const
{
  int ret = OB_SUCCESS;
  UNUSED(tenant_id);
  UNUSED(ls_id);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_ERROR("ObLogRouteService has not been inited", KR(ret));
  } else {
    common::ObAddr restore_source;
    if (OB_SUCCESS == get_restore_source_addr_(restore_source)) {
      avail_svr_count = 1;
    } else {
      avail_svr_count = 0;
    }
    LOG_TRACE("get_server_count return", K(avail_svr_count));
  }

  return ret;
}

int ObLogRouteService::add_into_blacklist(
    const uint64_t tenant_id,
    const share::ObLSID &ls_id,
    const common::ObAddr &svr,
    const int64_t svr_service_time,
    int64_t &survival_time)
{
  int ret = OB_SUCCESS;
  UNUSED(tenant_id);
  UNUSED(ls_id);
  UNUSED(svr);
  UNUSED(svr_service_time);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_ERROR("ObLogRouteService has not been inited", KR(ret));
  } else {
    // For single machine, single tenant, single LS: blacklist is not needed
    // Just return success with survival_time = 0
    survival_time = 0;
    LOG_TRACE("add_into_blacklist ignored for single machine", K(svr));
  }

  return ret;
}

int ObLogRouteService::set_external_svr_blacklist(const char *server_blacklist)
{
  UNUSED(server_blacklist);
  if (IS_NOT_INIT) {
    return OB_NOT_INIT;
  }
  // For single machine: no-op
  return OB_SUCCESS;
}

int ObLogRouteService::async_server_query_req(
    const uint64_t tenant_id,
    const share::ObLSID &ls_id)
{
  int ret = OB_SUCCESS;
  UNUSED(tenant_id);
  UNUSED(ls_id);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_ERROR("ObLogRouteService has not been inited", KR(ret));
  } else {
    // For single machine, single tenant, single LS: no need to query server
    LOG_TRACE("async_server_query_req ignored for single machine");
  }

  return ret;
}

int ObLogRouteService::get_ls_svr_list_(const ObLSRouterKey &router_key,
    LSSvrList &svr_list)
{
  UNUSED(router_key);
  UNUSED(svr_list);
  // For single machine: not used
  return OB_SUCCESS;
}

int ObLogRouteService::query_ls_log_info_and_update_(const ObLSRouterKey &router_key,
    LSSvrList &svr_list)
{
  UNUSED(router_key);
  UNUSED(svr_list);
  // For single machine: not used
  return OB_SUCCESS;
}

int ObLogRouteService::query_units_info_and_update_(const ObLSRouterKey &router_key,
    LSSvrList &svr_list)
{
  UNUSED(router_key);
  UNUSED(svr_list);
  // For single machine: not used
  return OB_SUCCESS;
}

int ObLogRouteService::get_ls_router_value_(
    const ObLSRouterKey &router_key,
    ObLSRouterValue *&router_value)
{
  UNUSED(router_key);
  router_value = nullptr;
  // For single machine: not used
  return OB_SUCCESS;
}

int ObLogRouteService::handle_when_ls_route_info_not_exist_(
    const ObLSRouterKey &router_key,
    ObLSRouterValue *&router_value)
{
  UNUSED(router_key);
  router_value = nullptr;
  // For single machine: not used
  return OB_SUCCESS;
}

// For single machine: these functors are not used, removed

int ObLogRouteService::update_all_ls_server_list_()
{
  // For single machine: not used
  return OB_SUCCESS;
}

int ObLogRouteService::update_server_list_(
    const ObLSRouterKey &router_key,
    ObLSRouterValue &router_value)
{
  UNUSED(router_key);
  UNUSED(router_value);
  // For single machine: not used
  return OB_SUCCESS;
}

int ObLogRouteService::query_units_info_and_update_(
    const ObLSRouterKey &router_key,
    ObLSRouterValue &router_value)
{
  UNUSED(router_key);
  UNUSED(router_value);
  // For single machine: not used
  return OB_SUCCESS;
}

int ObLogRouteService::update_all_server_and_zone_cache_()
{
  // For single machine: not used
  return OB_SUCCESS;
}

ObLogRouteService::ObLSRouteTimerTask::ObLSRouteTimerTask(ObLogRouteService &log_route_service) :
    is_inited_(false),
    log_route_service_(log_route_service)
{}

int ObLogRouteService::ObLSRouteTimerTask::init(int tg_id)
{
  int ret = OB_SUCCESS;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_ERROR("ObLSRouteTimerTask has already been inited", KR(ret), K(tg_id));
  } else {
    is_inited_ = true;
    LOG_INFO("ls_route_timer_task inited", K(tg_id));
  }

  return ret;
}

void ObLogRouteService::ObLSRouteTimerTask::destroy()
{
  is_inited_ = false;
}

void ObLogRouteService::ObLSRouteTimerTask::runTimerTask()
{
  // For single machine: no timer task needed
  LOG_TRACE("ls_route_timer_task ignored for single machine");
}

} // namespace logservice
} // namespace oceanbase
