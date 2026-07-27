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
#include "share/io/io_schedule/ob_io_schedule_v2.h"
#include "share/io/ob_io_struct.h"

namespace oceanbase
{
namespace common
{
int64_t get_norm_iops(const int64_t size, const double iops, const ObIOMode mode);
int64_t get_norm_bw(const int64_t size, const ObIOMode mode);
class ObIOService;

class ObIOManager final
{
public:
  static ObIOManager &get_instance();
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

  int detect_read(const ObIOInfo &info, ObIOHandle &handle);

  // config related, thread safe
  int set_io_config(const ObIOConfig &conf);
  const ObIOConfig &get_io_config() const;

  // device health management
  ObIOFaultDetector &get_device_health_detector();
  int get_device_health_status(ObDeviceHealthStatus &dhs, int64_t &device_abnormal_time);

  // device channel management
  int add_device_channel(ObIODevice *device_handle, const int64_t async_channel_count, const int64_t sync_channel_count,
      const int64_t max_io_depth);
  int remove_device_channel(ObIODevice *device_handle);
  int get_device_channel(const ObIORequest &req, ObDeviceChannel *&device_channel);

  // service configuration
  int refresh_io_resource_config(const ObIOServiceConfig::ResourceConfig &io_resource_config);
  // service configuration
  int refresh_io_param_config(const ObIOServiceConfig::ParamConfig &io_param_config);
  int get_io_service(ObRefHolder<ObIOService> &service_holder) const;
  OB_INLINE bool is_inited()
  {
    return is_inited_;
  }
  void print_service_status();
  void print_channel_status();
  void print_status();

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
  lib::ObMutex mutex_;
  ObIOConfig io_config_;
  ObConcurrentFIFOAllocator allocator_;
  hash::ObHashMap<int64_t /*device_handle*/, ObDeviceChannel *> channel_map_;
  ObIOFaultDetector fault_detector_;
  ObIOService *io_service_;
};

class ObIOService final
{
public:
  static int server_module_new(ObIOService *&io_service);
  static int server_module_init(ObIOService *&io_service);
  static void server_module_destroy(ObIOService *&io_service);

public:
  ObIOService();
  ~ObIOService();
  int init(const ObIOServiceConfig &io_config);
  int init_io_config();
  void destroy();
  int start();
  void stop();
  bool is_working() const;
  int alloc_and_init_result(const ObIOInfo &info, ObIOResult *&io_result);
  int alloc_req_and_result(const ObIOInfo &info, ObIOHandle &handle, ObIORequest *&io_request, RequestHolder &req_holder);
  int inner_aio(const ObIOInfo &info, ObIOHandle &handle);
  int detect_aio(const ObIOInfo &info, ObIOHandle &handle);
  int enqueue_callback(ObIORequest &req);
  ObIOUsage &get_io_usage()
  {
    return io_usage_;
  }
  ObIOCallbackManager &get_callback_mgr()
  {
    return callback_mgr_;
  };
  ObIOUsage &get_sys_io_usage()
  {
    return io_sys_usage_;
  }
  int update_basic_io_resource_config(const ObIOServiceConfig::ResourceConfig &io_resource_config);
  int update_basic_io_param_config(const ObIOServiceConfig::ParamConfig &io_param_config);
  int try_alloc_req_until_timeout(const int64_t timeout_ts, ObIORequest *&req);
  int try_alloc_result_until_timeout(const int64_t timeout_ts, ObIOResult *&result);
  int alloc_io_request(ObIORequest *&req);
  int alloc_io_result(ObIOResult *&result);
  int init_group_index_map(const ObIOServiceConfig &io_config);
  int get_group_index(const ObIOGroupKey &key, uint64_t &index);
  int calc_io_memory(const int64_t memory);
  int init_memory_pool(const int64_t memory);
  int update_memory_pool(const int64_t memory);
  const ObIOServiceConfig &get_io_config();
  int64_t get_group_num();
  int64_t get_ref_cnt() { return ATOMIC_LOAD(&ref_cnt_); }
  ObIOAllocator *get_io_allocator() { return &io_allocator_; }
  int print_io_status();
  void inc_ref();
  void dec_ref();
  int get_throttled_time(uint64_t group_id, int64_t &throttled_time);

  TO_STRING_KV(K(is_inited_), K(ref_cnt_), K(io_memory_limit_), K(request_count_), K(result_count_),
       K(io_config_), K(io_allocator_), K(callback_mgr_), K(io_memory_limit_),
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
  
  ObIOServiceConfig io_config_;
  ObIOAllocator io_allocator_;
  ObIOCallbackManager callback_mgr_;
  ObIOUsage io_usage_;            // user group usage
  ObIOUsage io_sys_usage_;        // sys group usage
  ObIOMemStats io_mem_stats_;     // Group Level: IO memory monitor
  DRWLock io_config_lock_;                                      // for map and config
  hash::ObHashMap<ObIOGroupKey, uint64_t> group_id_index_map_;  // key:group_id, value:index
  ObIOScheduler qsched_;
};

#define OB_IO_MANAGER (oceanbase::common::ObIOManager::get_instance())
}  // end namespace common
}  // end namespace oceanbase

#endif  // OCEANBASE_LIB_STORAGE_OB_IO_MANAGER_H
