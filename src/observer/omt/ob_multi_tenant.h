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

#ifndef _OCEABASE_OBSERVER_OMT_OB_MULTI_TENANT_H_
#define _OCEABASE_OBSERVER_OMT_OB_MULTI_TENANT_H_


#include <functional>
#include "lib/task/ob_timer.h"
#include "share/ob_unit_getter.h"       // ObUnitInfoGetter / TenantUnits (formerly via ob_tenant_node_balancer.h)

namespace oceanbase
{
namespace lib
{
class ObShareTenantLimiter;
}
namespace storage
{
class ObStorageLogger;
}
namespace common
{
class ObMySQLProxy;
class ObServerConfig;
}
namespace rpc
{
class ObRequest;
}
namespace omt
{

// Forward declearation
class ObTenant;
class ObTenantHandle;
class ObTenantMeta;

// This is the entry class of OMT module.

class ObMultiTenant : public common::ObTimerTask
{
public:
  // 100ms: bounds packet-retry requeue latency (retry_queue_ is only drained
  // by timeup); heavy work in timeup is gated to 1s internally.
  const     static int64_t TIME_SLICE_PERIOD        = 100000;

public:
  explicit ObMultiTenant();

  int init(common::ObAddr myaddr,
           common::ObMySQLProxy *sql_proxy = NULL,
           bool mtl_bind_flag = true,
           bool embedded = false);

  int start();
  void stop();
  void wait();
  void destroy();

  int create_hidden_sys_tenant();
  int update_hidden_sys_tenant();
  int convert_hidden_to_real_sys_tenant(const share::ObUnitInfoGetter::ObTenantConfig &unit, const int64_t abs_timeout_us = INT64_MAX);
  int create_tenant(const ObTenantMeta &meta, bool write_slog, const int64_t abs_timeout_us = INT64_MAX);
  int update_tenant_unit(const share::ObUnitInfoGetter::ObTenantConfig &unit);

  int get_tenant_unit(share::ObUnitInfoGetter::ObTenantConfig &unit);
  int get_unit_id(uint64_t &unit_id);
  int get_tenant_meta(ObTenantMeta &meta, bool &exist);
  int get_tenant_meta_for_ckpt(ObTenantMeta &meta, bool &exist);
  int update_tenant_memory(const int64_t mem_limit);
  int update_tenant_memory(const share::ObUnitInfoGetter::ObTenantConfig &unit);
  int update_tenant_log_disk_size(const int64_t old_log_disk_size,
                                  const int64_t new_log_disk_size,
                                  int64_t &allowed_log_disk_size);
  int modify_tenant_io(const share::ObUnitConfig &unit_config);
  int update_tenant_config();
  int update_palf_config();
  int update_tenant_dag_scheduler_config();
  int update_tenant_ddl_config();
  int update_tenant_query_response_time_flush_config();
  int get_tenant(ObTenant *&tenant) const;
  int get_tenant_with_tenant_lock(ObTenant *&tenant) const;
  int get_active_tenant_with_tenant_lock(ObTenant *&tenant) const;
  int update_tenant(std::function<int(ObTenant&)> &&func);
  int recv_request(rpc::ObRequest &req);
  int update_tenant_freezer_mem_limit(const int64_t tenant_min_mem,
                                      const int64_t tenant_max_mem);
  void reload_tenant_task_queue_size();

  inline ObTenant *get_tenant_instance();
  // NB: access MTL safely

  inline double get_node_quota() const;
  inline double get_attenuation_factor() const;
  inline int64_t get_times_of_workers() const;
  int get_tenant_cpu_usage(double &usage) const;
  int get_tenant_worker_time(int64_t &worker_time) const;
  int get_tenant_cpu_time(int64_t &rusage_time) const;
  int get_tenant_cpu(double &min_cpu, double &max_cpu) const;

  bool has_tenant() const;
  bool is_available_tenant() const;
  int check_if_hidden_sys(bool &is_hidden_sys);
  inline void set_cpu_dump();
  inline void unset_cpu_dump();

  inline void set_synced();
  inline bool has_synced() const;

  void set_workers_per_cpu(int64_t v);
  int inc_tenant_ddl_count(const int64_t cpu_quota_concurrency);
  int dec_tenant_ddl_count();

  // ==== sys-tenant bring-up & periodic GCONF refresh (collapsed from ObTenantNodeBalancer) ====
  // Aggregated server resource (relocated verbatim from ObTenantNodeBalancer::ServerResource).
  struct ServerResource
  {
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
  // Bring the single sys tenant fully up at boot: flip hidden->real + first
  // GCONF apply + mark synced. Called once from ObServer::try_update_hidden_sys().
  int bring_up_sys_tenant();
  int get_server_allocated_resource(ServerResource &server_resource);

protected:
  virtual void runTimerTask() override;
  int get_tenant_unsafe(ObTenant *&tenant) const;
  int construct_meta_for_hidden_sys(ObTenantMeta &meta);
  int create_virtual_tenants();
  void remove_tenant();
  int update_tenant_unit_no_lock(const share::ObUnitInfoGetter::ObTenantConfig &unit);
  int construct_allowed_unit_config(const int64_t allowed_log_disk_size,
                                    const int64_t max_cpu, const int64_t min_cpu,
                                    const share::ObUnitInfoGetter::ObTenantConfig &expected_unit_config,
                                    share::ObUnitInfoGetter::ObTenantConfig &allowed_unit);

private:
  int update_tenant_freezer_config_();
  int update_throttle_config_();
  // collapsed-from-ObTenantNodeBalancer helpers (single sys tenant, GCONF-sourced)
  int gen_sys_tenant_unit_(share::ObUnitInfoGetter::ObTenantConfig &unit);
  int apply_sys_tenant_unit_(const share::ObUnitInfoGetter::ObTenantConfig &unit,
                             const int64_t abs_timeout_us);
  int bring_up_sys_tenant_();
  int refresh_sys_tenant_config_();
  void periodically_check_sys_tenant_();
  int64_t get_sys_refresh_interval_();
protected:
      static const int DEL_TRY_TIMES = 30;
      enum class ObTenantCreateStep {
        STEP_BEGIN = 0, // begin
        STEP_CTX_MEM_CONFIG_SETTED = 1, // set_tenant_ctx_idle succ
        STEP_LOG_DISK_SIZE_PINNED = 2,  // pin log disk size succ
        STEP_CREATION_PREPARED = 4, // finish prepare create tenant
        STEP_TENANT_NEWED = 5, // new tenant succ
        STEP_FINISH,
      };

  bool is_inited_;
  storage::ObStorageLogger *server_slogger_;

  // Single-tenant: tenant_ is built once during ObServer::start (boot create or
  // slog replay) and freed only at stop()/shutdown; never swapped at runtime.
  // An aligned pointer load/store is atomic, so readers dereference tenant_
  // without a lock (the former bucket_lock_/lock_ guarded a now-collapsed
  // multi-tenant create/remove race). Start-window readers see null and get
  // OB_TENANT_NOT_IN_SERVER via get_tenant_unsafe.
  ObTenant *tenant_;
  // periodic sys-tenant GCONF-refresh cadence (relocated from ObTenantNodeBalancer)
  static const int64_t BOOTSTRAP_REFRESH_INTERVAL = 100L * 1000L; // 100ms until synced
  int64_t refresh_interval_;
  common::ObAddr myaddr_;
  bool cpu_dump_;
  bool has_synced_;
  bool tenant_active_;
  common::ObTimer timer_;
  common::ObTimer memory_printer_timer_;
  bool timer_stopped_;
  bool embedded_;

private:
  lib::ObShareTenantLimiter *tenant_limiter_head_;
  lib::ObMutex limiter_mutex_;
  // serializes concurrent unit-config writers (OMT timer apply vs config reload);
  // the former bucket_lock_ conflated this with the now-removed tenant-lifecycle lock.
  lib::ObMutex unit_conf_lock_;
  DISALLOW_COPY_AND_ASSIGN(ObMultiTenant);
}; // end of class ObMultiTenant

// Inline function implementation
ObTenant *ObMultiTenant::get_tenant_instance()
{
  return tenant_;
}

void ObMultiTenant::set_cpu_dump()
{
  cpu_dump_ = true;
}

void ObMultiTenant::unset_cpu_dump()
{
  cpu_dump_ = false;
}

void ObMultiTenant::set_synced()
{
  has_synced_ = true;
}

bool ObMultiTenant::has_synced() const
{
  return has_synced_;
}

class ObSharedTimer
{
public:
  ObSharedTimer() : timer_() {}
  static int mtl_init(ObSharedTimer *&st);
  static int mtl_start(ObSharedTimer *&st);
  static void mtl_stop(ObSharedTimer *&st);
  static void mtl_wait(ObSharedTimer *&st);
  void destroy();
  int schedule(common::ObTimerTask &task, const int64_t delay,
      bool repeat = false, bool immediate = false);
  int cancel_task(const common::ObTimerTask &task);
  int wait_task(const common::ObTimerTask &task);
  bool task_exist(const common::ObTimerTask &task);
private:
  common::ObTimer timer_;
};

} // end of namespace omt
} // end of namespace oceanbase


#endif /* _OCEABASE_OBSERVER_OMT_OB_MULTI_TENANT_H_ */
