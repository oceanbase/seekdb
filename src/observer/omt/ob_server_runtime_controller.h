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

#ifndef OCEANBASE_OBSERVER_OMT_OB_SERVER_RUNTIME_CONTROLLER_H_
#define OCEANBASE_OBSERVER_OMT_OB_SERVER_RUNTIME_CONTROLLER_H_


#include "lib/task/ob_timer.h"
#include "share/ob_shared_timer.h"
#include "share/resource/ob_server_runtime_config.h"
#include "storage/api/storage/runtime/ob_i_server_runtime.h"

namespace oceanbase
{
namespace logservice
{
class ObServerLogBlockMgr;
}
namespace rpc
{
class ObRequest;
}
namespace omt
{

// Forward declearation
class ObServerRuntime;
class ObServerRuntimeMeta;

// This is the entry class of OMT module.

class ObServerRuntimeController : public common::ObTimerTask,
                                  public storage::ObIServerRuntime
{
public:
  // 100ms: bounds packet-retry requeue latency (retry_queue_ is only drained
  // by timeup); heavy work in timeup is gated to 1s internally.
  const     static int64_t TIME_SLICE_PERIOD        = 100000;

public:
  explicit ObServerRuntimeController();

  int init(logservice::ObServerLogBlockMgr &log_block_mgr);

  int start();
  void stop();
  void wait();
  void destroy();

  int create_bootstrap_runtime();
  int refresh_runtime_resources();
  int activate_runtime(const share::ObServerRuntimeConfig &runtime_config);
  int create_runtime(const ObServerRuntimeMeta &meta, bool write_slog) override;
  int update_server_resources(const share::ObServerRuntimeConfig &runtime_config);

  int get_server_resources(share::ObServerRuntimeConfig &runtime_config);
  int get_server_log_disk_size(int64_t &log_disk_size) override;
  int get_runtime_meta_for_ckpt(ObServerRuntimeMeta &meta, bool &exist) override;
  storage::ObServerRuntimeSuperBlock get_super_block() override;
  void set_server_super_block(
      const storage::ObServerRuntimeSuperBlock &super_block) override;
  bool is_hidden() override;
  int update_server_memory(const share::ObServerRuntimeConfig &runtime_config);
  int update_server_log_disk_size(const int64_t old_log_disk_size,
                                  const int64_t new_log_disk_size,
                                  int64_t &allowed_log_disk_size);
  int modify_server_io(const share::ObServerResourceConfig &resource_config);
  int update_server_config();
  int update_palf_config();
  int update_dag_scheduler_config();
  int get_runtime(ObServerRuntime *&runtime) const;
  int lock_runtime(ObServerRuntime *&runtime) const;
  int recv_request(rpc::ObRequest &req) const;
  int update_freezer_mem_limit(const int64_t server_min_mem,
                               const int64_t server_max_mem);
  void reload_request_queue_size();

  inline ObServerRuntime *runtime();
  int get_server_cpu(double &min_cpu, double &max_cpu) const;

  bool has_runtime() const override;
  inline void set_synced() override;
  inline bool has_synced() const;

  int inc_ddl_count(const int64_t cpu_quota_concurrency);
  int dec_ddl_count();

  // Aggregated resources assigned to the single runtime.
  struct ServerResource
  {
    ServerResource() : max_cpu_(0), min_cpu_(0), memory_size_(0),
                       log_disk_size_(0) {}
    ~ServerResource() {}
    void reset() {
      max_cpu_ = 0;
      min_cpu_ = 0;
      memory_size_ = 0;
      log_disk_size_ = 0;
    }
    double max_cpu_;
    double min_cpu_;
    int64_t memory_size_;
    int64_t log_disk_size_;
  };
  // Apply the initial resource configuration and mark the runtime ready.
  int bring_up_runtime();
  int get_server_allocated_resource(ServerResource &server_resource);

protected:
  virtual void runTimerTask() override;
  int get_runtime_unsafe(ObServerRuntime *&runtime) const;
  int construct_bootstrap_meta(ObServerRuntimeMeta &meta);
  void stop_runtime_();
  int update_server_resources_no_lock(const share::ObServerRuntimeConfig &runtime_config);
  int construct_allowed_runtime_config(const int64_t allowed_log_disk_size,
                                    const int64_t max_cpu, const int64_t min_cpu,
                                    const share::ObServerRuntimeConfig &expected_runtime_config,
                                    share::ObServerRuntimeConfig &allowed_runtime_config);

private:
  int update_freezer_config_();
  int update_throttle_config_();
  // Single server-runtime resource configuration sourced from GCONF.
  int build_server_resource_config_(share::ObServerRuntimeConfig &runtime_config);
  int apply_server_resource_config_(const share::ObServerRuntimeConfig &runtime_config);
  int bring_up_runtime_();
  int refresh_server_config_();
  void periodically_check_runtime_();
  int64_t get_refresh_interval_();
protected:
      enum class ObRuntimeCreateStep {
        STEP_BEGIN = 0,
        STEP_CTX_MEM_CONFIG_SETTED,
        STEP_CREATION_PREPARED,
        STEP_RUNTIME_CREATED,
        STEP_FINISH,
      };

  bool is_inited_;

  // Built once during startup and freed only during shutdown. Startup readers
  // see null and receive OB_SERVER_RUNTIME_NOT_READY.
  ObServerRuntime *runtime_;
  // Periodic server-resource refresh cadence.
  static const int64_t BOOTSTRAP_REFRESH_INTERVAL = 100L * 1000L; // 100ms until synced
  int64_t refresh_interval_;
  bool has_synced_;
  bool runtime_active_;
  common::ObTimer timer_;
  common::ObTimer memory_printer_timer_;
  bool timer_stopped_;
  logservice::ObServerLogBlockMgr *log_block_mgr_;

private:
  // Serializes timer-driven and explicit resource-config updates.
  lib::ObMutex resource_conf_lock_;
  DISALLOW_COPY_AND_ASSIGN(ObServerRuntimeController);
}; // end of class ObServerRuntimeController

// Inline function implementation
ObServerRuntime *ObServerRuntimeController::runtime()
{
  return runtime_;
}

void ObServerRuntimeController::set_synced()
{
  has_synced_ = true;
}

bool ObServerRuntimeController::has_synced() const
{
  return has_synced_;
}

class ObSharedTimer : public share::ObISharedTimer
{
public:
  ObSharedTimer() : timer_() {}
  static int server_module_init(ObSharedTimer *&st);
  static int server_module_start(ObSharedTimer *&st);
  static void server_module_stop(ObSharedTimer *&st);
  static void server_module_wait(ObSharedTimer *&st);
  void destroy();
  int schedule(common::ObTimerTask &task, const int64_t delay,
      bool repeat = false, bool immediate = false) override;
  int cancel_task(const common::ObTimerTask &task) override;
  int wait_task(const common::ObTimerTask &task) override;
  bool task_exist(const common::ObTimerTask &task) override;
private:
  common::ObTimer timer_;
};

} // end of namespace omt
} // end of namespace oceanbase


#endif /* OCEANBASE_OBSERVER_OMT_OB_SERVER_RUNTIME_CONTROLLER_H_ */
