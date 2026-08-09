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
#include "share/ob_server_info.h"

namespace oceanbase
{
namespace common
{
class ObInOutBandwidthThrottle;
}
namespace standby
{

struct StandbyConfig final
{
  StandbyConfig()
    : self_addr_(),
      rpc_port_(0),
      embedded_mode_(false),
      rpc_tls_enabled_(false),
      io_timeout_ms_(0),
      errsim_migration_tablet_id_(0),
      errsim_test_tablet_id_(0)
  {}

  bool is_valid() const
  {
    return self_addr_.is_valid()
        && (embedded_mode_ || rpc_port_ > 0)
        && io_timeout_ms_ > 0;
  }

  common::ObAddr self_addr_;
  int32_t rpc_port_;
  bool embedded_mode_;
  bool rpc_tls_enabled_;
  int64_t io_timeout_ms_;
  int64_t errsim_migration_tablet_id_;
  int64_t errsim_test_tablet_id_;
};

// The host owns generic database facilities. Standby depends only on these
// capabilities instead of the observer or its global context/config objects.
class IStandbyHost
{
public:
  virtual ~IStandbyHost() {}

  virtual share::ObServerRole::Role boot_role() const = 0;
  virtual int load_server_info(share::ObServerInfo &server_info) = 0;
  virtual int initialize_server_info() = 0;
  virtual int update_server_info(const share::ObServerInfo &server_info) = 0;
  // Runtime role publication is monotonic: recovery may become primary, but
  // a running primary is fenced and restarted before it can become standby.
  virtual void publish_server_role(const share::ObServerRole::Role role) = 0;
  virtual void set_write_enabled(const bool enabled) = 0;
  virtual bool is_write_enabled() const = 0;
  virtual void set_recovery_mode(bool enabled) = 0;
  virtual void advance_switchover_epoch() = 0;

  virtual int load_log_restore_source(
      common::ObIAllocator &allocator,
      common::ObString &source,
      int64_t &version) const = 0;
  virtual bool rpc_tls_enabled() const = 0;
  virtual void publish_rpc_cert_expire_time(int64_t expire_time_us) = 0;
  virtual int64_t operation_timeout_us() const = 0;
  virtual common::ObInOutBandwidthThrottle *bandwidth_throttle() = 0;

  virtual void reset_max_id_cache() = 0;
  virtual int get_latest_schema_version(int64_t &schema_version) = 0;
  virtual int submit_schema_refresh(int64_t schema_version) = 0;

  virtual int bootstrap_primary() = 0;
  virtual int report_bootstrap_telemetry() = 0;
  virtual int wait_schema_ready() = 0;
  virtual int wait_timezone_usable() = 0;
  virtual int start_timezone_manager() = 0;
};

} // namespace standby
} // namespace oceanbase

#endif // OCEANBASE_STANDBY_STANDBY_HOST_H_
