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

#define USING_LOG_PREFIX COMMON

#include "ob_io_struct.h"
#include "share/ob_io_device_helper.h"
#include "share/ob_local_device.h"
#include <limits>
#include <new>
#include <stdexcept>
#include "share/ob_force_print_log.h"
#include "lib/objectpool/ob_concurrency_objpool.h"
#include "lib/random/ob_random.h"
#include "lib/utility/ob_sort.h"
#include "lib/utility/ob_tracepoint.h"


#ifdef _WIN32

static inline void usage_to_timeval(FILETIME *ft, struct timeval *tv)
{
    ULARGE_INTEGER time;
    time.LowPart = ft->dwLowDateTime;
    time.HighPart = ft->dwHighDateTime;
    
    // Convert 100-nanosecond intervals to seconds and microseconds
    tv->tv_sec = (long)(time.QuadPart / 10000000);
    tv->tv_usec = (long)((time.QuadPart % 10000000) / 10);
}

#ifndef _WIN32
static inline int getrusage(int who, struct rusage *usage)
#else
inline int getrusage(int who, struct rusage *usage)
#endif
{
    FILETIME ctime, etime, stime, utime;
    
    memset(usage, 0, sizeof(struct rusage));
    
    if (who == RUSAGE_SELF) {
        PROCESS_MEMORY_COUNTERS pmc;
        HANDLE proc = GetCurrentProcess();
        
        if (!GetProcessTimes(proc, &ctime, &etime, &stime, &utime)) {
            return -1;
        }
        if (!GetProcessMemoryInfo(proc, &pmc, sizeof(pmc))) {
            return -1;
        }
        
        usage_to_timeval(&stime, &usage->ru_stime);
        usage_to_timeval(&utime, &usage->ru_utime);
        
        usage->ru_majflt = pmc.PageFaultCount;
        usage->ru_maxrss = pmc.PeakWorkingSetSize / 1024;
        
        return 0;
    } else if (who == RUSAGE_THREAD) {
        if (!GetThreadTimes(GetCurrentThread(), &ctime, &etime, &stime, &utime)) {
            return -1;
        }
        
        usage_to_timeval(&stime, &usage->ru_stime);
        usage_to_timeval(&utime, &usage->ru_utime);
        
        return 0;
    }
    
    return -1;
}
#endif

using namespace oceanbase::lib;
using namespace oceanbase::common;

int oceanbase::common::convert_storage_error(int error)
{
  switch (error) {
    case 0: return OB_SUCCESS;
    case EINTR: return OB_CANCELED;
    case EINVAL: return OB_INVALID_ARGUMENT;
    case ENOMEM: return OB_ALLOCATE_MEMORY_FAILED;
    case ENOSYS: return OB_NOT_SUPPORTED;
    case EBADF: return OB_NOT_INIT;
    case EPIPE: return OB_STATE_NOT_MATCH;
    default: return oceanbase::share::ObIODeviceLocalFileOp::convert_sys_errno(error);
  }
}

void switch_check_io_hang_errsim()
{
  oceanbase::storage_io::toggle_worker_fault_delay();
}

/******************             IOConfig              **********************/

ObIOConfig::ObIOConfig()
{
  set_default_value();
}

ObIOConfig::~ObIOConfig()
{

}


void ObIOConfig::set_default_value()
{
  read_failure_black_list_interval_ = 60 * 1000 * 1000; // Cooperate with the adjustment of tolerance_time to 1min
  data_storage_warning_tolerance_time_ = 5L * 1000L * 1000L; // 5s, same as parameter seed
  disk_io_thread_count_ = 8;
  sync_io_thread_count_ = 0;
  data_storage_io_timeout_ms_ = 120L * 1000L; // 120s
}

bool ObIOConfig::is_valid() const
{
  return read_failure_black_list_interval_ > 0
      && data_storage_warning_tolerance_time_ >= 1000L * 1000L
      && data_storage_warning_tolerance_time_ <= 300L * 1000L * 1000L
      && disk_io_thread_count_ > 0 && disk_io_thread_count_ <= MAX_IO_THREAD_COUNT
      && sync_io_thread_count_ >= 0 && sync_io_thread_count_ <= MAX_SYNC_IO_THREAD_COUNT
      && data_storage_io_timeout_ms_ > 0;
}


/******************             IOAllocator              **********************/
ObIOAllocator::ObIOAllocator()
  : is_inited_(false),
    memory_limit_(0),
    inner_allocator_()
{

}

ObIOAllocator::~ObIOAllocator()
{
  destroy();
}

int ObIOAllocator::init(const int64_t memory_limit)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
  } else if (OB_UNLIKELY(memory_limit <= 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(inner_allocator_.init(OB_MALLOC_MIDDLE_BLOCK_SIZE,
                                           ObModIds::OB_IO_CONTROL,
                                           memory_limit))) {
  } else if(FALSE_IT(inner_allocator_.set_nway(16))) {
  } else {
    memory_limit_ = memory_limit;
    is_inited_ = true;
  }
  if (OB_UNLIKELY(!is_inited_)) {
    destroy();
  }
  return ret;
}

void ObIOAllocator::destroy()
{
  is_inited_ = false;
  inner_allocator_.destroy();
}

void ObIOAllocator::commit_memory_limit(int64_t memory_limit)
{
  inner_allocator_.set_total_limit(memory_limit);
  memory_limit_ = memory_limit;
}

int64_t ObIOAllocator::get_allocated_size() const
{
  return inner_allocator_.allocated();
}

void *ObIOAllocator::alloc(const int64_t size, const lib::ObMemAttr &attr)
{
  UNUSED(attr);
  return alloc(size);
}

void *ObIOAllocator::alloc(const int64_t size)
{
  int ret = OB_SUCCESS;
  void *ret_buf = nullptr;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(size <= 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    // get buf from normal allocator
    if (OB_ISNULL(ret_buf)) {
      if (OB_ISNULL(ret_buf = inner_allocator_.alloc(size))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      }
    }
  }
  return ret_buf;
}

void ObIOAllocator::free(void *ptr)
{
  inner_allocator_.free(ptr);
}

/******************             IOUsage              **********************/

int ObIOUsage::init(const int64_t group_num)
{
  const int64_t modes = static_cast<int64_t>(ObIOGroupMode::MODECNT);
  if (group_num < 1 || group_num > INT64_MAX / modes
      || static_cast<uint64_t>(group_num) > SIZE_MAX / modes) { return OB_INVALID_CONFIG; }
  const int ret = statistics_->initialize_statistics(group_num * modes, ObTimeUtility::fast_current_time());
  if (OB_SUCCESS == ret) { lane_count_ = group_num * modes; }
  return ret;
}

void ObIOUsage::accumulate(ObIORequest &req)
{
  int ret = OB_SUCCESS;
  int64_t prepare_delay = 0;
  int64_t schedule_delay = 0;
  int64_t submit_delay = 0;
  int64_t device_delay = 0;
  int64_t total_delay = 0;
  if (OB_ISNULL(req.io_result_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("invalid io result", K(req));
  } else if (OB_FAIL(req.io_result_->cal_delay_us(
                 prepare_delay, schedule_delay, submit_delay, device_delay, total_delay))) {
  } else {
    storage_io::CompletionEventDto event{};
    event.lane = req.io_result_->get_io_usage_index();
    event.success = req.io_result_->time_log_.return_ts_ > 0 && req.io_result_->ret_code_.io_ret_ == 0;
    event.bytes = req.get_align_size();
    event.prepare_us = prepare_delay;
    event.schedule_us = schedule_delay;
    event.submit_us = submit_delay;
    event.device_us = device_delay;
    event.total_us = total_delay;
    if (OB_FAIL(statistics_->record_completion(event))) {
      LOG_WARN("failed to record io statistics", K(ret), K(req));
    }
  }
}

int ObIOUsage::snapshot(ObIOUsageInfoArray &output, storage_io::StatisticsSampleMode mode) const
{
  int ret = OB_SUCCESS;
  output.reset();
  if (OB_FAIL(output.reserve(lane_count_))) { return ret; }
  auto snapshot = storage_io::new_statistics_snapshot();
  if (OB_FAIL(statistics_->statistics_snapshot(ObTimeUtility::fast_current_time(), mode, *snapshot))) {
    return ret;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < lane_count_; ++i) {
    const auto rates = snapshot->statistics_at(i);
    if (OB_FAIL(rates.error)) { break; }
    ObIOUsageInfo info;
    info.avg_iops_ = rates.iops;
    info.avg_byte_ = rates.average_bytes;
    info.avg_prepare_delay_us_ = rates.prepare_us;
    info.avg_schedule_delay_us_ = rates.schedule_us;
    info.avg_submit_delay_us_ = rates.submit_us;
    info.avg_device_delay_us_ = rates.device_us;
    info.avg_total_delay_us_ = rates.total_us;
    if (OB_FAIL(output.push_back(info))) { break; }
  }
  return ret;
}

oceanbase::storage_io::CompletionRatesDto ObIOUsage::sample_failure(uint64_t index) const
{
  return statistics_->sample_failed_statistics(index, ObTimeUtility::fast_current_time());
}

/******************             IOChannel              **********************/
ObDeviceChannel::ObDeviceChannel()
  : is_inited_(false), threads_inited_(false), stopping_(false),
    device_handle_(nullptr), worker_threads_(0)
{}

ObDeviceChannel::~ObDeviceChannel() { destroy(); }

int64_t ObDeviceChannel::worker_thread_count(int64_t configured)
{
  if (configured > 0) { return configured; }
  const int64_t cpus = get_cpu_count();
  return cpus <= 16 ? cpus * 6 : std::min<int64_t>(96 + (cpus - 16) * 2, 1024);
}

int ObDeviceChannel::init(ObIODevice *device_handle, int64_t async_channel_count,
    int64_t sync_channel_count, int64_t max_io_depth, ObIOService &service)
{
  int ret = OB_SUCCESS;
  if (is_inited_) { return OB_INIT_TWICE; }
  if (nullptr == device_handle || !device_handle->is_local_device()
      || async_channel_count < 0 || sync_channel_count < 0 || max_io_depth < 2) {
    return OB_INVALID_ARGUMENT;
  }
  device_handle_ = device_handle;
  worker_threads_ = worker_thread_count(sync_channel_count);
  native_aio_ = device_handle->supports_native_aio();
  stopping_.store(false);
  static std::atomic<uint64_t> next_device{1};
  device_id_ = next_device.load();
  do {
    if (device_id_ == UINT64_MAX) { return OB_SIZE_OVERFLOW; }
  } while (!next_device.compare_exchange_weak(device_id_, device_id_ + 1));
  storage_io::DeviceOptions options{};
  options.id = device_id_;
  options.worker_threads = std::min(worker_threads_, max_io_depth);
  options.backend_capacity = max_io_depth;
  options.native_aio = native_aio_;
  endpoint_.emplace(storage_io::new_device_handle());
  const int64_t probe_bytes = std::max<int64_t>(2L * 1024L * 1024L,
      static_cast<share::ObLocalDevice *>(device_handle)->get_block_size());
  try { pending_.reserve(static_cast<size_t>(max_io_depth)); }
  catch (const std::bad_alloc &) { ret = OB_ALLOCATE_MEMORY_FAILED; }
  catch (const std::length_error &) { ret = OB_SIZE_OVERFLOW; }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(set_thread_count(1))) {
    } else if (OB_FAIL(lib::Threads::init())) {
    } else {
      threads_inited_ = true;
      ret = lib::Threads::start();
    }
  }
  if (OB_SUCC(ret)) {
    ret = service.register_device(options, max_io_depth, probe_bytes, **endpoint_);
  }
  if (OB_SUCC(ret)) {
    {
      std::lock_guard<std::mutex> guard(mutex_);
      is_inited_ = true;
      ready_ = true;
    }
    ready_condition_.notify_one();
  }
  if (OB_FAIL(ret)) { destroy(); }
  return ret;
}

void ObDeviceChannel::stop()
{
  bool close = false;
  {
    std::lock_guard<std::mutex> guard(mutex_);
    close = !stopping_.exchange(true) && ready_;
  }
  ready_condition_.notify_one();
  if (close) { (*endpoint_)->close_device(); }
  lib::Threads::stop();
}

void ObDeviceChannel::destroy()
{
  stop();
  if (threads_inited_) {
    lib::Threads::wait();
    lib::Threads::destroy();
    threads_inited_ = false;
  }
  endpoint_.reset();
  ready_ = false;
  is_inited_ = false;
  device_handle_ = nullptr;
}

int ObDeviceChannel::add_benchmark_target(const ObIOFd &fd,
    storage_io::BenchmarkPreparationSlot &preparation, int64_t length)
{
  if (!is_inited_ || stopping_.load()) { return OB_NOT_INIT; }
  if (fd.device_handle_ != device_handle_) { return OB_INVALID_ARGUMENT; }
  return static_cast<share::ObLocalDevice *>(device_handle_)->add_benchmark_target(
      fd, device_id_, length, preparation);
}

int ObDeviceChannel::begin(ObIORequest &req)
{
  if (!is_inited_ || stopping_.load()) { return OB_NOT_INIT; }
  if (req.fd_.device_handle_ != device_handle_ || nullptr == req.io_result_) { return OB_INVALID_ARGUMENT; }
  if (req.get_flag().is_write() && (req.get_align_offset() != req.io_result_->offset_
      || req.get_align_size() != req.io_result_->size_)) { return OB_INVALID_ARGUMENT; }
  if (req.rust_request_ || req.rust_lifetime_ || req.io_result_->rust_control_) { return OB_INIT_TWICE; }
  storage_io::RequestOptions options{};
  options.mode = req.get_flag().is_read() ? storage_io::RequestMode::Read : storage_io::RequestMode::Write;
  options.group = req.io_result_->group_id_;
  options.probe = req.get_flag().is_detect();
  options.observe_health = req.get_flag().is_read() && !req.get_flag().is_sync() && !options.probe;
  options.worker_required = req.get_flag().is_sync();
  options.rate_exempt = req.get_flag().is_unlimited();
  options.offset = req.get_align_offset();
  options.length = req.get_align_size();
  options.alignment = req.io_result_->aligned_size_;
  options.window_start = req.io_result_->offset_ - req.get_align_offset();
  options.window_length = req.io_result_->size_;
  options.timeout_us = req.get_remained_io_timeout_us();
  if (options.timeout_us == 0) { return OB_TIMEOUT; }
  const bool native = native_aio_ && !options.worker_required;
  options.worker_fault = native ? 0 : (OB_E(EventTable::EN_IO_HANG_ERROR) OB_SUCCESS);
  options.retry_alignment = native ? req.io_result_->aligned_size_ : 0;
  options.max_error_retries = native ? 10 : 0;
  options.full_buffer = nullptr != req.io_result_->io_callback_;
  static std::atomic<uint64_t> next_tag{1};
  options.tag = next_tag.load();
  do {
    if (options.tag == UINT64_MAX) { return OB_SIZE_OVERFLOW; }
  } while (!next_tag.compare_exchange_weak(options.tag, options.tag + 1));
  req.rust_lifetime_.emplace(storage_io::new_request_lifetime());
  req.rust_request_.emplace(storage_io::new_request_slot());
  req.io_result_->rust_control_.emplace(storage_io::new_cancel_handle());
  const int error = (*endpoint_)->begin_request(options, **req.rust_request_, **req.io_result_->rust_control_, **req.rust_lifetime_);
  if (error != 0) { return convert_storage_error(error); }
  req.registration_tag_ = options.tag;
  return static_cast<share::ObLocalDevice *>(device_handle_)->bind_request_target(req.fd_, **req.rust_request_, **req.rust_lifetime_);
}

int ObDeviceChannel::prepare_reserved(ObIORequest &req)
{
  rust::Slice<const uint8_t> bytes{};
  if (req.get_flag().is_write()) {
    bytes = {reinterpret_cast<const uint8_t *>(req.io_result_->buf_), static_cast<size_t>(req.io_result_->size_)};
  }
  const int error = (*req.rust_request_)->prepare_reserved(bytes);
  return error ? convert_storage_error(error) : OB_SUCCESS;
}

int ObDeviceChannel::register_pending(ObIORequest &req)
{
  std::lock_guard<std::mutex> guard(mutex_);
  if (!is_inited_ || stopping_.load()) { return OB_NOT_INIT; }
  try {
    if (!pending_.emplace(req.registration_tag_, &req).second) { return OB_ERR_UNEXPECTED; }
  } catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED; }
  req.inc_ref();
  return OB_SUCCESS;
}

void ObDeviceChannel::unregister_pending(ObIORequest &req)
{
  bool removed = false;
  {
    std::lock_guard<std::mutex> guard(mutex_);
    removed = pending_.erase(req.registration_tag_) != 0;
  }
  if (removed) { req.dec_ref(); }
}

void ObDeviceChannel::consume(rust::Box<storage_io::CompletionSlot> completion)
{
  int ret = OB_SUCCESS;
  const auto info = completion->completion_info();
  ObIORequest *req = nullptr;
  {
    std::lock_guard<std::mutex> guard(mutex_);
    const auto found = pending_.find(info.tag);
    if (found != pending_.end()) { req = found->second; pending_.erase(found); }
  }
  if (nullptr == req) {
    LOG_ERROR("unregistered storage completion", K(info.tag), K(info.id));
    return;
  }
  RequestHolder holder(req);
  req->dec_ref();
  using storage_io::CompletionCode;
  switch (info.code) {
    case CompletionCode::Complete: break;
    case CompletionCode::ShortIo: ret = OB_DATA_OUT_OF_RANGE; break;
    case CompletionCode::IncompleteEnvelope: ret = OB_NOT_SUPPORTED; break;
    case CompletionCode::TimedOut: ret = OB_TIMEOUT; break;
    case CompletionCode::Cancelled:
    case CompletionCode::Shutdown: ret = OB_CANCELED; break;
    default: ret = OB_IO_ERROR; break;
  }
  {
    ObThreadCondGuard guard(req->io_result_->get_cond());
    if (OB_SUCCESS != guard.get_ret()) { ret = guard.get_ret(); }
    req->rust_completion_.emplace(std::move(completion));
    req->io_result_->complete_size_ = info.transferred;
    req->io_result_->time_log_.return_ts_ = ObTimeUtility::fast_current_time();
  }
  if (OB_SUCCESS == ret) {
    if (OB_SUCCESS != (ret = req->io_service_->complete_request(*req))) {
      req->free_io_buffer();
      req->io_result_->finish(ObIORetCode(ret, info.os_error), req);
    }
  } else {
    req->recycle_buffer();
    req->free_io_buffer();
    req->io_result_->finish(ObIORetCode(ret, info.os_error), req);
  }
}

void ObDeviceChannel::run1()
{
  int ret = OB_SUCCESS;
  set_thread_name("IOComplete", get_thread_idx());
  {
    std::unique_lock<std::mutex> guard(mutex_);
    ready_condition_.wait(guard, [this] { return ready_ || stopping_.load(); });
    if (!ready_) { return; }
  }
  auto &endpoint = **endpoint_;
  auto completion = storage_io::new_completion_slot();
  while (true) {
    {
      std::lock_guard<std::mutex> guard(mutex_);
      if (stopping_ && pending_.empty()) { break; }
    }
    const int error = endpoint.poll_completion(10000, *completion);
    if (error == 0) {
      consume(std::move(completion));
      completion = storage_io::new_completion_slot();
    } else if (error != EAGAIN) {
      LOG_ERROR("storage completion consumer failed", K(error));
      ob_usleep(1000);
    }
  }
}

void ObDeviceChannel::print_status()
{
  std::lock_guard<std::mutex> guard(mutex_);
  if (!is_inited_ || stopping_.load()) { return; }
  LOG_INFO("storage completion registry", K(device_id_), "pending", pending_.size());
}

int ObDeviceChannel::reload_config(const ObIOConfig &conf)
{
  std::lock_guard<std::mutex> guard(mutex_);
  return worker_thread_count(conf.sync_io_thread_count_) == worker_threads_ ? OB_SUCCESS : OB_NOT_SUPPORTED;
}

ObIOCallbackManager::ObIOCallbackManager()
  : is_inited_(false),
    config_thread_count_(0)
{

}

ObIOCallbackManager::~ObIOCallbackManager()
{
  destroy();
}

int ObIOCallbackManager::init(const int64_t thread_count,
                              const int32_t queue_depth)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
  } else if (OB_UNLIKELY(thread_count <= 0 || queue_depth <= 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    config_thread_count_ = thread_count;
    if (OB_FAIL(ObLinkQueueThreadPool::set_thread_count(thread_count))) {
    } else if (OB_FAIL(ObLinkQueueThreadPool::init(thread_count, queue_depth, "DiskCB"))) {
    } else {
      is_inited_ = true;
    }
  }
  if (OB_UNLIKELY(!is_inited_)) {
    destroy();
  }
  return ret;
}

void ObIOCallbackManager::destroy()
{
  if (is_inited_) {
    ObLinkQueueThreadPool::destroy();
    config_thread_count_ = 0;
    is_inited_ = false;
  }
}

int ObIOCallbackManager::complete_request(ObIORequest &req)
{
  int ret = OB_SUCCESS;
  bool process_inline = false;
  const int64_t current_ts = ObTimeUtility::current_time();
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(current_ts > req.timeout_ts())) {
    ret = OB_TIMEOUT;
  } else if (OB_NOT_NULL(req.io_result_)) {
    ObThreadCondGuard guard(req.io_result_->get_cond());
    if (OB_FAIL(guard.get_ret())) {
    } else if (req.is_canceled()) {
      ret = OB_CANCELED;
    } else if (nullptr == req.get_callback()) {
      process_inline = true;
    } else {
      req.inc_ref(); // ref for callback queue
      if (OB_NOT_NULL(req.io_result_)) {
        req.io_result_->time_log_.callback_enqueue_ts_ = ObTimeUtility::fast_current_time();
      }
      // Create a wrapper task
      if (OB_FAIL(ObLinkQueueThreadPool::push(&req.req_node_))) {
        req.dec_ref(); // ref for callback queue
      }
    }
  }
  if (OB_SUCC(ret) && process_inline) {
    process_completion(req);
  }
  return ret;
}

void ObIOCallbackManager::handle(LinkTask *task)
{
  ObIORequest *req = CONTAINER_OF(task, ObIORequest, req_node_);
  RequestHolder holder(req);
  ObTraceIDGuard trace_guard(req->trace_id_);
  int ret = OB_SUCCESS;
  if (OB_ISNULL(req)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    req->dec_ref(); // ref for callback queue
    if (OB_NOT_NULL(req->io_result_)) {
      req->io_result_->time_log_.callback_dequeue_ts_ = ObTimeUtility::fast_current_time();
      const int64_t time_in_queue = req->io_result_->time_log_.callback_dequeue_ts_
                                  - req->io_result_->time_log_.callback_enqueue_ts_;
      if (time_in_queue > 1000LL * 1000LL) { // 1000ms
        LOG_WARN("callback dequeue too late",
            K(time_in_queue), K(get_queue_num()), KPC(req));
      }
    }
    if (TC_REACH_TIME_INTERVAL(1000LL * 1000LL)) {  // 1000ms
      LOG_INFO("callback manager call handle", K(get_queue_num()), KPC(req));
    }
    process_completion(*req);
  }
}

void ObIOCallbackManager::process_completion(ObIORequest &req)
{
  ObTraceIDGuard trace_guard(req.trace_id_);
  const int64_t begin_time = ObTimeUtility::fast_current_time();
  int ret = OB_SUCCESS;
  bool free_raw_buf = true;
  if (OB_ISNULL(req.io_result_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObTimeGuard time_guard("cb_process", 100000); //100ms
    ObThreadCondGuard guard(req.io_result_->get_cond());
    if (OB_FAIL(guard.get_ret())) {
    } else if (FALSE_IT(time_guard.click("callback_guard"))) {
    } else if (req.is_canceled()) {
      // canceled, do nothing
    } else if (OB_UNLIKELY(begin_time > req.timeout_ts())) {
      ret = OB_TIMEOUT;
    } else if (OB_FAIL(req.io_result_->ret_code_.io_ret_)) {
      //failed, ignore
    } else if (!req.can_callback()) {
      if (!req.io_result_->flag_.is_detect()) {
        if (nullptr == req.io_result_->user_data_buf_) {
          free_raw_buf = false;
        } else if (!req.is_canceled() && nullptr != req.get_io_data_buf()) {
          MEMCPY(req.io_result_->user_data_buf_, req.get_io_data_buf(), req.get_data_size());
        }
      }
    } else {
      if (nullptr != req.get_callback()) {
        if (OB_FAIL(req.get_callback()->process(req.get_io_data_buf(), req.io_result_->size_))) {
        }
        time_guard.click("callback_process");
      }
    }
    if (OB_UNLIKELY(time_guard.get_diff() > 100000)) {// 100ms
      LOG_INFO("callback process cost too much time", K(ret), K(time_guard), K(req));
    }
  }
  if (free_raw_buf) {
    req.free_io_buffer();
  }
  req.io_result_->finish(ret, &req);
}

int ObIOCallbackManager::prepare_thread_count(const int64_t thread_count)
{
  if (!is_inited_) { return OB_NOT_INIT; }
  if (thread_count <= 0) { return OB_INVALID_ARGUMENT; }
  return ObSimpleThreadPoolDynamicMgr::get_instance().bind(this);
}

void ObIOCallbackManager::commit_thread_count(const int64_t thread_count)
{
  commit_bound_thread_count(thread_count);
  ATOMIC_STORE(&config_thread_count_, thread_count);
}

int64_t ObIOCallbackManager::to_string(char *buf, const int64_t len) const
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  if (OB_FAIL(databuff_printf(buf, len, pos,
                              "IO_CALLBACK: configured_max=%ld, workers=%ld, queue_count=%ld",
                              configured_thread_count(), get_thread_count(), get_queue_num()))) {
    // do nothing
  }
  UNUSED(ret);
  return pos;
}

const char *oceanbase::common::device_health_status_to_str(const ObDeviceHealthStatus dhs)
{
  const char *hstr = "UNKNOWN";
  switch (dhs) {
    case DEVICE_HEALTH_NORMAL:
      hstr = "NORMAL";
      break;
    case DEVICE_HEALTH_WARNING:
      hstr = "WARNING";
      break;
    default:
      hstr = "UNKNOWN";
      break;
  }
  if (STRLEN(hstr) > OB_MAX_DEVICE_HEALTH_STATUS_STR_LENGTH) {
    LOG_ERROR_RET(OB_INVALID_ARGUMENT, "invalid device health status str", K(hstr),
        K(OB_MAX_DEVICE_HEALTH_STATUS_STR_LENGTH), K(dhs));
  }
  return hstr;
}

/******************             IOFaultDetector              **********************/

ObIOTracer::ObIOTracer()
  : is_inited_(false), trace_map_()
{

}

ObIOTracer::~ObIOTracer()
{
  destroy();
}

int ObIOTracer::init()
{
  int ret = OB_SUCCESS;
  const ObMemAttr attr("io_trace_map");
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
  } else if (OB_FAIL(trace_map_.create(1009, attr))) {
  } else {
    is_inited_ = true;
  }
  if (OB_UNLIKELY(!is_inited_)) {
    destroy();
  }
  return ret;
}

void ObIOTracer::destroy()
{
  trace_map_.destroy();
  is_inited_ = false;
}

void ObIOTracer::reuse()
{
  int tmp_ret = OB_SUCCESS;
  if (OB_TMP_FAIL(trace_map_.reuse())) {
    LOG_WARN_RET(tmp_ret, "reuse trace map failed", K(tmp_ret));
  }
}

ObIOTracer::RefLog::RefLog()
  : click_count_(0)
{
  memset(click_str_, 0, sizeof(click_str_));
}

void ObIOTracer::RefLog::click(const char *mod)
{
  int64_t old_index = ATOMIC_FAA(&click_count_, 1);
  if (OB_LIKELY(old_index < MAX_CLICK_COUNT)) {
    click_str_[old_index] = mod;
  }
}

ObIOTracer::TraceInfo::TraceInfo()
  : ref_log_()
{
  memset(bt_str_, 0, sizeof(bt_str_));
}

uint64_t ObIOTracer::TraceInfo::hash() const
{
  return murmurhash(bt_str_, sizeof(bt_str_), 0);
}

bool ObIOTracer::TraceInfo::operator== (const TraceInfo &param) const
{
  return 0 == memcmp(bt_str_, param.bt_str_, sizeof(bt_str_));
}

int ObIOTracer::trace_request(const ObIORequest *req, const char *msg, const TraceType trace_type)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(nullptr == req)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (ObIOTracer::TraceType::IS_FIRST == trace_type) {
    TraceInfo trace_info;
    char *bt_str = lbt();
    strncpy(trace_info.bt_str_, bt_str, sizeof(trace_info.bt_str_) - 1);
    trace_info.ref_log_.click(msg);
    if (OB_FAIL(trace_map_.set_refactored(reinterpret_cast<int64_t>(req), trace_info))) {
    }
  } else {
    struct ModifyFn {
      ModifyFn(const char *msg) : msg_(msg) {}
      int operator () (hash::HashMapPair<int64_t, TraceInfo> &entry) {
        entry.second.ref_log_.click(msg_);
        return OB_SUCCESS;
      }
      const char *msg_;
    };
    ModifyFn modify_fn(msg);
    if (OB_FAIL(trace_map_.atomic_refactored(reinterpret_cast<int64_t>(req), modify_fn))) {
    }
    if (OB_SUCC(ret) && ObIOTracer::TraceType::IS_LAST == trace_type) {
      if (OB_FAIL(trace_map_.erase_refactored(reinterpret_cast<int64_t>(req)))) {
      }
    }
  }
  return ret;
}

int64_t ObIOTracer::to_string(char *buf, const int64_t len) const
{
  struct UpdateFn {
    int operator () (hash::HashMapPair<TraceInfo, int64_t> &entry) {
      ++entry.second;
      return OB_SUCCESS;
    }
  };

  struct CountFn {
    CountFn() : req_count_(0) {}
    int init() { return bt_count_.create(97, "count_fn"); }
    int operator () (hash::HashMapPair<int64_t, TraceInfo> &entry) {
      int ret = OB_SUCCESS;
      ++req_count_;
      if (OB_FAIL(bt_count_.set_refactored(entry.second, 1))) {
        if (OB_HASH_EXIST == ret) {
          UpdateFn update_fn;
          if (OB_FAIL(bt_count_.atomic_refactored(entry.second, update_fn))) {
          }
        } else {
        }
      }
      return ret;
    }
    int64_t req_count_;
    hash::ObHashMap<TraceInfo, int64_t> bt_count_;
  };

  struct TraceItem
  {
  public:
    TraceItem() : trace_info_(), count_(0) {}
    TraceItem(const TraceInfo &trace, const int64_t count) : trace_info_(trace), count_(count) {}
    TO_STRING_KV(K(trace_info_), K(count_));
    TraceInfo trace_info_;
    int64_t count_;
  };

  struct StoreFn {
    StoreFn(ObIArray<TraceItem> &trace_array) : trace_array_(trace_array) {}
    int operator () (hash::HashMapPair<TraceInfo, int64_t> &entry) {
      TraceItem item(entry.first, entry.second);
      return trace_array_.push_back(item);
    }
    ObIArray<TraceItem> &trace_array_;
  };

  struct {
    bool operator()(const TraceItem &left, const TraceItem &right) const
    {
      return left.count_ < right.count_;
    }
  } sort_fn;

  int64_t pos = 0;
  int ret = OB_SUCCESS;
  CountFn counter;
  if (OB_FAIL(counter.init())) {
  } else if (OB_FAIL(trace_map_.foreach_refactored(counter))) {
  } else if (counter.req_count_ > 0) {
    ObArray<TraceItem> trace_array;
    StoreFn store_fn(trace_array);
    if (OB_FAIL(trace_array.reserve(counter.bt_count_.size()))) {
    } else if (OB_FAIL(counter.bt_count_.foreach_refactored(store_fn))) {
    } else {
      lib::ob_sort(trace_array.begin(), trace_array.end(), sort_fn);
      databuff_printf(buf, len, pos, "trace_request_count: %ld, distinct_backtrace_count: %ld; ", counter.req_count_, trace_array.count());
      const int64_t print_count = min(5, trace_array.count());
      for (int64_t i = 0; OB_SUCC(ret) && i < print_count; ++i) {
        const TraceItem &item = trace_array.at(i);
	{
          ObCStringHelper helper;
          databuff_printf(buf, len, pos, "top: %ld, count: %ld, ref_log: %s, backtrace: %s; ", i + 1, item.count_, helper.convert(item.trace_info_.ref_log_), item.trace_info_.bt_str_);
        }
      }
    }
  }
  return pos;
}
void ObIOTracer::print_status()
{
  LOG_INFO("[IO STATUS TRACER]", K(*this));
}
