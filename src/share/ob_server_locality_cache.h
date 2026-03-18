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

#ifndef OCEANBASE_SHARE_OB_SERVER_LOCALITY_CACHE_
#define OCEANBASE_SHARE_OB_SERVER_LOCALITY_CACHE_

#include "lib/net/ob_addr.h"
#include "lib/lock/ob_spin_rwlock.h"
#include "lib/container/ob_array.h"
#include "lib/hash/ob_linear_hash_map.h"
#include "common/ob_zone.h"
#include "common/ob_region.h"
#include "common/ob_zone_type.h"
#include "common/ob_idc.h"
#include "share/ob_server_status.h"

namespace oceanbase
{
namespace share
{
class ObServerLocality
{
public:
  ObServerLocality();
  virtual ~ObServerLocality();
  void reset();
  int assign(const ObServerLocality &other);
  int init(const char *svr_ip,
           const int32_t svr_port,
           const common::ObZone &zone,
           const common::ObZoneType zone_type,
           const common::ObIDC &idc,
           const common::ObRegion &region,
           bool is_active);
  const common::ObAddr &get_addr() const { return addr_; }
  const common::ObZone &get_zone() const { return zone_; }
  const common::ObZoneType &get_zone_type() const { return zone_type_; }
  const common::ObIDC &get_idc() const { return idc_; }
  const common::ObRegion &get_region() const { return region_; }
  bool is_init() const { return inited_; }
  bool is_active() const { return is_active_; }
  void set_start_service_time(int64_t start_service_time) { start_service_time_ = start_service_time; }
  void set_server_stop_time(int64_t stop_time) { server_stop_time_ = stop_time; }
  int set_server_status(const char *str);
  int64_t get_start_service_time() const { return start_service_time_; }
  int64_t get_server_stop_time() const { return server_stop_time_;  }
  ObServerStatus::DisplayStatus get_server_status() const { return server_status_; }
  TO_STRING_KV(K_(inited),
               K_(addr),
               K_(zone),
               K_(zone_type),
               K_(idc),
               K_(region),
               K_(is_active),
               K_(start_service_time),
               K_(server_stop_time),
               K_(server_status));
private:
  bool inited_;
  bool is_idle_;
  bool is_active_;
  common::ObAddr addr_;
  common::ObZone zone_;
  common::ObZoneType zone_type_;
  common::ObIDC idc_;
  common::ObRegion region_;
  int64_t start_service_time_;
  int64_t server_stop_time_;
  ObServerStatus::DisplayStatus server_status_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObServerLocality);
};

}
}
#endif /* OCEANBASE_SHARE_OB_SERVER_LOCALITY_CACHE_ */
