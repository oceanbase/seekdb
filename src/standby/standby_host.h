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

#ifndef OCEANBASE_STANDBY_STANDBY_HOST_H_
#define OCEANBASE_STANDBY_STANDBY_HOST_H_

#include <stdint.h>
#include "lib/allocator/ob_allocator.h"
#include "lib/net/ob_addr.h"
#include "lib/string/ob_string.h"
#include "share/ob_server_role.h"

namespace oceanbase
{
namespace common
{
class ObInOutBandwidthThrottle;
class ObConfigManager;
}
namespace standby
{

struct StandbyConfig final
{
  StandbyConfig()
    : self_addr_(),
      promotion_node_id_(),
      rpc_port_(0),
      embedded_mode_(false),
      rpc_service_enabled_(false),
      rpc_tls_enabled_(false),
      io_timeout_ms_(0),
      operation_timeout_us_(0),
      boot_role_(share::ObServerRole::INVALID_ROLE),
      config_manager_(nullptr),
      bandwidth_throttle_(nullptr),
      errsim_migration_tablet_id_(0),
      errsim_test_tablet_id_(0)
  {}

  bool is_valid() const
  {
    return self_addr_.is_valid()
        && promotion_node_id_.is_valid()
        && (embedded_mode_ || rpc_port_ > 0)
        && io_timeout_ms_ > 0
        && operation_timeout_us_ > 0
        && share::ObServerRole::INVALID_ROLE != boot_role_
        && nullptr != config_manager_
        && nullptr != bandwidth_throttle_;
  }

  common::ObAddr self_addr_;
  // Opaque process identity used only for promotion-path cycle detection.
  // It is deliberately independent of the network routing address.
  common::ObAddr promotion_node_id_;
  int32_t rpc_port_;
  bool embedded_mode_;
  bool rpc_service_enabled_;
  bool rpc_tls_enabled_;
  int64_t io_timeout_ms_;
  int64_t operation_timeout_us_;
  share::ObServerRole::Role boot_role_;
  common::ObConfigManager *config_manager_;
  common::ObInOutBandwidthThrottle *bandwidth_throttle_;
  int64_t errsim_migration_tablet_id_;
  int64_t errsim_test_tablet_id_;
};

// The host owns generic database facilities. Standby depends only on these
// capabilities instead of the observer or its global context/config objects.
class IStandbyHost
{
public:
  virtual ~IStandbyHost() {}

  virtual int load_log_restore_source(
      common::ObIAllocator &allocator,
      common::ObString &source,
      int64_t &version) const = 0;
  virtual void publish_rpc_cert_expire_time(int64_t expire_time_us) = 0;

  virtual void reset_max_id_cache() = 0;
  virtual int refresh_schema() = 0;

  virtual int bootstrap_primary() = 0;
  virtual int report_bootstrap_telemetry() = 0;
  virtual int wait_primary_metadata_ready() = 0;
  virtual int start_timezone_manager() = 0;
};

} // namespace standby
} // namespace oceanbase

#endif // OCEANBASE_STANDBY_STANDBY_HOST_H_
