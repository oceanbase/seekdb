#include "observer/ob_ex_rpc.h"
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

#include "ob_root_minor_freeze.h"
#include "share/ob_server_struct.h"
#include "observer/ob_service.h" // for ObService

#include "share/location_cache/ob_location_service.h"

namespace oceanbase
{
using namespace common;
using namespace obcall;
using namespace share;
using namespace share::schema;

namespace rootserver
{
ObRootMinorFreeze::ObRootMinorFreeze()
    :inited_(false),
     stopped_(false)
{
}

ObRootMinorFreeze::~ObRootMinorFreeze()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(destroy())) {
  }
}

int ObRootMinorFreeze::init()
{
  int ret = OB_SUCCESS;
  if (inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else {
    stopped_ = false;
    inited_ = true;
  }

  return ret;
}

void ObRootMinorFreeze::start()
{
  ATOMIC_STORE(&stopped_, false);
}

void ObRootMinorFreeze::stop()
{
  ATOMIC_STORE(&stopped_, true);
}

int ObRootMinorFreeze::destroy()
{
  int ret = OB_SUCCESS;
  inited_ = false;
  return ret;
}

inline
int ObRootMinorFreeze::check_cancel() const
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (ATOMIC_LOAD(&stopped_)) {
    ret = OB_CANCELED;
    LOG_WARN("rs is stopped", K(ret));
  }
  return ret;
}

inline
bool ObRootMinorFreeze::is_server_alive(const ObAddr &server) const
{
  return true;
}

int ObRootMinorFreeze::try_minor_freeze(const obcall::ObRootMinorFreezeArg &arg) const
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObRootMinorFreeze not init", K(ret));
  } else {
    ParamsContainer params;
    if ((arg.ls_id_.is_valid() && arg.ls_id_.id() > 0) || arg.tablet_id_.is_valid()) {
      if (OB_FAIL(init_params_by_ls_or_tablet( arg.ls_id_, arg.tablet_id_, params))) {
      }
    } else if (arg.server_list_.count() == 0 && arg.zone_.size() > 0) {
      if (OB_FAIL(init_params_by_zone(arg.zone_, params))) {
      }
    } else if (arg.server_list_.count() > 0) {
      if (OB_FAIL(init_params_by_server(arg.server_list_, params))) {
      }
    } else {
      if (OB_FAIL(init_params_by_tenant(params))) {
      }
    }

    if (OB_SUCC(ret) && !params.is_empty()) {
      if (OB_FAIL(do_minor_freeze(params))) {
      }
    }
  }

  return ret;
}

int ObRootMinorFreeze::do_minor_freeze(const ParamsContainer &params) const
{
  int ret = OB_SUCCESS;
  int64_t failure_cnt = 0;
  LOG_INFO("do minor freeze", K(params));

  for (int64_t i = 0; OB_SUCC(ret) && i < params.get_params().count(); ++i) {
    const MinorFreezeParam &param = params.get_params().at(i);
    if (OB_FAIL(check_cancel())) {
    } else {
      obcall::Int64 result;
      int rpc_ret = GCTX.ob_service_->minor_freeze(param.arg, result);
      if (OB_SUCCESS != rpc_ret || OB_SUCCESS != static_cast<int>(result)) {
        LOG_WARN("minor freeze failed", KR(rpc_ret), "dest addr", param.server, "param", param.arg);
        failure_cnt++;
      }
    }
  }

  if (OB_FAIL(ret)) {
  } else if (0 != failure_cnt) {
    ret = OB_PARTIAL_FAILED;
    LOG_WARN("minor freeze partial failed", KR(ret), K(failure_cnt));
  }

  return ret;
}

int ObRootMinorFreeze::is_server_belongs_to_zone(const ObAddr &addr,
                                                 const ObZone &zone,
                                                 bool &server_in_zone) const
{
  int ret = OB_SUCCESS;
  server_in_zone = true;
  return ret;
}

int ObRootMinorFreeze::init_params_by_ls_or_tablet(share::ObLSID ls_id,
                                                   const common::ObTabletID &tablet_id,
                                                   ParamsContainer &params) const
{
  int ret = OB_SUCCESS;

  const int64_t expire_renew_time = INT64_MAX;
  share::ObLSLocation location;
  bool is_cache_hit = false;
  if (OB_UNLIKELY(OB_ISNULL(GCTX.location_service_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("location service ptr is null", KR(ret));
  } else if (tablet_id.is_valid() && !ls_id.is_valid()) {
    // get ls id by tablet_id
    if (tablet_id.is_ls_inner_tablet()) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("can not minor freeze inner tablet without specifying ls id", K(ls_id), K(tablet_id));
    } else if (OB_FAIL(GCTX.location_service_->get(tablet_id, expire_renew_time, is_cache_hit, ls_id))) {
    }
  } 
  
  if (OB_FAIL(ret)) {
  } else if (ls_id.is_valid()) {
    // get ls location by ls_id
    if (OB_FAIL(GCTX.location_service_->get(
            GCONF.cluster_id, ls_id, expire_renew_time, is_cache_hit, location))) {
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid ls_id or tablet_id", KR(ret), K(ls_id), K(tablet_id));
  }

  if (OB_FAIL(ret)) {
  } else {
    const ObIArray<ObLSReplicaLocation> &ls_locations = location.get_replica_locations();
    for (int i = 0; i < ls_locations.count() && OB_SUCC(ret); ++i) {
      const ObAddr &server = ls_locations.at(i).get_server();
      if (is_server_alive(server)) {
        if (OB_FAIL(params.push_back_param(server, ls_id, tablet_id))) {
        }
      } else {
        int tmp_ret = OB_SERVER_NOT_ACTIVE;
        LOG_WARN("server not alive or invalid", "server", server, K(tmp_ret), K(ls_id), K(tablet_id));
      }
    }
  }

  return ret;
}

int ObRootMinorFreeze::init_params_by_tenant(ParamsContainer &params) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(params.push_back_param(GCTX.self_addr()))) {
  }
  return ret;
}

int ObRootMinorFreeze::init_params_by_zone(const ObZone &zone,
                                           ParamsContainer &params) const
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(0 == zone.size())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(params.push_back_param(GCTX.self_addr()))) {
  }
  return ret;
}

int ObRootMinorFreeze::init_params_by_server(const ObIArray<ObAddr> &server_list,
                                             ParamsContainer &params) const
{
  int ret = OB_SUCCESS;
  if (server_list.count() > 0) {
    for (int i = 0; i < server_list.count() && OB_SUCC(ret); ++i) {
      if (is_server_alive(server_list.at(i))) {
        if (OB_FAIL(params.push_back_param(server_list.at(i)))) {
        }
      } else {
        ret = OB_SERVER_NOT_ACTIVE;
        LOG_WARN("server not alive or invalid", "server", server_list.at(i), K(ret));
      }
    }
  } else if (OB_FAIL(params.push_back_param(GCTX.self_addr()))) {
  }

  return ret;
}

int ObRootMinorFreeze::ParamsContainer::push_back_param(const common::ObAddr &server,
                                                        share::ObLSID ls_id,
                                                        const common::ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;

  MinorFreezeParam param;
  param.server = server;
  param.arg.ls_id_ = ls_id;
  param.arg.tablet_id_ = tablet_id;

  if (OB_FAIL(params_.push_back(param))) {
  }

  return ret;
}

} // namespace rootserver
} // namespace oceanbase
