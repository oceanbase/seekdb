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

#include "ob_io_manager.h"
#include <stdexcept>
#include <system_error>
#include "lib/ob_running_mode.h"
#include "share/ob_io_device_helper.h"
#include "share/rc/ob_server_runtime.h"

using namespace oceanbase::share;
using namespace oceanbase::lib;
using namespace oceanbase::common;

namespace oceanbase
{
namespace common
{
static int64_t checked_metric(double value)
{
  const auto result = storage_io::metric_integer(value);
  if (result.error != OB_SUCCESS) { LOG_WARN_RET(result.error, "invalid io metric conversion", K(value)); }
  return result.value;
}

}  // namespace common
}  // namespace oceanbase
ObIOManager::ObIOManager()
  : is_inited_(false),
    is_working_(false),
    server_memory_limit_(ObIORuntimeOptions::DEFAULT_SERVER_MEMORY_LIMIT),
    mutex_(ObLatchIds::GLOBAL_IO_CONFIG_LOCK),
    io_config_(),
    allocator_()
{
}

ObIOManager::~ObIOManager()
{
  destroy();
}

ObIOManager &ObIOManager::get_instance()
{
  static ObIOManager instance;
  return instance;
}

int ObIOManager::init(const int64_t memory_limit,
                      const int32_t queue_depth,
                      const int32_t schedule_thread_count)
{
  return init(ObIORuntimeOptions(), memory_limit, queue_depth, schedule_thread_count);
}

int ObIOManager::init(const ObIORuntimeOptions &runtime_options,
                      const int64_t memory_limit,
                      const int32_t queue_depth,
                      const int32_t schedule_thread_count)
{
  int ret = OB_SUCCESS;
  int64_t schedule_queue_count = 0 != schedule_thread_count ? schedule_thread_count : (lib::is_mini_mode() ? 2 : 8);
  if (OB_UNLIKELY(ATOMIC_LOAD(&is_inited_))) {
    ret = OB_INIT_TWICE;
  } else if (OB_UNLIKELY(!runtime_options.is_valid() || memory_limit <= 0 || queue_depth < 2 || schedule_queue_count <= 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(allocator_.init(OB_MALLOC_MIDDLE_BLOCK_SIZE, "IO_MGR", memory_limit))) {
  } else if (OB_ISNULL(io_service_ = OB_NEW(ObIOService, ObMemAttr("IO_MGR")))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(io_service_->init(ObIOServiceConfig::default_instance(), runtime_options.server_memory_limit_, queue_depth))) {
  } else {
    ObMemAttr attr("IO_MGR");
    allocator_.set_attr(attr);
    server_memory_limit_ = runtime_options.server_memory_limit_;
    io_config_.set_default_value();
    is_stopping_ = false;
    ATOMIC_STORE(&is_inited_, true);
  }
  if (OB_UNLIKELY(!ATOMIC_LOAD(&is_inited_))) {
    destroy();
  }
  return ret;
}

void ObIOManager::destroy()
{
  stop();
  ObIOService *service = nullptr;
  {
    ObMutexGuard guard(mutex_);
    service = io_service_;
    io_service_ = nullptr;
    ATOMIC_STORE(&is_inited_, false);
  }
  OB_DELETE(ObIOService, "IO_MGR", service);
  allocator_.destroy();
  LOG_INFO("io manager is destroyed");
}

int ObIOManager::start()
{
  std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
  int ret = OB_SUCCESS;
  ObRefHolder<ObIOService> service;
  {
    ObMutexGuard guard(mutex_);
    if (!ATOMIC_LOAD(&is_inited_)) { return OB_NOT_INIT; }
    if (is_stopping_) { return OB_STATE_NOT_MATCH; }
    service.hold(io_service_);
  }
  if (OB_FAIL(service.get_ptr()->start())) {
  } else {
    ATOMIC_STORE(&is_working_, true);
  }
  return ret;
}

void ObIOManager::stop()
{
  std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
  std::unordered_map<int64_t, std::shared_ptr<ObDeviceChannel>> channels;
  ObRefHolder<ObIOService> service;
  {
    ObMutexGuard guard(mutex_);
    ATOMIC_STORE(&is_working_, false);
    is_stopping_ = true;
    service.hold(io_service_);
    channels.swap(channel_map_);
  }
  if (service.get_ptr() != nullptr) { service.get_ptr()->stop_requests(); }
  for (auto &entry : channels) { entry.second->stop(); }
  for (auto &entry : channels) { entry.second->wait(); }
  if (service.get_ptr() != nullptr) { service.get_ptr()->stop(); }
}

void ObIOManager::wait()
{
}

bool ObIOManager::is_stopped() const
{
  return !ATOMIC_LOAD(&is_working_);
}

int ObIOManager::read(const ObIOInfo &info, ObIOHandle &handle)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(aio_read(info, handle))) {
  } else if (OB_FAIL(handle.wait())) {
    // io callback should be freed by caller
    handle.clear_io_callback();
  }
  return ret;
}

int ObIOManager::write(const ObIOInfo &info)
{
  int ret = OB_SUCCESS;
  ObIOHandle handle;
  if (OB_FAIL(aio_write(info, handle))) {
  } else if (OB_FAIL(handle.wait())) {
    // io callback should be freed by caller
    handle.clear_io_callback();
  }
  return ret;
}

int ObIOManager::aio_read(const ObIOInfo &info, ObIOHandle &handle)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!ATOMIC_LOAD(&is_inited_))) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!ATOMIC_LOAD(&is_working_))) {
    ret = OB_STATE_NOT_MATCH;
  } else if (OB_UNLIKELY(!info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(dispatch_aio(info, handle))) {
  }
  return ret;
}

int ObIOManager::aio_write(const ObIOInfo &info, ObIOHandle &handle)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!ATOMIC_LOAD(&is_inited_))) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!ATOMIC_LOAD(&is_working_))) {
    ret = OB_STATE_NOT_MATCH;
  } else if (OB_UNLIKELY(!info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(dispatch_aio(info, handle))) {
  }
  return ret;
}

int ObIOManager::pread(ObIOInfo &info, int64_t &read_size)
{
  int ret = OB_SUCCESS;
  read_size = 0;
  if (OB_UNLIKELY(!ATOMIC_LOAD(&is_inited_))) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!ATOMIC_LOAD(&is_working_))) {
    ret = OB_STATE_NOT_MATCH;
  } else if (OB_UNLIKELY(!info.is_valid() || nullptr == info.buf_)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    info.flag_.set_read();
    info.flag_.set_sync();
    info.timeout_us_ = MAX_IO_WAIT_TIME_MS * 1000;
    ObIOHandle handle;
    if (OB_FAIL(dispatch_aio(info, handle))) {
    } else {
      bool finished = false;
      while (!finished && (OB_SUCC(ret) || OB_TIMEOUT == ret || OB_IO_TIMEOUT == ret)) {
        if (OB_FAIL(handle.wait(MAX_IO_WAIT_TIME_MS))) {
          const int status = handle.check_is_finished(finished);
          if (OB_SUCCESS != status) { ret = status; }
        } else {
          break;
        }
      }
    }
    if (OB_SUCC(ret) || OB_DATA_OUT_OF_RANGE == ret) {
      read_size = handle.get_data_size();
      MEMCPY(const_cast<char *>(info.buf_), handle.get_buffer(), read_size);
    }
  }
  return ret;
}

int ObIOManager::pwrite(ObIOInfo &info, int64_t &write_size)
{
  int ret = OB_SUCCESS;
  write_size = 0;
  if (OB_UNLIKELY(!ATOMIC_LOAD(&is_inited_))) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!ATOMIC_LOAD(&is_working_))) {
    ret = OB_STATE_NOT_MATCH;
  } else if (OB_UNLIKELY(!info.is_valid() || nullptr == info.buf_)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    info.flag_.set_write();
    info.flag_.set_sync();
    info.timeout_us_ = MAX_IO_WAIT_TIME_MS * 1000;
    ObIOHandle handle;
    if (OB_FAIL(dispatch_aio(info, handle))) {
    } else {
      bool finished = false;
      while (!finished && (OB_SUCC(ret) || OB_TIMEOUT == ret || OB_IO_TIMEOUT == ret)) {
        if (OB_FAIL(handle.wait(MAX_IO_WAIT_TIME_MS))) {
          const int status = handle.check_is_finished(finished);
          if (OB_SUCCESS != status) { ret = status; }
        } else {
          break;
        }
      }
    }
    if (OB_SUCC(ret) || OB_DATA_OUT_OF_RANGE == ret) {
      write_size = handle.get_data_size();
    }
  }
  return ret;
}

int ObIOManager::dispatch_aio(const ObIOInfo &info, ObIOHandle &handle)
{
  int ret = OB_SUCCESS;
  ObRefHolder<ObIOService> service_holder;

  if (OB_FAIL(get_io_service(service_holder))) {
  } else if (OB_FAIL(service_holder.get_ptr()->inner_aio(info, handle))) {
  }
  return ret;
}

int ObIOManager::set_io_config(const ObIOConfig &conf)
{
  std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
  int ret = OB_SUCCESS;
  ObRefHolder<ObIOService> service;
  if (!ATOMIC_LOAD(&is_inited_)) { return OB_NOT_INIT; }
  if (!conf.is_valid() || conf.data_storage_io_timeout_ms_ > INT64_MAX / 1000) { return OB_INVALID_ARGUMENT; }
  if (OB_FAIL(get_io_service(service))) { return ret; }
  {
    ObMutexGuard guard(mutex_);
    for (auto &entry : channel_map_) {
      if (OB_FAIL(entry.second->reload_config(conf))) { return ret; }
    }
  }
  if (OB_FAIL(service.get_ptr()->update_health_config(conf))) { return ret; }
  {
    ObMutexGuard guard(mutex_);
    io_config_ = conf;
  }
  return ret;
}

const ObIOConfig &ObIOManager::get_io_config() const
{
  return io_config_;
}

int ObIOManager::get_device_health_status(ObDeviceHealthStatus &dhs, int64_t &device_abnormal_time)
{
  dhs = DEVICE_HEALTH_NORMAL;
  device_abnormal_time = 0;
  ObRefHolder<ObIOService> service;
  int ret = get_io_service(service);
  if (ret == OB_HASH_NOT_EXIST) { return OB_SUCCESS; }
  storage_io::HealthStatus status{};
  if (OB_SUCC(ret)) { ret = service.get_ptr()->health_status(status); }
  if (OB_SUCC(ret)) {
    dhs = status.warning ? DEVICE_HEALTH_WARNING : DEVICE_HEALTH_NORMAL;
    device_abnormal_time = status.warning_wall_us;
  }
  return ret;
}

int ObIOManager::add_device_channel(ObIODevice *device_handle,
    const int64_t async_channel_thread_count, const int64_t sync_channel_thread_count,
    const int64_t max_io_depth)
{
  std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
  int ret = OB_SUCCESS;
  std::shared_ptr<ObDeviceChannel> channel;
  ObRefHolder<ObIOService> service;
  if (!ATOMIC_LOAD(&is_inited_)) { return OB_NOT_INIT; }
  if (nullptr == device_handle || async_channel_thread_count < 0 || sync_channel_thread_count < 0
      || max_io_depth < 2) { return OB_INVALID_ARGUMENT; }
  const int64_t key = reinterpret_cast<int64_t>(device_handle);
  decltype(channel_map_) staged;
  {
    ObMutexGuard guard(mutex_);
    if (is_stopping_) { return OB_STATE_NOT_MATCH; }
    if (channel_map_.find(key) != channel_map_.end()) { return OB_HASH_EXIST; }
    service.hold(io_service_);
    try {
      channel = std::make_shared<ObDeviceChannel>();
      staged.emplace(key, channel);
      channel_map_.reserve(channel_map_.size() + 1);
    } catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED; }
    catch (const std::system_error &) { return OB_ERR_SYS; }
    catch (const std::length_error &) { return OB_SIZE_OVERFLOW; }
  }
  auto node = staged.extract(key);
  if (OB_FAIL(channel->init(device_handle, async_channel_thread_count, sync_channel_thread_count,
      max_io_depth, *service.get_ptr()))) {
  } else {
    ObMutexGuard guard(mutex_);
    channel_map_.insert(std::move(node));
  }
  return ret;
}

int ObIOManager::remove_device_channel(ObIODevice *device_handle)
{
  std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
  std::shared_ptr<ObDeviceChannel> channel;
  ObRefHolder<ObIOService> service;
  const int64_t key = reinterpret_cast<int64_t>(device_handle);
  {
    ObMutexGuard guard(mutex_);
    if (!ATOMIC_LOAD(&is_inited_)) { return OB_NOT_INIT; }
    if (nullptr == device_handle) { return OB_INVALID_ARGUMENT; }
    const auto found = channel_map_.find(key);
    if (found == channel_map_.end()) { return OB_HASH_NOT_EXIST; }
    channel = found->second;
    service.hold(io_service_);
  }
  const int ret = service.get_ptr()->remove_device(channel->device_id());
  if (ret != OB_SUCCESS) { return ret; }
  {
    ObMutexGuard guard(mutex_);
    channel_map_.erase(key);
  }
  channel->stop();
  channel->wait();
  return OB_SUCCESS;
}

int ObIOManager::begin_request(ObIORequest &req, std::shared_ptr<ObDeviceChannel> &channel)
{
  ObMutexGuard guard(mutex_);
  if (!ATOMIC_LOAD(&is_inited_)) { return OB_NOT_INIT; }
  const auto found = channel_map_.find(reinterpret_cast<int64_t>(req.fd_.device_handle_));
  if (found == channel_map_.end()) { return OB_HASH_NOT_EXIST; }
  channel = found->second;
  return channel->begin(req);
}


int ObIOManager::refresh_io_runtime_config(const ObIOServiceConfig::ResourceConfig &resources,
    const ObIOServiceConfig::ParamConfig &params)
{
  int ret = OB_SUCCESS;
  ObRefHolder<ObIOService> service;
  if (OB_FAIL(get_io_service(service))) { return ret; }
  ret = service.get_ptr()->update_runtime_config(&resources, &params);
  return ret;
}

int ObIOManager::refresh_io_resource_config(const ObIOServiceConfig::ResourceConfig &io_resource_config)
{
  int ret = OB_SUCCESS;
  ObRefHolder<ObIOService> service_holder;
  if (OB_UNLIKELY(!ATOMIC_LOAD(&is_inited_))) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!io_resource_config.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(get_io_service(service_holder))) {
  } else if (OB_FAIL(service_holder.get_ptr()->update_runtime_config(&io_resource_config, nullptr))) {
  }
  return ret;
}

int ObIOManager::refresh_io_param_config(const ObIOServiceConfig::ParamConfig &io_param_config)
{
  int ret = OB_SUCCESS;
  ObRefHolder<ObIOService> service_holder;
  if (OB_UNLIKELY(!ATOMIC_LOAD(&is_inited_))) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!io_param_config.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(get_io_service(service_holder))) {
  } else if (OB_FAIL(service_holder.get_ptr()->update_runtime_config(nullptr, &io_param_config))) {
  }
  return ret;
}

int ObIOManager::get_io_service(ObRefHolder<ObIOService> &service_holder) const
{
  ObMutexGuard guard(mutex_);
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(io_service_)) {
    service_holder.hold(io_service_);
  }
  if (OB_SUCC(ret) && OB_ISNULL(service_holder.get_ptr())) {
    ret = OB_HASH_NOT_EXIST;
  }
  return ret;
}

void ObIOManager::print_channel_status()
{
  ObMutexGuard guard(mutex_);
  for (const auto &entry : channel_map_) { entry.second->print_status(); }
}

void ObIOManager::print_status()
{
  print_service_status();
  print_channel_status();
}

/******************             IOService              **********************/

ObIOService::ObIOService()
  : is_inited_(false),
    is_working_(false),
    ref_cnt_(0),
    io_memory_limit_(0),
    request_count_(0),
    result_count_(0),
    io_allocator_(),
    callback_mgr_()
{

}

ObIOService::~ObIOService()
{
  destroy();
}

namespace {
struct MemoryPlan {
  int64_t bytes;
  int64_t requests;
  int64_t results;
};
MemoryPlan memory_plan(int64_t memory)
{
  const double gib = static_cast<double>(memory) / (1024LL * 1024LL * 1024LL);
  if (gib < 0.5) { return {256LL * 1024LL * 1024LL, 5000, 5000}; }
  if (gib <= 1) { return {256LL * 1024LL * 1024LL, 50000, 50000}; }
  if (gib <= 4) { return {1024LL * 1024LL * 1024LL, 100000, 100000}; }
  if (gib <= 8) { return {2048LL * 1024LL * 1024LL, 200000, 200000}; }
  return {memory, 300000, 300000};
}
}

int ObIOService::init(const ObIOServiceConfig &input, int64_t server_memory, int64_t queue_depth)
{
  int ret = OB_SUCCESS;
  ObIOServiceConfig config;
  storage_io::ResourceConfig native{};
  std::shared_ptr<IOConfigSnapshot> candidate;
  if (is_inited_) { return OB_INIT_TWICE; }
  if (queue_depth < 2 || server_memory <= 0) { return OB_INVALID_ARGUMENT; }
  if (OB_FAIL(config.deep_copy(input))) { return ret; }
  config.request_limit_ = queue_depth;
  config.probe_request_limit_ = std::min<int64_t>(100, std::max<int64_t>(1, queue_depth / 16));
  if (OB_FAIL(prepare_config(config, server_memory, candidate, native))) {
  } else if (OB_FAIL(init_memory_pool(config.param_config_.memory_limit_))) {
  } else if (OB_FAIL(io_sys_usage_.init(SYS_MODULE_CNT))) {
  } else {
    rust_service_.emplace(storage_io::new_service_handle());
    if (OB_FAIL(convert_storage_error((*rust_service_)->start_service(native)))) {
    } else {
      accepted_config_ = std::move(candidate);
      inc_ref();
      is_inited_ = true;
    }
  }
  if (!is_inited_) { destroy(); }
  return ret;
}

void ObIOService::destroy()
{
  int ret = OB_SUCCESS;
  stop();
  if (is_inited_) {
    while (1 != get_ref_cnt()) { ob_usleep(10L * 1000L); }
    dec_ref();
  }
  if (rust_service_) {
    const int error = (*rust_service_)->shutdown_service();
    if (error != 0) { LOG_ERROR("storage service shutdown failed", K(error)); }
    rust_service_.reset();
  }
  accepted_config_.reset();
  io_allocator_.reset();
  io_memory_limit_ = request_count_ = result_count_ = 0;
  is_inited_ = false;
}

int ObIOService::start()
{
  std::lock_guard<std::mutex> guard(config_mutex_);
  if (!is_inited_) { return OB_NOT_INIT; }
  if (is_working()) { return OB_SUCCESS; }
  const int64_t count = accepted_config_->config_.get_callback_thread_count();
  if (count > INT32_MAX / 100000) { return OB_INVALID_ARGUMENT; }
  const int ret = callback_mgr_.init(count, count * 100000);
  if (ret == OB_SUCCESS) { ATOMIC_STORE(&is_working_, true); }
  return ret;
}

void ObIOService::stop_requests()
{
  std::lock_guard<std::mutex> guard(config_mutex_);
  ATOMIC_STORE(&is_working_, false);
  if (rust_service_) { (*rust_service_)->stop_service(); }
}

void ObIOService::stop()
{
  stop_requests();
  callback_mgr_.destroy();
}

bool ObIOService::is_working() const { return ATOMIC_LOAD(&is_working_); }

int ObIOService::init_memory_pool(const int64_t memory)
{
  if (memory <= 0) { return OB_INVALID_ARGUMENT; }
  const MemoryPlan plan = memory_plan(memory);
  try { io_allocator_ = std::make_shared<ObIOAllocator>(); }
  catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED; }
  const int ret = io_allocator_->init(plan.bytes);
  if (ret == OB_SUCCESS) {
    io_memory_limit_ = plan.bytes;
    request_count_ = plan.requests;
    result_count_ = plan.results;
  }
  return ret;
}

int ObIOService::alloc_and_init_result(const ObIOInfo &info, ObIOResult *&io_result)
{
  int ret = OB_SUCCESS;
  io_result = nullptr;
  if (OB_FAIL(alloc_io_result(io_result))) {
    if (OB_ALLOCATE_MEMORY_FAILED == ret) {
      //blocking foreground thread
      ret = OB_SUCCESS;
      if (OB_FAIL(try_alloc_result_until_timeout(ObTimeUtility::current_time() + info.timeout_us_, io_result))) {
      }
    } else {
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_ISNULL(io_result)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(io_result->basic_init())) {
    }
  }

  if (OB_FAIL(ret)) {
  } else if (FALSE_IT(io_result->io_callback_ = info.callback_)) {
  } else if (OB_FAIL(io_result->init(info))) {
  }

  if (OB_FAIL(ret) && OB_NOT_NULL(io_result)) {
    io_allocator_->free(io_result);
    io_result = nullptr;
  }
  return ret;
}

int ObIOService::alloc_req_and_result(const ObIOInfo &info, ObIOHandle &handle,
    ObIORequest *&request, RequestHolder &holder)
{
  int ret = OB_SUCCESS;
  ObIOResult *result = nullptr;
  if (OB_FAIL(alloc_and_init_result(info, result))) { return ret; }
  result->inc_ref("submission");
  if (OB_FAIL(handle.set_result(*result))) {
  } else if (OB_FAIL(alloc_io_request(request))) {
    if (ret == OB_ALLOCATE_MEMORY_FAILED) {
      ret = try_alloc_req_until_timeout(result->time_log_.begin_ts_ + result->timeout_us_, request);
    }
  }
  if (OB_SUCC(ret)) {
    ObThreadCondGuard guard(result->cond_);
    if (OB_FAIL(guard.get_ret())) {
    } else if (result->is_canceled_) {
      ret = OB_CANCELED;
    } else if (OB_FAIL(request->basic_init())) {
    } else if (OB_FAIL(request->init(info, result))) {
    }
  }
  if (OB_FAIL(ret)) {
    {
      ObThreadCondGuard guard(result->cond_);
      if (OB_SUCCESS == guard.get_ret()) {
        result->io_callback_ = nullptr;
        result->callback_ownership_ = ObIOResult::CallbackOwnership::Detached;
      }
    }
    if (nullptr != request) { request->free(); request = nullptr; }
  } else {
    holder.hold(request);
  }
  result->dec_ref("submission");
  return ret;
}

int ObIOService::submit_request(ObIORequest &req)
{
  int ret = OB_SUCCESS;
  bool registered = false;
  RequestHolder request_holder(&req);
  std::optional<rust::Box<storage_io::CancelHandle>> rejected_control;
  std::shared_ptr<ObDeviceChannel> channel;
  ObIOResult &result = *req.io_result_;
  {
    ObThreadCondGuard result_guard(result.cond_);
    if (OB_FAIL(result_guard.get_ret())) {
    } else if (result.is_canceled_) {
      ret = OB_CANCELED;
    } else {
      std::lock_guard<std::mutex> config_guard(config_mutex_);
      if (!is_working()) {
        ret = OB_STATE_NOT_MATCH;
      } else {
        result.config_snapshot_ = accepted_config_;
        result.group_id_ = 0;
        result.group_index_ = static_cast<uint64_t>(ObIOMode::MAX_MODE);
        result.time_log_.enqueue_ts_ = ObTimeUtility::fast_current_time();
        ret = OB_IO_MANAGER.begin_request(req, channel);
      }
    }
  }
  if (OB_SUCC(ret)) { ret = convert_storage_error((*req.rust_request_)->wait_capacity()); }
  if (OB_SUCC(ret)) {
    ObThreadCondGuard guard(result.cond_);
    if (OB_FAIL(guard.get_ret())) {
    } else if (result.is_canceled_) {
      ret = OB_CANCELED;
    } else {
      result.time_log_.dequeue_ts_ = ObTimeUtility::fast_current_time();
      ret = channel->prepare_reserved(req);
    }
  }
  if (OB_SUCC(ret)) {
    ret = channel->register_pending(req);
    registered = ret == OB_SUCCESS;
  }
  if (OB_SUCC(ret)) {
    ObThreadCondGuard guard(result.cond_);
    if (OB_FAIL(guard.get_ret())) {
    } else if (result.is_canceled_) {
      ret = OB_CANCELED;
    } else {
      result.time_log_.submit_ts_ = ObTimeUtility::fast_current_time();
      const auto submitted = (*req.rust_request_)->publish();
      ret = convert_storage_error(submitted.error);
      if (OB_SUCC(ret)) {
        req.rust_id_ = submitted.id;
        result.callback_ownership_ = ObIOResult::CallbackOwnership::Adopted;
      }
    }
  }
  if (OB_FAIL(ret)) {
    {
      ObThreadCondGuard guard(result.cond_);
      if (OB_SUCCESS == guard.get_ret()) {
        result.io_callback_ = nullptr;
        result.callback_ownership_ = ObIOResult::CallbackOwnership::Detached;
        rejected_control.swap(result.rust_control_);
      }
    }
    if (registered) { channel->unregister_pending(req); }
    req.rust_request_.reset();
    rejected_control.reset();
    req.rust_lifetime_.reset();
  }
  return ret;
}

int ObIOService::inner_aio(const ObIOInfo &info, ObIOHandle &handle)
{
  int ret = OB_SUCCESS;
  handle.reset();
  ObIORequest *req = nullptr;
  RequestHolder req_holder;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!is_working())) {
    ret = OB_STATE_NOT_MATCH;
  } else if (OB_ISNULL(info.fd_.device_handle_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(alloc_req_and_result(info, handle, req, req_holder))) {
  } else if (OB_FAIL(submit_request(*req))) {
  }
  if (OB_FAIL(ret)) {
    // io callback should be freed by caller
    handle.clear_io_callback();
    handle.reset();
  }
  return ret;
}

int ObIOService::complete_request(ObIORequest &req)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!is_working())) {
    ret = OB_STATE_NOT_MATCH;
  } else if (OB_FAIL(callback_mgr_.complete_request(req))) {
  }
  return ret;
}
int ObIOService::prepare_config(const ObIOServiceConfig &replacement, int64_t server_memory,
    std::shared_ptr<IOConfigSnapshot> &candidate, storage_io::ResourceConfig &native)
{
  int ret = OB_SUCCESS;
  if (!replacement.is_valid() || server_memory <= 0) { return OB_INVALID_ARGUMENT; }
  try {
    candidate = std::make_shared<IOConfigSnapshot>();
    if (OB_FAIL(candidate->config_.deep_copy(replacement))) { return ret; }
    candidate->effective_server_memory_ = server_memory;
    candidate->effective_io_buffer_limit_ = replacement.param_config_.io_buffer_limit_ == 0
        ? std::max<int64_t>(1, server_memory / 5) : replacement.param_config_.io_buffer_limit_;
    candidate->effective_wrapper_limit_ = memory_plan(replacement.param_config_.memory_limit_).bytes;
    const int64_t probe_reserve = replacement.effective_probe_buffer_limit();
    if (probe_reserve >= candidate->effective_io_buffer_limit_) { return OB_INVALID_ARGUMENT; }
    native.health_warning_tolerance_us = replacement.health_warning_tolerance_us_;
    native.health_blacklist_interval_us = replacement.health_blacklist_interval_us_;
    native.health_wait_cap_us = replacement.health_wait_cap_us_;
    native.max_records = replacement.request_limit_;
    native.max_bytes = candidate->effective_io_buffer_limit_;
    native.probe_records = replacement.probe_request_limit_;
    native.probe_bytes = replacement.probe_buffer_limit_;
    for (const auto &device : replacement.devices_) {
      if (accepted_config_) {
        for (const auto &previous : accepted_config_->config_.devices_) {
          if (previous.id_ == device.id_ && device.max_probe_bytes_ < previous.max_probe_bytes_) {
            return OB_INVALID_ARGUMENT;
          }
        }
      }
      if (device.max_probe_bytes_ > probe_reserve
          || device.max_probe_bytes_ > candidate->effective_io_buffer_limit_ - probe_reserve) {
        return OB_INVALID_ARGUMENT;
      }
      native.devices.push_back(storage_io::DevicePolicy{
          device.id_, static_cast<size_t>(device.depth_), static_cast<size_t>(device.probe_depth_),
          static_cast<size_t>(device.max_probe_bytes_)});
    }
    bool reuse_usage = accepted_config_ != nullptr
        && accepted_config_->config_.group_configs_.count() == replacement.group_configs_.count();
    if (reuse_usage) {
      for (int64_t i = 0; i < replacement.group_configs_.count(); ++i) {
        const auto &old = accepted_config_->config_.group_configs_.at(i);
        const auto &next = replacement.group_configs_.at(i);
        if (old.group_id_ != next.group_id_ || old.mode_ != next.mode_) { reuse_usage = false; break; }
      }
    }
    if (reuse_usage) {
      candidate->usage_ = accepted_config_->usage_;
    } else {
      candidate->usage_ = std::make_shared<ObIOUsage>();
      if (OB_FAIL(candidate->usage_->init(replacement.group_configs_.count() / 3))) { return ret; }
    }
  } catch (const std::bad_alloc &) { ret = OB_ALLOCATE_MEMORY_FAILED; }
  return ret;
}

int ObIOService::replace_config_locked(const ObIOServiceConfig &replacement, uint64_t expected_generation,
    int64_t server_memory, ResourceConfigReceipt &receipt)
{
  int ret = OB_SUCCESS;
  receipt = ResourceConfigReceipt{};
  if (!is_inited_ || !rust_service_) { return OB_NOT_INIT; }
  receipt.generation_ = accepted_config_->generation_;
  if (expected_generation != accepted_config_->generation_) { return OB_EAGAIN; }
  std::shared_ptr<IOConfigSnapshot> candidate;
  storage_io::ResourceConfig native{};
  if (OB_FAIL(prepare_config(replacement, server_memory, candidate, native))) { return ret; }
  const int64_t callback_count = replacement.get_callback_thread_count();
  if (callback_count > INT32_MAX / 100000) { return OB_INVALID_ARGUMENT; }
  if (is_working() && OB_FAIL(callback_mgr_.prepare_thread_count(callback_count))) { return ret; }
  const auto applied = (*rust_service_)->apply_config(expected_generation, native);
  receipt.generation_ = applied.generation;
  if (applied.error != 0) { return convert_storage_error(applied.error); }
  publish_config(std::move(candidate), applied, receipt);
  return OB_SUCCESS;
}

void ObIOService::publish_config(std::shared_ptr<IOConfigSnapshot> candidate,
    const storage_io::ConfigReceipt &applied, ResourceConfigReceipt &receipt)
{
  const auto &replacement = candidate->config_;
  const int64_t callback_count = replacement.get_callback_thread_count();
  receipt.generation_ = applied.generation;
  const MemoryPlan memory = memory_plan(replacement.param_config_.memory_limit_);
  io_allocator_->commit_memory_limit(memory.bytes);
  io_memory_limit_ = memory.bytes;
  request_count_ = memory.requests;
  result_count_ = memory.results;
  if (is_working()) { callback_mgr_.commit_thread_count(callback_count); }
  candidate->generation_ = applied.generation;
  accepted_config_ = std::move(candidate);
  {
    ObMutexGuard manager_guard(OB_IO_MANAGER.mutex_);
    if (OB_IO_MANAGER.io_service_ == this) {
      ATOMIC_STORE(&OB_IO_MANAGER.server_memory_limit_, accepted_config_->effective_server_memory_);
    }
  }
  receipt.effective_io_buffer_limit_ = accepted_config_->effective_io_buffer_limit_;
  receipt.effective_wrapper_limit_ = accepted_config_->effective_wrapper_limit_;
  receipt.configured_callback_threads_ = callback_count;
  receipt.physical_reserve_debt_ = applied.physical_debt;
  receipt.admission_debt_ = applied.normal_record_debt != 0 || applied.normal_byte_debt != 0
      || applied.probe_record_debt != 0 || applied.probe_byte_debt != 0;
}

int ObIOService::health_status(storage_io::HealthStatus &status) const
{
  return rust_service_ ? convert_storage_error((*rust_service_)->health_status(status)) : OB_NOT_INIT;
}

int ObIOService::update_health_config(const ObIOConfig &conf)
{
  int ret = OB_SUCCESS;
  std::lock_guard<std::mutex> guard(config_mutex_);
  if (!is_inited_) { return OB_NOT_INIT; }
  ObIOServiceConfig replacement;
  if (OB_FAIL(replacement.deep_copy(accepted_config_->config_))) { return ret; }
  replacement.health_warning_tolerance_us_ = conf.data_storage_warning_tolerance_time_;
  replacement.health_blacklist_interval_us_ = conf.read_failure_black_list_interval_;
  replacement.health_wait_cap_us_ = conf.data_storage_io_timeout_ms_ * 1000;
  ResourceConfigReceipt receipt;
  return replace_config_locked(replacement, accepted_config_->generation_, accepted_config_->effective_server_memory_, receipt);
}

int ObIOService::update_runtime_config(const ObIOServiceConfig::ResourceConfig *resources,
    const ObIOServiceConfig::ParamConfig *params)
{
  int ret = OB_SUCCESS;
  std::lock_guard<std::mutex> guard(config_mutex_);
  if (!is_inited_) { return OB_NOT_INIT; }
  if (!is_working()) { return OB_STATE_NOT_MATCH; }
  ObIOServiceConfig replacement;
  if (OB_FAIL(replacement.deep_copy(accepted_config_->config_))) { return ret; }
  if (resources != nullptr) { replacement.resource_config_ = *resources; }
  if (params != nullptr) { replacement.param_config_ = *params; }
  ResourceConfigReceipt receipt;
  return replace_config_locked(replacement, accepted_config_->generation_,
      params == nullptr ? accepted_config_->effective_server_memory_ : params->memory_limit_, receipt);
}

int ObIOService::register_device(const storage_io::DeviceOptions &options, int64_t depth,
    int64_t probe_bytes, storage_io::DeviceHandle &endpoint)
{
  int ret = OB_SUCCESS;
  std::lock_guard<std::mutex> guard(config_mutex_);
  if (!is_inited_) { return OB_NOT_INIT; }
  ObIOServiceConfig replacement;
  if (OB_FAIL(replacement.deep_copy(accepted_config_->config_))) { return ret; }
  const ObIOServiceConfig::DeviceResource device{options.id, depth, 1, probe_bytes};
  if (OB_FAIL(replacement.devices_.push_back(device))) { return ret; }
  std::shared_ptr<IOConfigSnapshot> candidate;
  storage_io::ResourceConfig native{};
  if (OB_FAIL(prepare_config(replacement, accepted_config_->effective_server_memory_, candidate, native))) {
    return ret;
  }
  const int64_t callback_count = replacement.get_callback_thread_count();
  if (callback_count > INT32_MAX / 100000) { return OB_INVALID_ARGUMENT; }
  if (is_working() && OB_FAIL(callback_mgr_.prepare_thread_count(callback_count))) { return ret; }
  const auto applied = (*rust_service_)->register_device(accepted_config_->generation_, native, options, endpoint);
  if (applied.error != 0) { return convert_storage_error(applied.error); }
  ResourceConfigReceipt receipt;
  publish_config(std::move(candidate), applied, receipt);
  return ret;
}

int ObIOService::remove_device(uint64_t device_id)
{
  int ret = OB_SUCCESS;
  std::lock_guard<std::mutex> guard(config_mutex_);
  if (!is_inited_ || !rust_service_) { return OB_NOT_INIT; }
  ObIOServiceConfig replacement;
  if (OB_FAIL(replacement.deep_copy(accepted_config_->config_))) { return ret; }
  replacement.devices_.reset();
  bool found = false;
  for (const auto &device : accepted_config_->config_.devices_) {
    if (device.id_ == device_id) { found = true; }
    else if (OB_FAIL(replacement.devices_.push_back(device))) { return ret; }
  }
  if (!found) { return OB_HASH_NOT_EXIST; }
  std::shared_ptr<IOConfigSnapshot> candidate;
  storage_io::ResourceConfig native{};
  if (OB_FAIL(prepare_config(replacement, accepted_config_->effective_server_memory_, candidate, native))) {
    return ret;
  }
  const auto removed = (*rust_service_)->remove_device(accepted_config_->generation_, device_id);
  if (removed.error != 0) { return convert_storage_error(removed.error); }
  candidate->generation_ = removed.generation;
  accepted_config_ = std::move(candidate);
  return OB_SUCCESS;
}

std::shared_ptr<const IOConfigSnapshot> ObIOService::config_snapshot() const
{
  std::lock_guard<std::mutex> guard(config_mutex_);
  return accepted_config_;
}

int ObIOService::resource_stats(storage_io::ServiceStats &stats) const
{
  std::lock_guard<std::mutex> guard(config_mutex_);
  return rust_service_ ? convert_storage_error((*rust_service_)->service_stats(stats)) : OB_NOT_INIT;
}

int ObIOService::try_alloc_req_until_timeout(const int64_t timeout_ts, ObIORequest *&req)
{
  int ret = OB_SUCCESS;
  int64_t retry_alloc_count = 0;
  while (OB_SUCC(ret)) {
    ++retry_alloc_count;
    const int64_t current_ts = ObTimeUtility::current_time();
    if (current_ts > timeout_ts) {
      ret = OB_TIMEOUT;
    } else if (OB_FAIL(alloc_io_request(req))) {
      if (OB_ALLOCATE_MEMORY_FAILED == ret) {
        const int64_t remain_time = timeout_ts - current_ts;
        const int64_t sleep_time = MIN(remain_time, 1000L);
        if (TC_REACH_TIME_INTERVAL(1000L * 1000L)) {
          LOG_INFO("execute failed, retry later", K(ret), K(remain_time), K(sleep_time), K(retry_alloc_count));
        }
        ob_usleep((useconds_t)sleep_time);
        ret = OB_SUCCESS;
      }
    } else {
      LOG_INFO("retry alloc io_request success", K(retry_alloc_count));
      break;
    }
  }
  return ret;
}

int ObIOService::try_alloc_result_until_timeout(const int64_t timeout_ts, ObIOResult *&result)
{
  int ret = OB_SUCCESS;
  int64_t retry_alloc_count = 0;
  while (OB_SUCC(ret)) {
    ++retry_alloc_count;
    const int64_t current_ts = ObTimeUtility::current_time();
    if (current_ts > timeout_ts) {
      ret = OB_TIMEOUT;
    } else if (OB_FAIL(alloc_io_result(result))) {
      if (OB_ALLOCATE_MEMORY_FAILED == ret) {
        const int64_t remain_time = timeout_ts - current_ts;
        const int64_t sleep_time = MIN(remain_time, 1000L);
        if (TC_REACH_TIME_INTERVAL(1000L * 1000L)) {
          LOG_INFO("execute failed, retry later", K(ret), K(remain_time), K(sleep_time), K(retry_alloc_count));
        }
        ob_usleep((useconds_t)sleep_time);
        ret = OB_SUCCESS;
      }
    } else {
      LOG_INFO("retry alloc io_result success", K(retry_alloc_count));
      break;
    }
  }
  return ret;
}

int ObIOService::alloc_io_request(ObIORequest *&req)
{
  int ret = OB_SUCCESS;
  req = nullptr;
  void *buf = nullptr;
  if (OB_ISNULL(buf = io_allocator_->alloc(sizeof(ObIORequest)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    req = new (buf) ObIORequest;
    req->io_service_ = this;
    req->allocation_owner_ = io_allocator_;
  }
  return ret;
}

int ObIOService::alloc_io_result(ObIOResult *&result)
{
  int ret = OB_SUCCESS;
  result = nullptr;
  void *buf = nullptr;
  if (OB_ISNULL(buf = io_allocator_->alloc(sizeof(ObIOResult)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    result = new (buf) ObIOResult;
    result->io_service_ = this;
    result->allocation_owner_ = io_allocator_;
  }
  return ret;
}

int64_t ObIOService::get_group_num()
{
  const auto snapshot = config_snapshot();
  return snapshot ? snapshot->config_.group_configs_.count() / 3 : 0;
}

int ObIOService::print_io_status()
{
  int ret = OB_SUCCESS;
  const auto snapshot = config_snapshot();
  if (!snapshot) { return OB_NOT_INIT; }
  const ObIOServiceConfig &io_config_ = snapshot->config_;
  ObIOUsage &io_usage_ = *snapshot->usage_;
  if (is_working() && is_inited_) {
    char io_status[1024] = { 0 };
    bool need_print_io_config = false;
    ObIOUsageInfoArray info;
    ObIOUsageInfoArray sys_info;
    if (OB_FAIL(io_usage_.snapshot(info, storage_io::StatisticsSampleMode::Advance))
        || OB_FAIL(io_sys_usage_.snapshot(sys_info, storage_io::StatisticsSampleMode::Advance))) {
      return ret;
    }
    const int64_t GROUP_MODE_CNT = static_cast<int64_t>(ObIOGroupMode::MODECNT);
    int64_t ips = 0;
    int64_t ops = 0;
    double failed_ips = 0;
    double failed_ops = 0;
    uint64_t group_config_index = 0;
    ObIOGroupMode group_mode = ObIOGroupMode::MODECNT;
    int tmp_ret = OB_SUCCESS;
    for (int64_t i = 0; i < info.count(); ++i) {
      if (OB_TMP_FAIL(transform_usage_index_to_group_config_index(i, group_config_index))) {
        continue;
      } else if (group_config_index >= io_config_.group_configs_.count()) {
        continue;
      }
      group_mode = static_cast<ObIOGroupMode>(i % GROUP_MODE_CNT);
      const ObIOServiceConfig::GroupConfig &group_config = io_config_.group_configs_.at(group_config_index);
      if (group_config.deleted_) {
        continue;
      }
      const char *group_name = i < GROUP_MODE_CNT ? "OTHER_GROUPS" : group_config.group_name_;
      const char *mode_str = get_io_mode_string(group_mode);
      int64_t group_bw = 0;
      double failed_avg_size = 0;
      double failed_req_iops = 0;
      int64_t failed_req_bw = 0;
      int64_t failed_avg_prepare_delay = 0;
      int64_t failed_avg_schedule_delay = 0;
      int64_t failed_avg_submit_delay = 0;
      int64_t failed_avg_device_delay = 0;
      int64_t failed_avg_total_delay = 0;
      const auto failed = io_usage_.sample_failure(i);
      failed_avg_size = failed.average_bytes;
      failed_req_iops = failed.iops;
      failed_req_bw = failed.bytes_per_second;
      failed_avg_prepare_delay = failed.prepare_us;
      failed_avg_schedule_delay = failed.schedule_us;
      failed_avg_submit_delay = failed.submit_us;
      failed_avg_device_delay = failed.device_us;
      failed_avg_total_delay = failed.total_us;
      if (OB_TMP_FAIL(failed.error)) {
      } else if ((info.at(i).avg_byte_ + failed_avg_size) < std::numeric_limits<double>::epsilon()) {
      } else {
        group_bw = checked_metric(info.at(i).avg_byte_ * info.at(i).avg_iops_);
        switch (group_mode) {
          case ObIOGroupMode::LOCALREAD: {
            ips += checked_metric(info.at(i).avg_iops_);
            failed_ips += failed_req_iops;
            break;
          }
          case ObIOGroupMode::LOCALWRITE: {
            ops += checked_metric(info.at(i).avg_iops_);
            failed_ops += failed_req_iops;
            break;
          }
          default:
            break;
        }
        snprintf(io_status, sizeof(io_status),"group_id:%ld, group_name:%s, mode:%s "
            "[FAILED]:fail_size:%ld, fail_iops:%ld, fail_bw:%ld, [delay/us]:prepare:%ld, schedule:%ld, submit:%ld, rt:%ld, total:%ld, "
            "[SUCC]:size:%ld, iops:%ld, bw:%ld, [delay/us]:prepare:%ld, schedule:%ld, submit:%ld, rt:%ld, total:%ld",
            group_config.group_id_,
            group_name,
            mode_str,
            checked_metric(failed_avg_size),
            checked_metric(failed_req_iops + 0.5),
            checked_metric(failed_req_bw),
            failed_avg_prepare_delay,
            failed_avg_schedule_delay,
            failed_avg_submit_delay,
            failed_avg_device_delay,
            failed_avg_total_delay,
            checked_metric(info.at(i).avg_byte_),
            checked_metric(info.at(i).avg_iops_ + 0.5),
            static_cast<int64_t>(group_bw),
            info.at(i).avg_prepare_delay_us_,
            info.at(i).avg_schedule_delay_us_,
            info.at(i).avg_submit_delay_us_,
            info.at(i).avg_device_delay_us_,
            info.at(i).avg_total_delay_us_
            );
        LOG_INFO("[IO STATUS GROUP]", KCSTRING(io_status));
        need_print_io_config = true;
      }
    }
    // SYS GROUPS
    for (int64_t i = 0; i < sys_info.count(); ++i) {
      if (OB_TMP_FAIL(transform_usage_index_to_group_config_index(i, group_config_index))) {
        continue;
      }
      group_mode = static_cast<ObIOGroupMode>(i % GROUP_MODE_CNT);
      ObIOModule module = static_cast<ObIOModule>(SYS_MODULE_START_ID + i / GROUP_MODE_CNT);
      const char *mode_str = get_io_mode_string(group_mode);
      int64_t group_bw = 0;
      double failed_avg_size = 0;
      double failed_req_iops = 0;
      int64_t failed_req_bw = 0;
      int64_t failed_avg_prepare_delay = 0;
      int64_t failed_avg_schedule_delay = 0;
      int64_t failed_avg_submit_delay = 0;
      int64_t failed_avg_device_delay = 0;
      int64_t failed_avg_total_delay = 0;
      const auto failed = io_sys_usage_.sample_failure(i);
      failed_avg_size = failed.average_bytes;
      failed_req_iops = failed.iops;
      failed_req_bw = failed.bytes_per_second;
      failed_avg_prepare_delay = failed.prepare_us;
      failed_avg_schedule_delay = failed.schedule_us;
      failed_avg_submit_delay = failed.submit_us;
      failed_avg_device_delay = failed.device_us;
      failed_avg_total_delay = failed.total_us;
      if (OB_TMP_FAIL(failed.error)) {
      } else if ((sys_info.at(i).avg_byte_ + failed_avg_size) < std::numeric_limits<double>::epsilon()) {
      } else {
        switch (group_mode) {
          case ObIOGroupMode::LOCALREAD: {
            ips += checked_metric(sys_info.at(i).avg_iops_);
            failed_ips += failed_req_iops;
            break;
          }
          case ObIOGroupMode::LOCALWRITE: {
            ops += checked_metric(sys_info.at(i).avg_iops_);
            failed_ops += failed_req_iops;
            break;
          }
          default:
            break;
        }
        group_bw = checked_metric(sys_info.at(i).avg_byte_ * sys_info.at(i).avg_iops_);
        snprintf(io_status, sizeof(io_status),
                "sys_group_name:%s, mode:%s "
                "[FAILED]: fail_size:%ld, fail_iops:%ld, fail_bw:%ld, [delay/us]:prepare:%ld, schedule:%ld, submit:%ld, rt:%ld, total:%ld, "
                "[SUCC]: size:%ld, iops:%ld, bw:%ld, [delay/us]:prepare:%ld, schedule:%ld, submit:%ld, rt:%ld, total:%ld",
                 get_io_sys_group_name(module),
                 mode_str,
                 checked_metric(failed_avg_size),
                 checked_metric(failed_req_iops + 0.5),
                 checked_metric(failed_req_bw),
                 failed_avg_prepare_delay,
                 failed_avg_schedule_delay,
                 failed_avg_submit_delay,
                 failed_avg_device_delay,
                 failed_avg_total_delay,
                 checked_metric(sys_info.at(i).avg_byte_),
                 checked_metric(sys_info.at(i).avg_iops_ + 0.5),
                 static_cast<int64_t>(group_bw),
                 sys_info.at(i).avg_prepare_delay_us_,
                 sys_info.at(i).avg_schedule_delay_us_,
                 sys_info.at(i).avg_submit_delay_us_,
                 sys_info.at(i).avg_device_delay_us_,
                 sys_info.at(i).avg_total_delay_us_
                 );
        LOG_INFO("[IO STATUS GROUP SYS]", KCSTRING(io_status));
        need_print_io_config = true;
      }
    }
    if (need_print_io_config) {
      int64_t iops = ips + ops;
      double failed_iops = failed_ips + failed_ops;
      LOG_INFO("[IO STATUS]", K_(ref_cnt), K_(io_config),
          "hold_mem", io_allocator_->get_allocated_size(),
          "[FAILED]: "
          "fail_ips", lround(failed_ips),
          "fail_ops", lround(failed_ops),
          "fail_iops", lround(failed_iops),
          "[SUCC]: "
          "ips", ips,
          "ops", ops,
          "iops", iops);
    }

    storage_io::ServiceStats resources{};
    if (OB_SUCCESS == resource_stats(resources)) {
      LOG_INFO("storage resources", K(resources.generation), K(resources.admitted_requests),
          K(resources.reserved_bytes), K(resources.queued_requests), K(resources.awaiting_retirement),
          K(resources.admission_debt), K(resources.physical_debt), K(resources.probe_attempts));
    }
    {
      (void)callback_mgr_.to_string(io_status, sizeof(io_status));
      LOG_INFO("[IO STATUS CALLBACK]", KCSTRING(io_status));
    }
  }
  return ret;
}

void ObIOService::inc_ref()
{
  ATOMIC_INC(&ref_cnt_);
}

void ObIOService::dec_ref()
{
  int ret = OB_SUCCESS;
  int64_t tmp_ref = ATOMIC_SAF(&ref_cnt_, 1);
  if (tmp_ref < 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("bug: ref_cnt < 0", K(ret), K(tmp_ref));
    abort();
  }
}

namespace oceanbase
{
namespace common
{
void ObIOManager::print_service_status()
{
  ObRefHolder<ObIOService> holder;
  if (OB_SUCCESS == get_io_service(holder)) { holder.get_ptr()->print_io_status(); }
}

}
}
