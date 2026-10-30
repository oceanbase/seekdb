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

#ifndef OCEANBASE_LIB_STORAGE_OB_IO_MANAGER_H
#define OCEANBASE_LIB_STORAGE_OB_IO_MANAGER_H

#include "lib/restore/ob_io_device.h"
#include <map>
#include <memory>
#include <unordered_map>
#include "share/io/ob_io_struct.h"

namespace oceanbase
{
namespace common
{
struct ObIORuntimeOptions final
{
  static const int64_t DEFAULT_SERVER_MEMORY_LIMIT = 10L * 1024LL * 1024LL * 1024LL;

  explicit ObIORuntimeOptions(const int64_t server_memory_limit = DEFAULT_SERVER_MEMORY_LIMIT)
      : server_memory_limit_(server_memory_limit)
  {}
  bool is_valid() const { return server_memory_limit_ > 0; }
  TO_STRING_KV(K_(server_memory_limit));

  int64_t server_memory_limit_;
};

class ObIOService;

class ObIOManager final
{
public:
  static ObIOManager &get_instance();
  int init(const ObIORuntimeOptions &runtime_options,
      const int64_t memory_limit = DEFAULT_MEMORY_LIMIT,
      const int32_t queue_depth = DEFAULT_QUEUE_DEPTH,
      const int32_t schedule_thread_count = 0);
  int init(const int64_t memory_limit = DEFAULT_MEMORY_LIMIT, const int32_t queue_depth = DEFAULT_QUEUE_DEPTH,
      const int32_t schedule_thread_count = 0);
  void destroy();
  int start();
  void stop();
  void wait();
  bool is_stopped() const;

  int read(const ObIOInfo &info, ObIOHandle &handle);

  int write(const ObIOInfo &info);

  int aio_read(const ObIOInfo &info, ObIOHandle &handle);

  int aio_write(const ObIOInfo &info, ObIOHandle &handle);

  int pread(ObIOInfo &info, int64_t &read_size);

  int pwrite(ObIOInfo &info, int64_t &write_size);


  // config related, thread safe
  int set_io_config(const ObIOConfig &conf);
  const ObIOConfig &get_io_config() const;

  // device health management
  int get_device_health_status(ObDeviceHealthStatus &dhs, int64_t &device_abnormal_time);

  // device channel management
  int add_device_channel(ObIODevice *device_handle, const int64_t async_channel_count, const int64_t sync_channel_count,
      const int64_t max_io_depth);
  int remove_device_channel(ObIODevice *device_handle);
  int begin_request(ObIORequest &req, std::shared_ptr<ObDeviceChannel> &device_channel);
  int benchmark_device(ObIODevice *device, std::shared_ptr<ObDeviceChannel> &channel);

  int refresh_io_service_config(const ObIOServiceConfig &replacement, uint64_t expected_generation,
      struct ResourceConfigReceipt &receipt);
  int refresh_io_runtime_config(const ObIOServiceConfig::ResourceConfig &resources,
      const ObIOServiceConfig::ParamConfig &params);
  int refresh_io_resource_config(const ObIOServiceConfig::ResourceConfig &io_resource_config);
  // service configuration
  int refresh_io_param_config(const ObIOServiceConfig::ParamConfig &io_param_config);
  int get_io_service(ObRefHolder<ObIOService> &service_holder) const;
  OB_INLINE bool is_inited()
  {
    return ATOMIC_LOAD(&is_inited_);
  }
  void print_service_status();
  void print_channel_status();
  void print_status();
  int64_t get_server_memory_limit() const { return ATOMIC_LOAD(&server_memory_limit_); }

private:
  friend class ObIOService;
  static const int64_t DEFAULT_MEMORY_LIMIT = 10L * 1024LL * 1024LL * 1024LL;  // 10GB
  static const int32_t DEFAULT_QUEUE_DEPTH = 10000;
  ObIOManager();
  ~ObIOManager();
  int dispatch_aio(const ObIOInfo &info, ObIOHandle &handle);
  DISABLE_COPY_ASSIGN(ObIOManager);

private:
  bool is_inited_;
  bool is_working_;
  bool is_stopping_ = false;
  std::mutex lifecycle_mutex_;
  int64_t server_memory_limit_;
  mutable lib::ObMutex mutex_;
  ObIOConfig io_config_;
  ObConcurrentFIFOAllocator allocator_;
  std::unordered_map<int64_t, std::shared_ptr<ObDeviceChannel>> channel_map_;
  ObIOService *io_service_ = nullptr;
};

struct ResourceConfigReceipt
{
  uint64_t generation_ = 0;
  int64_t effective_io_buffer_limit_ = 0;
  int64_t effective_wrapper_limit_ = 0;
  int64_t configured_callback_threads_ = 0;
  bool admission_debt_ = false;
  bool physical_reserve_debt_ = false;
};

struct IOConfigSnapshot
{
  ObIOServiceConfig config_;
  uint64_t generation_ = 0;
  int64_t effective_server_memory_ = 0;
  int64_t effective_io_buffer_limit_ = 0;
  int64_t effective_wrapper_limit_ = 0;
  std::map<uint64_t, uint64_t> group_indices_;
  std::map<uint64_t, uint64_t> module_groups_;
  std::shared_ptr<ObIOUsage> usage_;
};

class ObIOService final
{
public:
  ObIOService();
  ~ObIOService();
  int init(const ObIOServiceConfig &io_config,
      int64_t server_memory = ObIORuntimeOptions::DEFAULT_SERVER_MEMORY_LIMIT, int64_t queue_depth = 10000);
  void destroy();
  int start();
  void stop_requests();
  void stop();
  bool is_working() const;
  int alloc_and_init_result(const ObIOInfo &info, ObIOResult *&io_result);
  int alloc_req_and_result(const ObIOInfo &info, ObIOHandle &handle, ObIORequest *&io_request, RequestHolder &req_holder);
  int inner_aio(const ObIOInfo &info, ObIOHandle &handle);
  int complete_request(ObIORequest &req);
  std::shared_ptr<const IOConfigSnapshot> config_snapshot() const;
  ObIOCallbackManager &get_callback_mgr()
  {
    return callback_mgr_;
  };
  ObIOUsage &get_sys_io_usage()
  {
    return io_sys_usage_;
  }
  int replace_config(const ObIOServiceConfig &replacement, uint64_t expected_generation,
      ResourceConfigReceipt &receipt);
  int health_status(storage_io::HealthStatus &status) const;
  int update_health_config(const ObIOConfig &conf);
  int update_runtime_config(const ObIOServiceConfig::ResourceConfig *resources,
      const ObIOServiceConfig::ParamConfig *params);
  int register_device(const storage_io::DeviceOptions &options, int64_t depth, int64_t probe_bytes,
      storage_io::DeviceHandle &endpoint);
  int remove_device(uint64_t device_id);
  int resource_stats(storage_io::ServiceStats &stats) const;
  rust::Box<storage_io::CalibrationView> calibration_snapshot() const;
  int replace_calibration(rust::Slice<const storage_io::CalibrationSample> samples);
  int reset_calibration();
  storage_io::BeginBenchmarkDto begin_benchmark(const storage_io::BenchmarkShapeDto &shape,
      storage_io::BenchmarkPreparationSlot &preparation, storage_io::BenchmarkRunSlot &run);
  storage_io::BenchmarkStatusDto benchmark_status() const;
  void stop_benchmarks();
  int try_alloc_req_until_timeout(const int64_t timeout_ts, ObIORequest *&req);
  int try_alloc_result_until_timeout(const int64_t timeout_ts, ObIOResult *&result);
  int alloc_io_request(ObIORequest *&req);
  int alloc_io_result(ObIOResult *&result);
  int init_memory_pool(const int64_t memory);
  int64_t get_group_num();
  int64_t get_ref_cnt() { return ATOMIC_LOAD(&ref_cnt_); }
  int print_io_status();
  void inc_ref();
  void dec_ref();

  TO_STRING_KV(K(is_inited_), K(ref_cnt_), K(io_memory_limit_), K(request_count_), K(result_count_),
       KPC(io_allocator_.get()), K(callback_mgr_), K(io_memory_limit_),
       K(request_count_), K(result_count_));
private:
  friend class ObIORequest;
  friend class ObIOResult;
  bool is_inited_;
  bool is_working_;
  int64_t ref_cnt_;
  int64_t io_memory_limit_;
  int64_t request_count_;
  int64_t result_count_;
  
  int submit_request(ObIORequest &req);
  int replace_config_locked(const ObIOServiceConfig &replacement, uint64_t expected_generation,
      int64_t server_memory, ResourceConfigReceipt &receipt);
  int prepare_config(const ObIOServiceConfig &replacement, int64_t server_memory,
      std::shared_ptr<IOConfigSnapshot> &candidate, storage_io::ResourceConfig &native);
  void publish_config(std::shared_ptr<IOConfigSnapshot> candidate,
      const storage_io::ConfigReceipt &applied, ResourceConfigReceipt &receipt);
  std::shared_ptr<const IOConfigSnapshot> accepted_config_;
  std::optional<rust::Box<storage_io::ServiceHandle>> rust_service_;
  mutable std::mutex config_mutex_;
  std::shared_ptr<ObIOAllocator> io_allocator_;
  ObIOCallbackManager callback_mgr_;
  ObIOUsage io_sys_usage_;        // sys group usage

};

#define OB_IO_MANAGER (oceanbase::common::ObIOManager::get_instance())
}  // end namespace common
}  // end namespace oceanbase

#endif  // OCEANBASE_LIB_STORAGE_OB_IO_MANAGER_H
