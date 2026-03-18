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

#define USING_LOG_PREFIX SHARE
#include "share/ob_server_locality_cache.h"

using namespace oceanbase::common;
namespace oceanbase
{
namespace share
{
ObServerLocality::ObServerLocality()
  : inited_(false),
    is_idle_(false),
    is_active_(false),
    addr_(),
    zone_(),
    zone_type_(ObZoneType::ZONE_TYPE_INVALID),
    idc_(),
    region_(),
    start_service_time_(0),
    server_stop_time_(0),
    server_status_(ObServerStatus::OB_DISPLAY_MAX)
{
}

ObServerLocality::~ObServerLocality()
{
}

void ObServerLocality::reset()
{
  inited_ = false;
  is_idle_ = false;
  is_active_ = false;
  addr_.reset();
  zone_.reset();
  zone_type_ = ObZoneType::ZONE_TYPE_INVALID;
  idc_.reset();
  region_.reset();
  start_service_time_ = 0;
  server_stop_time_ = 0;
  server_status_ = ObServerStatus::OB_DISPLAY_MAX;
}

int ObServerLocality::assign(const ObServerLocality &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(zone_.assign(other.zone_))) {
    LOG_WARN("fail to assign zone", K(ret), K(zone_), K(other.zone_));
  } else if (OB_FAIL(idc_.assign(other.idc_))) {
    LOG_WARN("fail to assign idc", K(ret), K(idc_), K(other.idc_));
  } else if (OB_FAIL(region_.assign(other.region_))) {
    LOG_WARN("fail to assign region", K(ret), K(region_), K(other.region_));
  } else {
    addr_ = other.addr_;
    zone_type_ = other.zone_type_;
    inited_ = other.inited_;
    is_idle_ = other.is_idle_;
    is_active_ = other.is_active_;
    zone_type_ = other.zone_type_;
    start_service_time_ = other.start_service_time_;
    server_stop_time_ = other.server_stop_time_;
    server_status_ = other.server_status_;
  }
  return ret;
}


int ObServerLocality::init(const char *svr_ip,
                           const int32_t svr_port,
                           const ObZone &zone,
                           const ObZoneType zone_type,
                           const ObIDC &idc,
                           const ObRegion &region,
                           bool is_active)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(inited_)) {
    ret = OB_INIT_TWICE;
    LOG_ERROR("init twice", K(ret));
  } else if (OB_UNLIKELY(!addr_.set_ip_addr(svr_ip, svr_port))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("fail to set addr", K(ret), K(svr_ip), K(svr_port));
  } else if (OB_FAIL(zone_.assign(zone))) {
    LOG_WARN("fail to assign zone", K(ret), K(zone_), K(zone));
  } else if (OB_FAIL(idc_.assign(idc))) {
    LOG_WARN("fail to assign idc", K(idc), K(idc_), K(ret));
  } else if (OB_FAIL(region_.assign(region))) {
    LOG_WARN("fail to assign region", K(ret), K(region_), K(region));
  } else {
    zone_type_ = zone_type;
    inited_ = true;
    is_active_ = is_active;
  }
  return ret;
}

int ObServerLocality::set_server_status(const char *svr_status)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObServerStatus::str2display_status(svr_status, server_status_))) {
    LOG_WARN("string to display status failed", K(ret), K(svr_status));
  } else if (server_status_ < 0 || server_status_ >= ObServerStatus::OB_DISPLAY_MAX) {
    ret = OB_INVALID_SERVER_STATUS;
    LOG_WARN("invalid display status", K(svr_status), K(ret));
  }
  return ret;
}

}/* ns share*/
}/* ns oceanbase */
