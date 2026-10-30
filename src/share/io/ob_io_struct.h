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

#ifndef OCEANBASE_LIB_STORAGE_OB_IO_STRUCT_H
#define OCEANBASE_LIB_STORAGE_OB_IO_STRUCT_H

#ifndef _WIN32
#include <sys/resource.h>
#endif
#include "lib/allocator/ob_concurrent_fifo_allocator.h"
#include "lib/lock/ob_mutex.h"
#include "lib/lock/ob_drw_lock.h"
#include "lib/thread/ob_simple_thread_pool.h"
#include "lib/thread/threads.h"
#include "lib/queue/ob_link_queue.h"
#include "lib/queue/ob_fixed_queue.h"
#include "lib/container/ob_array_iterator.h"
#include "lib/container/ob_array_wrap.h"
#include "lib/lock/ob_spin_lock.h"
#include "share/io/ob_io_define.h"
#include "lib/queue/ob_lighty_queue.h"
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <optional>
#include <unordered_map>
#include "rust/storage_io.h"
#include "lib/thread/ob_simple_thread_pool.h"

#ifdef _WIN32
// Windows: define rusage structure and getrusage function
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

struct rusage {
  struct timeval ru_utime; // user CPU time used
  struct timeval ru_stime; // system CPU time used
  long   ru_maxrss;        // maximum resident set size
  long   ru_ixrss;         // integral shared memory size
  long   ru_idrss;         // integral unshared data size
  long   ru_isrss;         // integral unshared stack size
  long   ru_minflt;        // page reclaims (soft page faults)
  long   ru_majflt;        // page faults (hard page faults)
  long   ru_nswap;         // swaps
  long   ru_inblock;       // block input operations
  long   ru_oublock;       // block output operations
  long   ru_msgsnd;        // IPC messages sent
  long   ru_msgrcv;        // IPC messages received
  long   ru_nsignals;      // signals received
  long   ru_nvcsw;         // voluntary context switches
  long   ru_nivcsw;        // involuntary context switches
};

#define RUSAGE_SELF 0
#define RUSAGE_THREAD 1

// Forward declaration or inline implementation
int getrusage(int who, struct rusage *usage);
#endif

namespace oceanbase
{
namespace common
{
int convert_storage_error(int error);

struct ObIOConfig final
{
public:
  ObIOConfig();
  ~ObIOConfig();
  void set_default_value();
  bool is_valid() const;
  TO_STRING_KV(
      K(read_failure_black_list_interval_),
      K(data_storage_warning_tolerance_time_),
      K(disk_io_thread_count_),
      K(sync_io_thread_count_),
      K(data_storage_io_timeout_ms_));

private:
  // async io thread count
  static const int64_t MAX_IO_THREAD_COUNT = 32 * 2;
  // sync io thread count
  static const int64_t MAX_SYNC_IO_THREAD_COUNT = 1024;
public:
  // diagnose related
  int64_t read_failure_black_list_interval_; // const literal
  int64_t data_storage_warning_tolerance_time_;
  // resource related
  int64_t disk_io_thread_count_;
  int64_t sync_io_thread_count_;
  int64_t data_storage_io_timeout_ms_;
};

class ObIOAllocator : public ObIAllocator
{
public:
  ObIOAllocator();
  virtual ~ObIOAllocator();
  int init(const int64_t memory_limit);
  void destroy();
  void commit_memory_limit(int64_t memory_limit);
  int64_t get_allocated_size() const;
  virtual void *alloc(const int64_t size, const lib::ObMemAttr &attr) override;
  virtual void *alloc(const int64_t size) override;
  virtual void free(void *ptr) override;
  template<typename T, typename... Args> int alloc(T *&instance, Args &...args);
  template<typename T> void free(T *instance);
  TO_STRING_KV(K(is_inited_), "allocated", inner_allocator_.allocated());
private:
  bool is_inited_;
  int64_t memory_limit_;
  ObConcurrentFIFOAllocator inner_allocator_;
};


struct ObIOUsageInfo
{
  double avg_iops_ = 0;
  double avg_byte_ = 0;
  int64_t avg_prepare_delay_us_ = 0;
  int64_t avg_schedule_delay_us_ = 0;
  int64_t avg_submit_delay_us_ = 0;
  int64_t avg_device_delay_us_ = 0;
  int64_t avg_total_delay_us_ = 0;
  TO_STRING_KV(K(avg_iops_), K(avg_byte_), K(avg_prepare_delay_us_), K(avg_schedule_delay_us_), K(avg_submit_delay_us_), K(avg_device_delay_us_), K(avg_total_delay_us_));
};

typedef ObSEArray<ObIOUsageInfo, GROUP_START_NUM * static_cast<uint64_t>(ObIOGroupMode::MODECNT)> ObIOUsageInfoArray;
class ObIOUsage final
{
public:
  ObIOUsage() : statistics_(storage_io::new_statistics_handle()), lane_count_(0) {}
  int init(const int64_t group_num);
  void accumulate(ObIORequest &request);
  int snapshot(ObIOUsageInfoArray &output,
      storage_io::StatisticsSampleMode mode = storage_io::StatisticsSampleMode::Cached) const;
  storage_io::CompletionRatesDto sample_failure(uint64_t index) const;
  TO_STRING_KV(K(lane_count_));
private:
  rust::Box<storage_io::StatisticsHandle> statistics_;
  int64_t lane_count_;
};

class ObDeviceChannel final : public lib::Threads
{
public:
  ObDeviceChannel();
  ~ObDeviceChannel();
  int init(ObIODevice *device_handle, int64_t async_channel_count,
      int64_t sync_channel_count, int64_t max_io_depth, ObIOService &service);
  void destroy();
  void stop() override;
  uint64_t device_id() const { return device_id_; }
  int begin(ObIORequest &req);
  int prepare_reserved(ObIORequest &req);
  int register_pending(ObIORequest &req);
  void unregister_pending(ObIORequest &req);
  void print_status();
  int reload_config(const ObIOConfig &conf);
  void run1() override;
  TO_STRING_KV(KP(device_handle_), K(worker_threads_), K(device_id_));
private:
  void consume(rust::Box<storage_io::CompletionSlot> completion);
  static int64_t worker_thread_count(int64_t configured);
  std::mutex mutex_;
  bool is_inited_;
  bool threads_inited_;
  std::atomic<bool> stopping_;
  std::condition_variable ready_condition_;
  bool ready_ = false;
  ObIODevice *device_handle_;
  int64_t worker_threads_;
  uint64_t device_id_ = 0;
  bool native_aio_ = false;
  std::optional<rust::Box<storage_io::DeviceHandle>> endpoint_;
  std::unordered_map<uint64_t, ObIORequest *> pending_;
};

class ObIOCallbackManager final : public ObLinkQueueThreadPool
{
public:
  ObIOCallbackManager();
  ~ObIOCallbackManager();
  int init(int64_t thread_count,
           const int32_t queue_depth);
  void destroy();

  int complete_request(ObIORequest &req);
  int prepare_thread_count(int64_t thread_count);
  void commit_thread_count(int64_t thread_count);
  int64_t configured_thread_count() const { return ATOMIC_LOAD(&config_thread_count_); }
  int64_t to_string(char *buf, const int64_t len) const;
private:
  virtual void handle(LinkTask *task) override;
  static void process_completion(ObIORequest &req);
private:
  bool is_inited_;
  int64_t config_thread_count_;
};

// Device Health status
enum ObDeviceHealthStatus
{
  DEVICE_HEALTH_NORMAL = 0,
  DEVICE_HEALTH_WARNING
};

const char *device_health_status_to_str(const ObDeviceHealthStatus dhs);

class ObIOTracer final
{
public:
  struct RefLog
  {
  public:
    RefLog();
    void click(const char *mod = NULL);
    TO_STRING_KV(K_(click_count), "ref_log", ObArrayWrap<const char *>(click_str_, min(MAX_CLICK_COUNT, click_count_)));
  private:
    static const int64_t MAX_CLICK_COUNT = 16;
    int64_t click_count_;
    const char *click_str_[MAX_CLICK_COUNT];
  };
  struct TraceInfo
  {
  public:
    TraceInfo();
    uint64_t hash() const;
    int hash(uint64_t &hash_val) const { hash_val = hash(); return OB_SUCCESS; }
    bool operator== (const TraceInfo &param) const;
    TO_STRING_KV(KCSTRING(bt_str_), K(ref_log_));
  public:
    char bt_str_[LBT_BUFFER_LENGTH];
    RefLog ref_log_;
  };
  enum TraceType
  {
    IS_FIRST,
    IS_LAST,
    OTHER
  };
  ObIOTracer();
  ~ObIOTracer();
  int init();
  void destroy();
  void reuse();
  int trace_request(const ObIORequest *req, const char *msg, const TraceType trace_type);
  void print_status();
  int64_t to_string(char *buf, const int64_t len) const;
private:
  bool is_inited_;
  hash::ObHashMap<int64_t /*request_ptr*/, TraceInfo> trace_map_;
};

// template function implementation
template<typename T, typename... Args>
int ObIOAllocator::alloc(T *&instance, Args &...args)
{
  int ret = OB_SUCCESS;
  instance = nullptr;
  void *buf = nullptr;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "io allocator not init", K(ret), K(is_inited_));
  } else if (OB_ISNULL(buf = inner_allocator_.alloc(sizeof(T)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    COMMON_LOG(WARN, "allocate memory failed", K(ret), KP(buf), K(sizeof(T)));
  } else {
    instance = new (buf) T(args...);
  }
  return ret;
}

template<typename T>
void ObIOAllocator::free(T *instance)
{
  if (nullptr != instance) {
    instance->~T();
    inner_allocator_.free(instance);
  }
}

// some utilities

inline bool is_io_aligned(const int64_t value, const int64_t align)
{
  return (align > 0) && (0 == (value % align));
}

inline void align_offset_size(
    const int64_t offset,
    const int64_t size,
    const int64_t align,
    int64_t &align_offset,
    int64_t &align_size)
{
  align_offset = lower_align(offset, align);
  align_size = upper_align(size + offset - align_offset, align);
}

typedef ObRefHolder<ObIORequest> RequestHolder;

class ObTraceIDGuard final
{
public:
  explicit ObTraceIDGuard(const ObCurTraceId::TraceId &trace_id)
  {
    saved_trace_id_ = *ObCurTraceId::get_trace_id();
    ObCurTraceId::set(trace_id);
  }
  ~ObTraceIDGuard()
  {
    ObCurTraceId::set(saved_trace_id_);
  }
private:
  ObCurTraceId::TraceId saved_trace_id_;
};


}// end namespace common
}// end namespace oceanbase

#endif//OCEANBASE_LIB_STORAGE_OB_IO_STRUCT_H
