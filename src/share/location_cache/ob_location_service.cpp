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

#define USING_LOG_PREFIX SHARE_LOCATION

#include "share/location_cache/ob_location_service.h"

namespace oceanbase
{
using namespace common;

namespace share
{
ObLocationService::ObLocationService()
    : inited_(false),
      stopped_(false)
{
}

int ObLocationService::get(
    const int64_t cluster_id,
    const uint64_t tenant_id,
    const ObLSID &ls_id,
    const int64_t expire_renew_time,
    bool &is_cache_hit,
    ObLSLocation &location)
{
  is_cache_hit = true;
  location.reset();
  return nonblock_get(cluster_id, tenant_id, ls_id, location);
}

int ObLocationService::get_leader(
    const int64_t cluster_id,
    const uint64_t tenant_id,
    const ObLSID &ls_id,
    const bool force_renew,
    common::ObAddr &leader)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else {
    leader = GCTX.self_addr();
  }
  return ret;
}

int ObLocationService::get_leader_with_retry_until_timeout(
    const int64_t cluster_id,
    const uint64_t tenant_id,
    const ObLSID &ls_id,
    common::ObAddr &leader,
    const int64_t abs_retry_timeout,
    const int64_t retry_interval)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret), KCSTRING(lbt()));
  } else {
    leader = GCTX.self_addr();
  }
  return ret;
}

int ObLocationService::nonblock_get(
    const int64_t cluster_id,
    const uint64_t tenant_id,
    const ObLSID &ls_id,
    ObLSLocation &location)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_ISNULL(GCTX.config_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(GCTX.config_));
  } else if (OB_FAIL(location.init(cluster_id, tenant_id, ls_id, ObTimeUtility::current_time()))) {
    LOG_WARN("location init error", KR(ret), K(cluster_id), K(tenant_id), K(ls_id));
  } else {
    ObLSReplicaLocation replica_location;
    common::ObReplicaProperty property;
    if(OB_FAIL(replica_location.init(
                   GCTX.self_addr(), LEADER/*role*/, GCTX.config_->mysql_port/*sql_port*/,
                   REPLICA_TYPE_FULL/*replica_type*/, property/*not_used*/,
                   ObLSRestoreStatus(ObLSRestoreStatus::Status::NONE)/*restore_status*/, 1/*proposal_id*/))) {
      LOG_WARN("fail to init", KR(ret));
    } else if (OB_FAIL(location.add_replica_location(replica_location))) {
      LOG_WARN("fail to add replica locaiton", KR(ret), K(replica_location));
    }
  }
  return ret;
}

int ObLocationService::nonblock_get_leader(
    const int64_t cluster_id,
    const uint64_t tenant_id,
    const ObLSID &ls_id,
    common::ObAddr &leader)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else {
    leader = GCTX.self_addr();
  }
  return ret;
}

int ObLocationService::get(
    const uint64_t tenant_id,
    const ObTabletID &tablet_id,
    const int64_t expire_renew_time,
    bool &is_cache_hit,
    ObLSID &ls_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else {
    is_cache_hit = true;
    ls_id = ObLSID::SYS_LS_ID;
  }
  return ret;
}

int ObLocationService::nonblock_get(
    const uint64_t tenant_id,
    const ObTabletID &tablet_id,
    ObLSID &ls_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else {
    ls_id = ObLSID::SYS_LS_ID;
  }
  return ret;
}

int ObLocationService::vtable_get(
    const uint64_t tenant_id,
    const uint64_t table_id,
    const int64_t expire_renew_time,
    bool &is_cache_hit,
    ObIArray<common::ObAddr> &locations)
{
  int ret = OB_SUCCESS;
  locations.reset();
  is_cache_hit = true;
  if (OB_FAIL(locations.push_back(GCTX.self_addr()))) {
    LOG_WARN("fail to get location for virtual table", KR(ret));
  }
  return ret;
}

int ObLocationService::external_table_get(
    const uint64_t tenant_id,
    const uint64_t table_id,
    ObIArray<ObAddr> &locations)
{
  UNUSED(table_id);
  int ret = OB_SUCCESS;
  bool is_cache_hit = false;
  //using the locations from any distributed virtual table
  ObSEArray<ObAddr, 16> all_active_locations;
  if (OB_FAIL(vtable_get(tenant_id, OB_ALL_VIRTUAL_PROCESSLIST_TID, 0, is_cache_hit, all_active_locations))) {
    LOG_WARN("failed to get active server", K(ret), K(tenant_id));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < all_active_locations.count(); ++i) {
      if (OB_FAIL(locations.push_back(all_active_locations.at(i)))) {
        LOG_WARN("failed to push back", K(ret), K(i), K(tenant_id));
      }
    }
    if (OB_FAIL(ret)) {
      // do nothing
    } else if (0 == locations.count() && OB_FAIL(locations.assign(all_active_locations))) {
      LOG_WARN("failed to assign locations", K(ret));
    }
    LOG_TRACE("locations for external table", K(locations), K(ret));
  }
  return ret;
}


int ObLocationService::init(
    schema::ObMultiVersionSchemaService &schema_service,
    common::ObMySQLProxy &sql_proxy,
    obrpc::ObSrvRpcProxy &srv_rpc_proxy)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("location service init twice", KR(ret));
  } else {
    inited_ = true;
  }
  return ret;
}

int ObLocationService::start()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("location service not init", KR(ret));
  }
  return ret;
}

void ObLocationService::stop()
{
}

void ObLocationService::wait()
{
}

int ObLocationService::destroy()
{
  int ret = OB_SUCCESS;
  stopped_ = true;
  inited_ = false;
  return ret;
}

ERRSIM_POINT_DEF(EN_CHECK_LS_EXIST_WITH_TENANT_NOT_NORMAL);

int ObLocationService::check_ls_exist(
    const uint64_t tenant_id,
    const ObLSID &ls_id,
    ObLSExistState &state)
{
  int ret = OB_SUCCESS;
  state.reset();
  ObSqlString sql;
  if (OB_UNLIKELY(!ls_id.is_valid_with_tenant(tenant_id))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(tenant_id), K(ls_id));
  } else if (OB_ISNULL(GCTX.schema_service_) || OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("GCTX has null ptr", KR(ret), KP(GCTX.schema_service_), KP(GCTX.sql_proxy_));
  } else {
    schema::ObSchemaGetterGuard schema_guard;
    const ObSimpleTenantSchema *tenant_schema = NULL;
    if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(OB_SYS_TENANT_ID, schema_guard))) {
      LOG_WARN("fail to get tenant schema guard", KR(ret));
    } else if (OB_FAIL(schema_guard.get_tenant_info(tenant_id, tenant_schema))) {
      LOG_WARN("fail to get tenant schema", KR(ret), K(tenant_id));
    } else if (OB_ISNULL(tenant_schema)) {
      ret = OB_TENANT_NOT_EXIST;
      LOG_WARN("tenant does not exist", KR(ret), K(tenant_id));
    } else if ((is_sys_tenant(tenant_id) || is_meta_tenant(tenant_id))
        && (tenant_schema->is_normal() || tenant_schema->is_dropping())) {
      // sys and meta tenants only have sys ls. If tenant is in normal or dropping status, sys ls exists.
      state.set_existing();
    }
  } // release schema_guard as soon as possible

  // errsim for test
  if (EN_CHECK_LS_EXIST_WITH_TENANT_NOT_NORMAL) {
    state.reset();
  }
  return ret;
}

} // end namespace share
} // end namespace oceanbase
