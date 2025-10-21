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

#ifndef _OCEABASE_OBSERVER_OMT_OB_TENANT_NODE_BALANCER_H_
#define _OCEABASE_OBSERVER_OMT_OB_TENANT_NODE_BALANCER_H_

#include "lib/container/ob_vector.h"
#include "lib/net/ob_addr.h"
#include "lib/lock/ob_tc_rwlock.h"
#include "share/ob_unit_getter.h"
#include "share/ob_thread_pool.h"
#include "share/ob_rpc_struct.h"      // obrpc::

namespace oceanbase
{
namespace common
{
class ObMySQLProxy;
class ObServerConfig;
}
namespace omt
{
class ObMultiTenant;
// monitor tenant units and create/delete/modify local OMT.
class ObTenantNodeBalancer
{
public:
  struct ServerResource
  {
  public:
    ServerResource() : max_cpu_(0), min_cpu_(0), memory_size_(0),
                       log_disk_size_(0), data_disk_size_(0) {}
    ~ServerResource() {}
    void reset() {
      max_cpu_ = 0;
      min_cpu_ = 0;
      memory_size_ = 0;
      log_disk_size_ = 0;
      data_disk_size_ = 0;
    }
    double max_cpu_;
    double min_cpu_;
    int64_t memory_size_;
    int64_t log_disk_size_;
    int64_t data_disk_size_;
  };

public:
  static OB_INLINE ObTenantNodeBalancer &get_instance();

  int init(ObMultiTenant *omt, common::ObMySQLProxy &sql_proxy,
    const common::ObAddr &myaddr);

  int handle_notify_unit_resource(const obrpc::TenantServerUnitConfig &arg);
  int notify_create_tenant(const obrpc::TenantServerUnitConfig &unit);

  int try_notify_drop_tenant(const int64_t tenant_id);

  int get_server_allocated_resource(ServerResource &server_resource);

  void handle();

  bool is_inited() { return is_inited_; }

  int64_t get_refresh_interval();

private:
  static const int64_t RECYCLE_LATENCY = 180L * 1000L * 1000L;
  ObTenantNodeBalancer();
  ~ObTenantNodeBalancer();

  int check_new_tenants(share::TenantUnits &units);
  int check_new_tenant(const share::ObUnitInfoGetter::ObTenantConfig &unit,
                       const bool check_data_version,
                       const int64_t abs_timeout_us = INT64_MAX);
  int check_del_tenants(const share::TenantUnits &local_units, share::TenantUnits &units);
  void periodically_check_tenant();
  int fetch_effective_tenants(const share::TenantUnits &old_tenants, share::TenantUnits &new_tenants);
  int check_tenant_resource_released(const uint64_t tenant_id, bool &is_released) const;
  int refresh_tenant(share::TenantUnits &units);
  DISALLOW_COPY_AND_ASSIGN(ObTenantNodeBalancer);

private:
  const int64_t BOOTSTRAP_REFRESH_INTERVAL = 100L * 1000L;
  ObMultiTenant *omt_;
  common::ObAddr myaddr_;
  bool is_inited_;
  share::ObUnitInfoGetter unit_getter_;
  mutable common::TCRWLock lock_;
  int64_t refresh_interval_;
}; // end of class ObTenantNodeBalancer

OB_INLINE ObTenantNodeBalancer &ObTenantNodeBalancer::get_instance()
{
  static ObTenantNodeBalancer instance;
  return instance;
}

} // end of namespace omt
} // end of namespace oceanbase

#endif /* _OCEABASE_OBSERVER_OMT_OB_TENANT_NODE_BALANCER_H_ */
