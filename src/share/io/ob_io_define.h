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

#ifndef OCEANBASE_LIB_STORAGE_IO_DEFINE
#define OCEANBASE_LIB_STORAGE_IO_DEFINE

#include "lib/restore/ob_io_device.h"
#include "lib/container/ob_array_iterator.h"
#include "lib/container/ob_array_wrap.h"
#include "lib/container/ob_heap.h"
#include "lib/container/ob_rbtree.h"
#include "lib/list/ob_list.h"
#include "lib/lock/ob_thread_cond.h"
#include "lib/profile/ob_trace_id.h"
#include "lib/thread/ob_link_task.h"
#include "lib/worker.h"
#include "share/ob_define.h"
#include "rust/storage_io.h"
#include <optional>
#include <memory>

namespace oceanbase
{
namespace share
{
class ObServerResourceConfig;
}
namespace common
{

// default NIC bandwidth 10000Mbit→1250MBps(moved down from observer ObServer:base vocabulary for IO scheduling)
constexpr int64_t OB_DEFAULT_ETHERNET_SPEED = 10000 / 8 * 1024 * 1024;
class ObIOCallbackManager;

static constexpr int64_t DEFAULT_IO_WAIT_TIME_MS = 5000L;        // 5s
static constexpr int64_t MAX_IO_WAIT_TIME_MS = 300L * 1000L;     // 5min
static constexpr int64_t GROUP_START_NUM = 1L;
static constexpr int64_t DEFAULT_IO_WAIT_TIME_US = 5000L * 1000L;  // 5s

enum class ObIOMode : uint8_t { READ = 0, WRITE = 1, MAX_MODE };

enum class ObIOGroupMode : uint8_t { LOCALREAD = 0, LOCALWRITE = 1, MODECNT };

enum class ObIOPriority : uint8_t {
  EMERGENT = 0,  // Reserved
  HIGH = 1,      // dump write, middle layer index read
  MIDDLE = 2,    // merge write, temporary file write
  LOW = 3        // background task read/write, for example CRC check
};

const char *get_io_mode_string(const ObIOMode mode);
const char *get_io_mode_string(const ObIOGroupMode mode);
int transform_usage_index_to_group_config_index(const uint64_t &usage_index, uint64_t &group_config_index);
ObIOMode get_io_mode_enum(const char *mode_string);

enum ObIOModule {
  SYS_MODULE_START_ID = 1,
  SLOG_IO = SYS_MODULE_START_ID,
  CALIBRATION_IO,
  DETECT_IO,
  SSTABLE_WHOLE_SCANNER_IO,
  INSPECT_BAD_BLOCK_IO,
  SSTABLE_INDEX_BUILDER_IO,
  BACKUP_READER_IO,
  BLOOM_FILTER_IO,
  INDEX_BLOCK_TREE_CURSOR_IO,
  MICRO_BLOCK_CACHE_IO,
  ROOT_BLOCK_IO,
  TMP_PAGE_CACHE_IO,
  INDEX_BLOCK_MICRO_ITER_IO,
  LINKED_MACRO_BLOCK_IO,
  TMP_MEM_BLOCK_IO,
  SSTABLE_MACRO_BLOCK_WRITE_IO,
  CLOG_WRITE_IO,
  CLOG_READ_IO,
  // end
  SYS_MODULE_END_ID
};

const int64_t SYS_MODULE_CNT = SYS_MODULE_END_ID - SYS_MODULE_START_ID;

const char *get_io_sys_group_name(ObIOModule module);
struct ObIOFlag final
{
public:
  ObIOFlag();
  ~ObIOFlag();
  ObIOFlag &operator=(const ObIOFlag &other)
  {
    this->flag_ = other.flag_;
    this->sys_module_id_ = other.sys_module_id_;
    return *this;
  }
  void reset();
  bool is_valid() const;
  void set_mode(ObIOMode mode);
  ObIOMode get_mode() const;
  void set_wait_event(int64_t wait_event_id);
  void set_sys_module_id(const uint64_t sys_module_id);
  uint64_t get_sys_module_id() const;
  bool is_sys_module() const;
  int64_t get_wait_event() const;
  void set_read();
  bool is_read() const;
  void set_write();
  bool is_write() const;
  void set_sync();
  bool is_sync() const;
  void set_unlimited(const bool is_unlimited = true);
  bool is_unlimited() const { return is_unlimited_; }
  void set_detect(const bool is_detect = true);
  bool is_detect() const;
  void set_priority(const ObIOPriority priority);
  ObIOPriority get_priority() const;
  // user layer please do not call this function! only allow underlying io layer call this function!
  void set_write_through(const bool is_write_through = true);
  bool is_write_through() const;
  void set_unsealed();
  void set_sealed();
  bool is_sealed() const;
  void set_dirty();
  void set_clean();
  bool is_dirty() const;
  TO_STRING_KV("mode", common::get_io_mode_string(static_cast<ObIOMode>(mode_)),
      K(wait_event_id_), K(is_sync_), K(is_unlimited_), K(is_detect_), K(is_write_through_), K(is_sealed_),
      K(reserved_));

private:
  friend struct ObIOResult;
  static constexpr int64_t IO_MODE_BIT = 4; // read, write, append
  static constexpr int64_t IO_WAIT_EVENT_BIT = 32; // for performance monitor
  static constexpr int64_t IO_SYNC_FLAG_BIT = 1; // indicate if the caller is waiting io finished
  static constexpr int64_t IO_DETECT_FLAG_BIT = 1;
  static constexpr int64_t IO_UNLIMITED_FLAG_BIT = 1; // indicate if the io is unlimited
  static constexpr int64_t IO_WRITE_THROUGH_BIT = 1; // indicate write mode of the io
  // indicate if the block io is sealed. only sealed blocks can be flushed by the IO layer.
  // e.g., for block append write, the first n writes should set is_sealed_ to false, and the
  // last write should set is_sealed to true. only after the last write set is_sealed to true,
  // the block can be flushed by the IO layer.
  static constexpr int64_t IO_SEALED_FLAG_BIT = 1;
  static constexpr int64_t IO_RESERVED_BIT = 64 - IO_MODE_BIT
                                                - IO_WAIT_EVENT_BIT
                                                - IO_SYNC_FLAG_BIT
                                                - IO_DETECT_FLAG_BIT
                                                - IO_UNLIMITED_FLAG_BIT
                                                - IO_WRITE_THROUGH_BIT
                                                - IO_SEALED_FLAG_BIT;

  union { // FARM COMPAT WHITELIST
    int64_t flag_;
    struct {
      int64_t mode_ : IO_MODE_BIT;
      int64_t wait_event_id_ : IO_WAIT_EVENT_BIT;
      int64_t is_sync_ : IO_SYNC_FLAG_BIT;
      int64_t is_unlimited_ : IO_UNLIMITED_FLAG_BIT;
      int64_t is_detect_ : IO_DETECT_FLAG_BIT;
      int64_t is_write_through_ : IO_WRITE_THROUGH_BIT;
      int64_t is_sealed_ : IO_SEALED_FLAG_BIT;
      int64_t reserved_ : IO_RESERVED_BIT;
    };
  };
  uint64_t sys_module_id_;
};

// different io callback types enqueue different io callback thread queue
enum class ObIOCallbackType : uint8_t {
  ASYNC_SINGLE_MICRO_BLOCK_CALLBACK = 0,
  MULTI_DATA_BLOCK_CALLBACK = 1,
  SYNC_SINGLE_MICRO_BLOCK_CALLBACK = 2,
  STORAGE_META_CALLBACK = 3,
  TMP_PAGE_CALLBACK = 4,
  TMP_MULTI_PAGE_CALLBACK = 5,
  TMP_DIRECT_READ_PAGE_CALLBACK = 6,
  TEST_CALLBACK = 7, // just for unittest
  TMP_CACHED_READ_CALLBACK = 8,
  MAX_CALLBACK_TYPE = 9
};

class ObIOCallback
{
public:
  static const int64_t CALLBACK_BUF_SIZE = 1024;
  ObIOCallback(const ObIOCallbackType type);
  virtual ~ObIOCallback();

  int process(const char *data_buffer, const int64_t size);
  virtual ObIAllocator *get_allocator() = 0;
  virtual const char *get_data() = 0;
  virtual int64_t size() const = 0;
  virtual int alloc_data_buf(const char *io_data_buffer, const int64_t data_size) = 0;
  virtual int inner_process(const char *data_buffer, const int64_t size) = 0;
  ObIOCallbackType get_type() const
  {
    return type_;
  }
  virtual const char *get_cb_name() const = 0;
  DECLARE_PURE_VIRTUAL_TO_STRING;

protected:
  static int alloc_and_copy_data(
      const char *io_data_buffer, const int64_t data_size, common::ObIAllocator *allocator, char *&data_buffer);

protected:
  ObIOCallbackType type_;

private:
  friend class ObIOResult;
};

template <typename T>
void free_io_callback(ObIOCallback *&io_callback)
{
  if (OB_NOT_NULL(io_callback) && OB_NOT_NULL(io_callback->get_allocator())) {
    ObIAllocator *allocator = io_callback->get_allocator();
    T *specific_callback = static_cast<T *>(io_callback);
    specific_callback->~T();
    allocator->free(specific_callback);
    specific_callback = nullptr;
    io_callback = nullptr;
  }
}

struct ObSNIOInfo
{
public:
  ObSNIOInfo();
  explicit ObSNIOInfo(const ObSNIOInfo &other);
  virtual ~ObSNIOInfo();
  virtual void reset();
  bool is_valid() const;
  ObSNIOInfo &operator=(const ObSNIOInfo &other);
  TO_STRING_KV(K(fd_), K(offset_), K(size_), K(timeout_us_), K(flag_), KP(callback_), KP(buf_),
      KP(user_data_buf_));

public:
  
  ObIOFd fd_;
  int64_t offset_;
  int64_t size_;
  int64_t timeout_us_;
  ObIOFlag flag_;
  ObIOCallback *callback_;
  const char *buf_;
  char *user_data_buf_;  // actual data buf without cb, allocated by the calling layer
};


#define ObIOInfo ObSNIOInfo

template <typename T>
class ObRefHolder final
{
public:
  explicit ObRefHolder() : ptr_(nullptr)
  {}
  explicit ObRefHolder(T *ptr) : ptr_(nullptr)
  {
    hold(ptr);
  }
  ~ObRefHolder()
  {
    reset();
  }
  T *get_ptr()
  {
    return ptr_;
  }
  void hold(T *ptr)
  {
    if (nullptr != ptr && ptr != ptr_) {
      ptr->inc_ref();
      reset();  // reset previous ptr, must after ptr->inc_ref()
      ptr_ = ptr;
    }
  }
  void reset()
  {
    if (nullptr != ptr_) {
      ptr_->dec_ref();
      ptr_ = nullptr;
    }
  }
  TO_STRING_KV(KP_(ptr));

private:
  T *ptr_;
};

struct ObIOTimeLog final
{
public:
  ObIOTimeLog();
  ~ObIOTimeLog();
  void reset();
  // Note: -1 represents invalid value
  TO_STRING_KV(K_(begin_ts), "enqueue_used", (enqueue_ts_ > 0 ? enqueue_ts_ - begin_ts_ : -1), "dequeue_used",
      (dequeue_ts_ > 0 ? dequeue_ts_ - enqueue_ts_ : -1), "submit_used",
      (submit_ts_ > 0 ? submit_ts_ - dequeue_ts_ : -1), "return_used", (return_ts_ > 0 ? return_ts_ - submit_ts_ : -1),
      "callback_enqueue_used", (callback_enqueue_ts_ > 0 ? callback_enqueue_ts_ - return_ts_ : -1),
      "callback_dequeue_used", (callback_dequeue_ts_ > 0 ? callback_dequeue_ts_ - callback_enqueue_ts_ : -1),
      "callback_finish_used", (callback_finish_ts_ > 0 ? callback_finish_ts_ - callback_dequeue_ts_ : -1), "end_used",
      (end_ts_ > 0 ? end_ts_ - callback_finish_ts_ : -1));

public:
  int64_t begin_ts_;
  int64_t enqueue_ts_;
  int64_t dequeue_ts_;
  int64_t submit_ts_;
  int64_t return_ts_;
  volatile int64_t callback_enqueue_ts_;
  volatile int64_t callback_dequeue_ts_;
  volatile int64_t callback_finish_ts_;
  int64_t end_ts_;
};

int64_t get_io_interval(const int64_t &end_time, const int64_t &begin_time);

struct ObIORetCode final
{
public:
  ObIORetCode();
  ObIORetCode(int io_ret, int fs_errno = 0);
  ~ObIORetCode();
  void reset();
  TO_STRING_KV(K(io_ret_), K(fs_errno_));

public:
  int io_ret_;
  int fs_errno_;
};

class ObIOService;
class ObIOChannel;
class ObIOUsage;
class ObIORequest;

struct ObIOGroupKey
{
  ObIOGroupKey() : group_id_(0), mode_(ObIOMode::MAX_MODE)
  {}
  ObIOGroupKey(uint64_t group_id, ObIOMode mode) : group_id_(group_id), mode_(mode)
  {}
  int hash(uint64_t &res) const
  {
    res = ((uint64_t)mode_) << 56 | group_id_;
    return OB_SUCCESS;
  }
  uint64_t hash() const
  {
    return ((uint64_t)mode_) << 56 | group_id_;
  }
  bool operator==(const ObIOGroupKey &that) const
  {
    return group_id_ == that.group_id_ && mode_ == that.mode_;
  }
  ObIOGroupKey& operator=(const ObIOGroupKey &other)
  {
    if (this != &other) {
      group_id_ = other.group_id_;
      mode_ = other.mode_;
    }
    return *this;
  }
  uint64_t group_id_;
  ObIOMode mode_;
  TO_STRING_KV(K(group_id_), K(mode_));
};

struct IOConfigSnapshot;
class ObIOAllocator;

class ObIOResult final
{
public:
  ObIOResult();
  ~ObIOResult();
  bool is_valid() const;
  int basic_init();  // for pool
  int init(const ObIOInfo &info);
  void reset();
  void destroy();
  void cancel();
  int wait(int64_t wait_ms);
  void finish(const ObIORetCode &ret_code, ObIORequest *req = nullptr);
  ObIOMode get_mode() const; // 2 mode : read and write
  ObIOGroupKey get_group_key() const;
  uint64_t get_sys_module_id() const;
  bool is_sys_module() const;
  int64_t get_data_size() const;
  uint64_t get_io_usage_index();
  void inc_ref(const char *msg = nullptr);
  void dec_ref(const char *msg = nullptr);
  void inc_out_ref();
  void dec_out_ref();
  void finish_without_accumulate(const ObIORetCode &ret_code);
  void set_complete_size(const int64_t complete_size) { complete_size_ = complete_size; }
  int cal_delay_us(int64_t &prepare_delay, int64_t &schedule_delay, int64_t &submit_delay, int64_t &device_delay, int64_t &total_delay);
  int transform_group_config_index_to_usage_index(const ObIOGroupKey &key, uint64_t &usage_index);
  ObThreadCond &get_cond() { return cond_; }

  TO_STRING_KV(K(is_inited_), K(is_finished_), K(is_canceled_), K(has_estimated_), K(complete_size_), K(offset_), K(size_),
               K(timeout_us_), K(result_ref_cnt_), K(out_ref_cnt_), K(flag_), K(ret_code_), KP(io_service_),
               KP(user_data_buf_), KP(buf_), KP(io_callback_), K_(time_log));
  DISALLOW_COPY_AND_ASSIGN(ObIOResult);
private:
  friend class ObIORequest;
  friend class ObIOHandle;
  friend class ObIOService;
  friend class ObDeviceChannel;
  friend class ObIOCallbackManager;
  bool is_inited_;
  bool is_finished_;
  bool is_canceled_;
  bool has_estimated_;
  enum class CallbackOwnership { Borrowed, Adopted, Detached };
  CallbackOwnership callback_ownership_ = CallbackOwnership::Borrowed;
  std::optional<rust::Box<storage_io::CancelHandle>> rust_control_;
  std::shared_ptr<const IOConfigSnapshot> config_snapshot_;
  uint64_t group_id_ = 0;
  uint64_t group_index_ = 0;
  volatile int32_t result_ref_cnt_; //for io_result and io_handle
  volatile int32_t out_ref_cnt_; //for io_handle
  int64_t complete_size_;
  int64_t offset_;
  int64_t size_;
  int64_t timeout_us_;
  
  int64_t aligned_size_;
  ObIOService *io_service_;
  std::shared_ptr<ObIOAllocator> allocation_owner_;
  const char *buf_;
  char *user_data_buf_; //actual data buf without cb, allocated by thpe calling layer
  ObIOCallback *io_callback_;
  ObIOFlag flag_;
  ObThreadCond cond_;
public:
  ObIOTimeLog time_log_;
  ObIORetCode ret_code_;
};

class ObIORequest : public common::ObDLinkBase<ObIORequest>
{
public:
  ObIORequest();
  virtual ~ObIORequest();
  bool is_valid() const;
  int init(const ObIOInfo &info, ObIOResult *result);
  int basic_init();  // for pool
  virtual void destroy();
  virtual void reset();
  void free();
  void set_result(ObIOResult &io_result);
  bool is_canceled();
  int64_t timeout_ts() const;
  int64_t get_data_size() const;
  ObIOGroupKey get_group_key() const;
  bool is_sys_module() const;
  const ObIOFlag &get_flag() const;
  ObIOMode get_mode() const; // 2 mode
  ObIOGroupMode get_group_mode() const;
  ObIOCallback *get_callback() const;
  int64_t get_align_size() const;
  int64_t get_align_offset() const;
  int recycle_buffer();
  bool can_callback() const;
  void free_io_buffer();
  void inc_ref();
  void dec_ref();

  int64_t get_remained_io_timeout_us();

  TO_STRING_KV(K(is_inited_), K(ref_cnt_), K(registration_tag_), K(rust_id_), K(fd_),
               K(trace_id_), KP(io_service_), KPC(io_result_));
private:
  friend class ObDeviceChannel;
  friend class ObIOManager;
  friend class ObIOResult;
  friend class ObIOCallbackManager;
  friend class ObIOService;
  friend class ObIOUsage;
  const char *get_io_data_buf();  // get data buf for MEMCPY before io_buf recycle
  int calc_io_offset_and_size_();
public:
  common::LinkTask req_node_;
  ObIOResult *io_result_;
protected:
  bool is_inited_;
  volatile int32_t ref_cnt_;
  std::optional<rust::Box<storage_io::RequestSlot>> rust_request_;
  std::optional<rust::Box<storage_io::RequestLifetime>> rust_lifetime_;
  std::optional<rust::Box<storage_io::CompletionSlot>> rust_completion_;
  uint64_t registration_tag_ = 0;
  uint64_t rust_id_ = 0;
  int64_t align_size_; // align io size, use this and don't use calc_io_offset_and_size_()
  int64_t align_offset_;
  
  ObIOService *io_service_;
  std::shared_ptr<ObIOAllocator> allocation_owner_;
  ObIOFd fd_;
  ObCurTraceId::TraceId trace_id_;
};

class ObIOHandle final
{
public:
  ObIOHandle();
  ~ObIOHandle();
  ObIOHandle(const ObIOHandle &other);
  ObIOHandle &operator=(const ObIOHandle &other);
  int set_result(ObIOResult &result);
  bool is_empty() const;
  bool is_valid() const;
  OB_INLINE bool is_finished() const
  {
    return nullptr != result_ && result_->is_finished_;
  }

  int wait(const int64_t wait_timeout_ms = UINT64_MAX);
  const char *get_buffer();
  int64_t get_data_size() const;
  int64_t get_rt() const;
  int get_fs_errno(int &io_errno) const;
  void reset();
  int get_io_ret() const;
  int get_io_flag(ObIOFlag &flag) const;
  int get_io_time_us(int64_t &io_time_us) const;
  int check_is_finished(bool &is_finished);
  void clear_io_callback();
  ObIOCallback *get_io_callback();
  TO_STRING_KV_WITH_HELPER("io_result", helper.convert(result_));

private:
  void estimate();

private:
  ObIOResult *result_;
};

struct ObIOServiceConfig final
{
public:
  struct ResourceConfig
  {
    ResourceConfig();
    ResourceConfig(const share::ObServerResourceConfig &resource_config);
    bool is_valid() const;
    TO_STRING_KV(K_(min_iops), K_(max_iops), K_(weight), K_(max_net_bandwidth), K_(net_bandwidth_weight));
    int64_t min_iops_;
    int64_t max_iops_;
    int64_t weight_;
    int64_t max_net_bandwidth_;
    int64_t net_bandwidth_weight_;
  };

  struct GroupConfig
  {
  public:
    GroupConfig();
    ~GroupConfig();
    bool is_valid() const;
    TO_STRING_KV(K_(mode), K_(deleted), K_(cleared), K_(min_percent), K_(max_percent), K_(weight_percent));

  public:
    bool deleted_;  // flag indicating that the group has been deleted
    bool cleared_;  // flag indicating that the group has been cleared, will be reset with new directive
    char group_name_[common::OB_MAX_RESOURCE_PLAN_NAME_LENGTH];
    int64_t min_percent_;
    int64_t max_percent_;
    int64_t weight_percent_;
    int64_t group_id_;
    ObIOMode mode_;
  };

  struct ParamConfig
  {
  public:
    ParamConfig();
    ~ParamConfig();
    bool is_valid() const;
    TO_STRING_KV(K_(memory_limit), K_(callback_thread_count));

  public:
    int64_t memory_limit_;
    int64_t callback_thread_count_;
    int64_t io_buffer_limit_;
  };

public:
  ObIOServiceConfig();
  ~ObIOServiceConfig();
  void destroy();
  static const ObIOServiceConfig &default_instance();
  bool is_valid() const;
  int deep_copy(const ObIOServiceConfig &other_config);
  int calc_group_config(const uint64_t index, int64_t &min, int64_t &max, int64_t &weight) const;
  int64_t get_callback_thread_count() const;
  int64_t effective_probe_buffer_limit() const;
  int64_t to_string(char *buf, const int64_t buf_len) const;

public:
  struct DeviceResource {
    uint64_t id_;
    int64_t depth_;
    int64_t probe_depth_;
    int64_t max_probe_bytes_;
    TO_STRING_KV(K_(id), K_(depth), K_(probe_depth), K_(max_probe_bytes));
  };
  ObSEArray<DeviceResource, 4> devices_;
  int64_t request_limit_ = 10000;
  int64_t probe_request_limit_ = 100;
  // Configured floor; registered device requirements can raise the effective reserve.
  int64_t probe_buffer_limit_ = 2L * 1024L * 1024L;
  int64_t health_warning_tolerance_us_ = 5L * 1000L * 1000L;
  int64_t health_blacklist_interval_us_ = 60L * 1000L * 1000L;
  int64_t health_wait_cap_us_ = 120L * 1000L * 1000L;
  ResourceConfig resource_config_;
  typedef ObSEArray<GroupConfig, GROUP_START_NUM * (static_cast<uint64_t>(ObIOMode::MAX_MODE) + 1)> ObIOGroupConfigArray;
  ObIOGroupConfigArray group_configs_;
  bool group_config_change_;
  ParamConfig param_config_;
};

}  // namespace common
}  // namespace oceanbase

#endif  // OCEANBASE_LIB_STORAGE_IO_DEFINE
