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
#include "observer/ob_server.h"
#include "common/storage/ob_fd_simulator.h"

#ifdef OB_BUILD_SHARED_STORAGE
#include "storage/shared_storage/ob_file_manager.h"
#endif

using namespace oceanbase::lib;
using namespace oceanbase::common;
using namespace oceanbase::storage;

static int clear_io_hang_errsim;

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
  write_failure_detect_interval_ = 60 * 1000 * 1000; // 1 min
  read_failure_black_list_interval_ = 60 * 1000 * 1000; // Cooperate with the adjustment of tolerance_time to 1min
  data_storage_warning_tolerance_time_ = 5L * 1000L * 1000L; // 5s, same as parameter seed
  data_storage_error_tolerance_time_ = 300L * 1000L * 1000L; // 300s
  disk_io_thread_count_ = 8;
  sync_io_thread_count_ = 0;
  data_storage_io_timeout_ms_ = 120L * 1000L; // 120s
}

bool ObIOConfig::is_valid() const
{
  return write_failure_detect_interval_ > 0
      && read_failure_black_list_interval_ > 0
      && data_storage_warning_tolerance_time_ > 0
      && data_storage_error_tolerance_time_ >= data_storage_warning_tolerance_time_
      && disk_io_thread_count_ > 0 && disk_io_thread_count_ <= MAX_IO_THREAD_COUNT
      && sync_io_thread_count_ >= 0 && sync_io_thread_count_ <= MAX_SYNC_IO_THREAD_COUNT
      && data_storage_io_timeout_ms_ > 0;
}


/******************             IOMemoryPool              **********************/
template<int64_t SIZE>
ObIOMemoryPool<SIZE>::ObIOMemoryPool()
  : is_inited_(false), capacity_(0), free_count_(0), allocator_(nullptr), begin_ptr_(nullptr)
{

}

template<int64_t SIZE>
ObIOMemoryPool<SIZE>::~ObIOMemoryPool()
{
  destroy();
}


template<int64_t SIZE>
void ObIOMemoryPool<SIZE>::destroy()
{
  is_inited_ = false;
  free_count_ = 0;
  pool_.destroy();

  if (nullptr != allocator_) {
    if (nullptr != begin_ptr_) {
      allocator_->free(begin_ptr_);
      begin_ptr_ = nullptr;
    }
  }
  allocator_ = nullptr;
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

int ObIOAllocator::init(const uint64_t tenant_id, const int64_t memory_limit)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("io allocator init twice", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id) || memory_limit <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tenant_id), K(memory_limit));
  } else if (OB_FAIL(inner_allocator_.init(OB_MALLOC_MIDDLE_BLOCK_SIZE,
                                           ObModIds::OB_IO_CONTROL,
                                           tenant_id,
                                           memory_limit))) {
    LOG_WARN("init inner allocator failed", K(ret), K(tenant_id), K(memory_limit));
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

int ObIOAllocator::update_memory_limit(const int64_t memory_limit)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(memory_limit <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(memory_limit));
  } else if (memory_limit == memory_limit_) {
    // do nothing
  } else if (FALSE_IT(inner_allocator_.set_total_limit(memory_limit))) {
  } else {
    // TODO@wenqu: dynamic ajust the pool count
  }
  return ret;
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
    LOG_WARN("io allocator is not inited", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(size <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(size));
  } else {
    // get buf from normal allocator
    if (OB_ISNULL(ret_buf)) {
      if (OB_ISNULL(ret_buf = inner_allocator_.alloc(size))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to allocate io mem block", K(ret), K(size));
      }
    }
  }
  return ret_buf;
}

void ObIOAllocator::free(void *ptr)
{
  inner_allocator_.free(ptr);
}

/******************             IOStat              **********************/

ObIOStat::ObIOStat()
  : io_count_(0),
    io_bytes_(0),
    io_prepare_delay_us_(0),
    io_schedule_delay_us_(0),
    io_submit_delay_us_(0),
    io_device_delay_us_(0),
    io_total_delay_us_(0)
{

}

ObIOStat::~ObIOStat()
{

}

void ObIOStat::accumulate(const uint64_t io_count, const uint64_t io_bytes, const uint64_t prepare_delay, const uint64_t schedule_delay, const uint64_t submit_delay, const int64_t device_delay, const int64_t total_delay)
{
  ATOMIC_AAF(&io_count_, io_count);
  ATOMIC_AAF(&io_bytes_, io_bytes);
  ATOMIC_AAF(&io_prepare_delay_us_, prepare_delay);
  ATOMIC_AAF(&io_schedule_delay_us_, schedule_delay);
  ATOMIC_AAF(&io_submit_delay_us_, submit_delay);
  ATOMIC_AAF(&io_device_delay_us_, device_delay);
  ATOMIC_AAF(&io_total_delay_us_, total_delay);
}

void ObIOStat::reset()
{
  io_count_ = 0;
  io_bytes_ = 0;
  io_prepare_delay_us_ = 0;
  io_schedule_delay_us_ = 0;
  io_submit_delay_us_ = 0;
  io_device_delay_us_ = 0;
  io_total_delay_us_ = 0;
}

/******************             IOStatDiff              **********************/

ObIOStatDiff::ObIOStatDiff()
  : last_stat_(),
    last_ts_(0)
{
  last_ts_ = ObTimeUtility::fast_current_time();
}

ObIOStatDiff::~ObIOStatDiff()
{

}

void ObIOStatDiff::diff(const ObIOStat &io_stat, double &avg_iops, double &avg_bytes, int64_t &avg_prepare_delay,
      int64_t &avg_schedule_delay, int64_t &avg_submit_delay, int64_t &avg_device_delay, int64_t &avg_total_delay)
{
  const int64_t new_ts = ObTimeUtility::fast_current_time();
  const ObIOStat new_stat = io_stat; // copy to prevent accumulating
  const int64_t delta_io_count = (new_stat.io_count_ - last_stat_.io_count_);
  if (delta_io_count > 0) {
    avg_bytes = 1.0 * (new_stat.io_bytes_ - last_stat_.io_bytes_) / delta_io_count;
    avg_prepare_delay = (new_stat.io_prepare_delay_us_ - last_stat_.io_prepare_delay_us_) / delta_io_count;
    avg_schedule_delay = (new_stat.io_schedule_delay_us_ - last_stat_.io_schedule_delay_us_) / delta_io_count;
    avg_submit_delay = (new_stat.io_submit_delay_us_ - last_stat_.io_submit_delay_us_) / delta_io_count;
    avg_device_delay = (new_stat.io_device_delay_us_ - last_stat_.io_device_delay_us_) / delta_io_count;
    avg_total_delay = (new_stat.io_total_delay_us_ - last_stat_.io_total_delay_us_) / delta_io_count;
  } else {
    avg_bytes = 0;
    avg_prepare_delay = 0;
    avg_schedule_delay = 0;
    avg_submit_delay = 0;
    avg_device_delay = 0;
    avg_total_delay = 0;
  }
  if (last_ts_ > 0 && new_ts > last_ts_) {
    const double delta_interval_s = 1.0 * (new_ts - last_ts_) / (1000L * 1000L);
    avg_iops = 1.0 * delta_io_count / delta_interval_s;
  } else {
    avg_iops = 0;
  }
  last_stat_ = new_stat;
  last_ts_ = new_ts;
}


/******************        Function Group Usage      **********************/
ObIOFuncUsages::ObIOFuncUsages()
{
}

int ObIOFuncUsages::init(const uint64_t tenant_id)
{
  int ret = OB_SUCCESS;
  int FUNC_NUM = static_cast<uint8_t>(share::ObFunctionType::MAX_FUNCTION_NUM);
  int GROUP_MODE_NUM = static_cast<uint8_t>(ObIOGroupMode::MODECNT);
  func_usages_.set_attr(ObMemAttr(tenant_id, "IOFuncUsages"));
  for (int i = 0; i < FUNC_NUM && OB_SUCC(ret); ++i) {
    ObIOFuncUsage func_usage;
    if (OB_FAIL(func_usage.reserve(GROUP_MODE_NUM))) {
      LOG_WARN("reserve func usage failed", K(ret), K(i));
    } else {
      for (int j = 0; j < GROUP_MODE_NUM && OB_SUCC(ret); ++j) {
        if (OB_FAIL(func_usage.push_back(ObIOFuncUsageByMode()))) {
          LOG_WARN("func usage push failed", K(ret), K(j));
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(func_usages_.push_back(func_usage))) {
        LOG_WARN("push func usage failed", K(ret), K(i));
      }
    }
  }
  return ret;
}
int ObIOFuncUsages::accumulate(ObIORequest &req) {
  int ret = OB_SUCCESS;
  int64_t io_size = 0;
  int64_t prepare_delay = 0;
  int64_t schedule_delay = 0;
  int64_t submit_delay = 0;
  int64_t device_delay = 0;
  int64_t total_delay = 0;
  uint64_t idx = 0;
  ObIOGroupMode mode = ObIOGroupMode::MODECNT;
  if (OB_ISNULL(req.io_result_)){
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("invalid io result", K(req));
  } else if (OB_FAIL(req.io_result_->cal_delay_us(
                 prepare_delay, schedule_delay, submit_delay, device_delay, total_delay))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_INFO("failed to cal delay", K(ret));
  } else {
    io_size = req.get_align_size();
    idx = static_cast<uint8_t>(req.get_flag().get_func_type());
    mode = req.get_group_mode();
    if (idx < 0 || idx >= func_usages_.count()) {
      LOG_ERROR("invalid io usage index", K(idx), K(func_usages_.count()));
    } else if (mode == ObIOGroupMode::MODECNT) {
      LOG_ERROR("invalid io usage mode", K(idx), K(mode), K(func_usages_.count()));
    } else {
      func_usages_.at(idx).at(static_cast<uint8_t>(mode)).inc(io_size, prepare_delay, schedule_delay, submit_delay, device_delay, total_delay);
    }
  }
  return ret;
}


/******************             IOUsage              **********************/
ObIOUsage::~ObIOUsage()
{
  lock_.destroy();
}

int ObIOUsage::init(const uint64_t tenant_id, const int64_t group_num)
{
  int ret =OB_SUCCESS;
  if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tenant_id));
  } else if (FALSE_IT(info_.set_attr(ObMemAttr(tenant_id, "IOUsageInfo")))) {
  } else if (FALSE_IT(failed_req_info_.set_attr(ObMemAttr(tenant_id, "IOUsageInfo")))) {
  } else if (FALSE_IT(group_throttled_time_us_.set_attr(ObMemAttr(tenant_id, "CPUUSage")))) {
  } else if (OB_FAIL(refresh_group_num(group_num))) {
    LOG_WARN("refresh io usage array failed", K(ret), K(group_num));
  } else if (OB_FAIL(lock_.init(lib::ObMemAttr(tenant_id, "IOUsage")))) {
    LOG_WARN("init lock failed", K(ret));
  }
  return ret;
}

int ObIOUsage::refresh_group_num(const int64_t group_num)
{
  int ret = OB_SUCCESS;
  ObQSyncLockWriteGuard guard(lock_);
  if (group_num < 1) {
    ret = OB_INVALID_CONFIG;
    LOG_WARN("invalid group num", K(ret), K(group_num));
  } else {
    int64_t IO_GROUP_MODE_CNT = static_cast<int64_t>(ObIOGroupMode::MODECNT);
    for (int64_t i = info_.count(); OB_SUCC(ret) && i < group_num * IO_GROUP_MODE_CNT; ++i) {
      if (OB_FAIL(info_.push_back(ObIOUsageInfo()))) {
        LOG_WARN("push array failed", K(ret), K(i));
      } else if (OB_FAIL(failed_req_info_.push_back(ObIOFailedReqUsageInfo()))) {
        LOG_WARN("push array failed", K(ret), K(i));
      }
    }
    for (int64_t i = group_throttled_time_us_.count(); OB_SUCC(ret) && i < group_num; ++i) {
      if (OB_FAIL(group_throttled_time_us_.push_back(0))) {
        LOG_WARN("push array failed", K(ret), K(i));
      }
    }
  }
  return ret;
}

void ObIOUsage::accumulate(ObIORequest &req)
{
  int ret = OB_SUCCESS;
  ObQSyncLockReadGuard guard(lock_);
  int64_t io_size = req.get_align_size();
  int64_t prepare_delay = 0;
  int64_t schedule_delay = 0;
  int64_t submit_delay = 0;
  int64_t device_delay = 0;
  int64_t total_delay = 0;
  uint64_t idx = 0;
  if (OB_ISNULL(req.io_result_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("invalid io result", K(info_.count()), K(req));
  } else if (FALSE_IT(idx = req.io_result_->get_io_usage_index())) {
  } else if (idx >= info_.count() || idx < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_INFO("invalid io usage index", K(idx), K(info_.count()), K(ret));
  } else if (OB_FAIL(req.io_result_->cal_delay_us(
                 prepare_delay, schedule_delay, submit_delay, device_delay, total_delay))) {
    LOG_INFO("failed to cal delay", K(ret));
  } else if (req.io_result_->time_log_.return_ts_ > 0 && req.io_result_->ret_code_.io_ret_ == 0) {
    info_.at(idx).io_stat_.accumulate(1, io_size, prepare_delay, schedule_delay, submit_delay, device_delay, total_delay);
  } else {
    failed_req_info_.at(idx).inc(io_size, prepare_delay, schedule_delay, submit_delay, device_delay, total_delay);
  }
}

void ObIOUsage::calculate_io_usage()
{
  ObQSyncLockReadGuard guard(lock_);
  for (int64_t i = 0; i < info_.count(); ++i) {
    ObIOUsageInfo &info = info_.at(i);
    info.io_estimator_.diff(info.io_stat_, info.avg_iops_, info.avg_byte_, info.avg_prepare_delay_us_, info.avg_schedule_delay_us_, info.avg_submit_delay_us_, info.avg_device_delay_us_, info.avg_total_delay_us_);
  }
}

int ObIOUsage::assign(const ObIOUsage &other)
{
  int ret = OB_SUCCESS;
  if (this == &other) {
    // do nothing
  } else if ((int64_t)(&lock_) < (int64_t)(&other.lock_)) {
    ObQSyncLockReadGuard guard1(lock_);
    ObQSyncLockReadGuard guard2(other.lock_);
    ret = assign_unsafe(other);
  } else {
    ObQSyncLockReadGuard guard1(other.lock_);
    ObQSyncLockReadGuard guard2(lock_);
    ret = assign_unsafe(other);
  }
  return ret;
}

int ObIOUsage::assign_unsafe(const ObIOUsage &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(info_.assign(other.info_))) {
  } else if (OB_FAIL(failed_req_info_.assign(other.failed_req_info_))) {
  } else if (OB_FAIL(group_throttled_time_us_.assign(other.group_throttled_time_us_))) {
  }
  return ret;
}

int64_t ObIOUsage::to_string(char* buf, const int64_t buf_len) const
{
  ObQSyncLockReadGuard guard(lock_);
  int64_t pos = 0;
  J_OBJ_START();
  J_OBJ_END();
  return pos;
}

/******************             IOGroupUsage              **********************/

int ObIOGroupUsage::record(const double avg_size, const double avg_iops, const int64_t avg_bw,
    const int64_t avg_prepare_delay, const int64_t avg_schedule_delay, const int64_t avg_submit_delay,
    const int64_t avg_device_delay, const int64_t avg_total_delay)
{
  int ret = OB_SUCCESS;
  last_stat_.avg_size_ = avg_size;
  last_stat_.avg_iops_ = avg_iops;
  ATOMIC_STORE(&last_stat_.avg_bw_, avg_bw);
  ATOMIC_STORE(&last_stat_.avg_delay_arr_.prepare_delay_us_, avg_prepare_delay);
  ATOMIC_STORE(&last_stat_.avg_delay_arr_.schedule_delay_us_, avg_schedule_delay);
  ATOMIC_STORE(&last_stat_.avg_delay_arr_.submit_delay_us_, avg_submit_delay);
  ATOMIC_STORE(&last_stat_.avg_delay_arr_.device_delay_us_, avg_device_delay);
  ATOMIC_STORE(&last_stat_.avg_delay_arr_.total_delay_us_, avg_total_delay);
  return ret;
}

int ObIOGroupUsage::calc(double &avg_size, double &avg_iops, int64_t &avg_bw,
    int64_t &avg_prepare_delay, int64_t &avg_schedule_delay, int64_t &avg_submit_delay, int64_t &avg_device_delay,
    int64_t &avg_total_delay)
{
  int ret = OB_SUCCESS;
  int64_t now = ObTimeUtility::fast_current_time();
  int64_t last_ts = ATOMIC_LOAD(&last_ts_);
  avg_size = 0;
  avg_iops = 0;
  avg_bw = 0;
  avg_prepare_delay = 0;
  avg_schedule_delay = 0;
  avg_submit_delay = 0;
  avg_device_delay = 0;
  avg_total_delay = 0;
  if (0 != last_ts && now - last_ts > 0 && ATOMIC_BCAS(&last_ts_, last_ts, 0)) {
    int64_t size = 0;
    int64_t io_count = 0;
    const int64_t diff = now - last_ts;
    io_count = ATOMIC_SET(&io_count_, 0);
    size = ATOMIC_SET(&size_, 0);
    ATOMIC_STORE(&last_ts_, now);
    if (io_count != 0) {
      avg_size = static_cast<double>(size) / io_count;
      avg_prepare_delay = total_delay_arr_.prepare_delay_us_ / io_count;
      avg_schedule_delay = total_delay_arr_.schedule_delay_us_ / io_count;
      avg_submit_delay = total_delay_arr_.submit_delay_us_ / io_count;
      avg_device_delay = total_delay_arr_.device_delay_us_ / io_count;
      avg_total_delay = total_delay_arr_.total_delay_us_ / io_count;
    }
    avg_iops = static_cast<double>(io_count * 1000L * 1000L) / diff;
    avg_bw = size * 1000L * 1000L / diff;
    if (OB_FAIL(record(avg_size, avg_iops, avg_bw, avg_prepare_delay, avg_schedule_delay, avg_submit_delay, avg_device_delay, avg_total_delay))) {
      LOG_WARN("record io group usage failed", K(ret), K(*this));
    } else if (OB_FAIL(clear())) {
      LOG_WARN("clear io group usage failed", K(ret), K(*this));
    }
  }
  return ret;
}

/******************             CpuUsage              **********************/
ObCpuUsage::ObCpuUsage()
  : last_usage_(),
    last_ts_(0)
{
  MEMSET(&last_usage_, 0, sizeof(last_usage_));
}

ObCpuUsage::~ObCpuUsage()
{

}

void ObCpuUsage::get_cpu_usage(double &avg_usage_percentage)
{
  const int64_t new_ts = ObTimeUtility::fast_current_time();
  struct rusage new_usage;
  int sys_errno = 0;
  if (0 != (sys_errno = getrusage(RUSAGE_SELF, &new_usage))) {
    if (REACH_TIME_INTERVAL(1000L * 1000L * 10)) {
      LOG_WARN_RET(OB_ERR_SYS, "get cpu usage failed", K(sys_errno));
    }
  } else {
    if (last_ts_ > 0 && new_ts > last_ts_) {
      const int64_t sched_period_us = new_ts - last_ts_;
      const int64_t cpu_time_us = (new_usage.ru_utime.tv_sec - last_usage_.ru_utime.tv_sec) * 1000000L
                             + (new_usage.ru_utime.tv_usec - last_usage_.ru_utime.tv_usec)
                             + (new_usage.ru_stime.tv_sec - last_usage_.ru_stime.tv_sec) * 1000000L
                             + (new_usage.ru_stime.tv_usec - last_usage_.ru_stime.tv_usec);
      avg_usage_percentage = 1.0 * cpu_time_us / sched_period_us * 100;
    } else {
      avg_usage_percentage = 0;
    }
    last_usage_ = new_usage;
    last_ts_ = new_ts;
  }
}


/******************         IOMemStat and IOMemStats           **********************/


int ObIOMemStat::refresh_group_num(const int64_t group_num)
{
  int ret = OB_SUCCESS;
  if (group_num < 1) {
    ret = OB_INVALID_CONFIG;
    LOG_WARN("invalid group num", K(ret), K(group_num));
  } else {
    int64_t IO_GROUP_MODE_CNT = static_cast<int64_t>(ObIOGroupMode::MODECNT);
    for (int64_t i = group_mem_infos_.count(); OB_SUCC(ret) && i < group_num * IO_GROUP_MODE_CNT; ++i) {
      if (OB_FAIL(group_mem_infos_.push_back(ObIOGroupMemInfo()))) {
        LOG_WARN("push array failed", K(ret), K(i));
      }
    }
  }
  return ret;
}

int ObIOMemStat::inc(const ObIORequest &req)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(req.io_result_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_INFO("invalid io result", K(group_mem_infos_.count()), K(req));
  } else {
    uint64_t idx = req.io_result_->get_io_usage_index();  // the group index  of mem is same as the group index in usage.
    if (idx >= group_mem_infos_.count() || idx < 0) {
      LOG_INFO("invalid io usage index", K(idx), K(group_mem_infos_.count()), K(req.io_result_->get_group_key()));
    } else if (OB_FAIL(group_mem_infos_.at(idx).inc(req.get_buf_size()))) {
      LOG_WARN("failed to inc", K(ret));
    }
  }
  return ret;
}

int ObIOMemStat::dec(const ObIORequest &req)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(req.io_result_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_INFO("invalid io result", K(group_mem_infos_.count()), K(req));
  } else {
    // the group index  of mem is same as the group index in usage.
    uint64_t idx = req.io_result_->get_io_usage_index();
    if (idx >= group_mem_infos_.count() || idx < 0) {
      LOG_INFO("invalid io usage index", K(idx), K(group_mem_infos_.count()), K(req.io_result_->get_group_key()));
    } else if (OB_FAIL(group_mem_infos_.at(idx).dec(req.get_buf_size()))) {
      LOG_WARN("failed to dec", K(ret));
    } else if (group_mem_infos_.at(idx).total_cnt_ < 0 || group_mem_infos_.at(idx).total_size_ < 0) {
      ret = OB_ERR_UNEXPECTED;
      ATOMIC_STORE(&group_mem_infos_.at(idx).total_cnt_, 0);
      ATOMIC_STORE(&group_mem_infos_.at(idx).total_size_, 0);
      LOG_WARN("invalid io mem stats", K(ret), K(idx), K(group_mem_infos_.at(idx)), K(req), K(req.get_buf_size()));
    }
  }
  return ret;
}

int ObIOMemStats::init(const int64_t sys_group_num, const int64_t group_num)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(sys_mem_stat_.refresh_group_num(sys_group_num))) {
    LOG_WARN("refresh sys group's io mem stats array failed", K(ret), K(sys_group_num));
  } else if (OB_FAIL(mem_stat_.refresh_group_num(group_num))) {
    LOG_WARN("refresh group's io mem stats array failed", K(ret), K(group_num));
  }
  return ret;
}

int ObIOMemStats::inc(const ObIORequest &req)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(req.io_result_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid io result", K(req));
  } else if (req.is_sys_module()) {
    if (OB_FAIL(sys_mem_stat_.inc(req))) {
      LOG_WARN("failed to inc", K(ret));
    }
  } else if (OB_FAIL(mem_stat_.inc(req))) {
    LOG_WARN("failed to inc", K(ret));
  }
  return ret;
}

int ObIOMemStats::dec(const ObIORequest &req)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(req.io_result_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid io result", K(req));
  } else if (req.is_sys_module()) {
    if (OB_FAIL(sys_mem_stat_.dec(req))) {
      LOG_WARN("failed to dec", K(ret));
    }
  } else if (OB_FAIL(mem_stat_.dec(req))) {
    LOG_WARN("failed to dec", K(ret));
  }
  return ret;
}

/******************             IOTuner              **********************/
ObIOTuner::ObIOTuner()
  : is_inited_(false), cpu_usage_()
{

}

ObIOTuner::~ObIOTuner()
{
  destroy();
}

int ObIOTuner::init()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else if (OB_FAIL(TG_SET_RUNNABLE_AND_START(lib::TGDefIDs::IO_TUNING, *this))) {
    LOG_WARN("start io scheduler failed", K(ret));
  } else {
    is_inited_ = true;
  }
  if (OB_UNLIKELY(!is_inited_)) {
    destroy();
  }
  return ret;
}

int ObIOTuner::send_detect_task()
{
  int ret = OB_SUCCESS;
  ObArray<MacroBlockId> macro_ids;
  macro_ids.set_attr(ObMemAttr(OB_SYS_TENANT_ID, "back_io_detect"));
  if (!OB_SERVER_BLOCK_MGR.is_started() || 0 == OB_SERVER_BLOCK_MGR.get_used_macro_block_count()) {
    ret = OB_NOT_INIT;
    LOG_WARN("block manager not init", K(ret));
  } else if (OB_FAIL(OB_SERVER_BLOCK_MGR.get_limited_iter_macro_ids(macro_ids, 128))) {
    LOG_WARN("fail to get macro ids", K(ret), K(macro_ids));
  } else if (OB_UNLIKELY(0 == macro_ids.count())) {
    // skip
  } else {
    MacroBlockId &rand_id = macro_ids.at(ObRandom::rand(0, macro_ids.count() - 1));
    if (OB_FAIL(
            OB_IO_MANAGER.get_device_health_detector().record_timing_task(rand_id.first_id(), rand_id.second_id()))) {
      LOG_WARN("fail to record timing task", K(ret), K(rand_id));
    }
  }
  return ret;
}

void ObIOTuner::stop()
{
  TG_STOP(lib::TGDefIDs::IO_TUNING);
}

void ObIOTuner::wait()
{
  TG_WAIT(lib::TGDefIDs::IO_TUNING);
}

void ObIOTuner::destroy()
{
  stop();
  wait();
  is_inited_ = false;
}

void ObIOTuner::run1()
{
  int ret = OB_SUCCESS;
  const int64_t thread_id = get_thread_idx();
  set_thread_name("IO_TUNING", thread_id);
  LOG_INFO("io tuner thread started");
  while (!has_set_stop()) {
    // print interval must <= 1s, for ensuring real_iops >= 1 in gv$ob_io_quota.
    if (REACH_TIME_INTERVAL(1000L * 1000L * 1L)) {
      OB_IO_MANAGER.print_status();
      if (!GCTX.is_shared_storage_mode() && OB_FAIL(send_detect_task())) {
        LOG_WARN("fail to send detect task", K(ret));
      }
    }
    ob_usleep(100 * 1000, true/*is_idle_sleep*/); // 100ms
  }
  LOG_INFO("io tuner thread stopped");
}


/******************             ObIOGroupQueues              **********************/
ObIOGroupQueues::ObIOGroupQueues(ObIAllocator &allocator)
  : is_inited_(false),
    allocator_(allocator),
    group_phy_queues_()
{

}

ObIOGroupQueues::~ObIOGroupQueues()
{
  destroy();
}


void ObIOGroupQueues::destroy()
{
  for (int64_t i = 0; i < group_phy_queues_.count(); ++i) {
    ObPhyQueue *tmp_phyqueue = group_phy_queues_.at(i);
    if (nullptr != tmp_phyqueue) {
      tmp_phyqueue->destroy();
      allocator_.free(tmp_phyqueue);
    }
  }
  group_phy_queues_.destroy();
  is_inited_ = false;
}

/******************             IOChannel              **********************/
ObIOChannel::ObIOChannel()
  : device_handle_(nullptr),
    device_channel_(nullptr)
{

}

ObIOChannel::~ObIOChannel()
{
}

int ObIOChannel::base_init(ObDeviceChannel *device_channel)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == device_channel)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(device_channel));
  } else {
    device_channel_ = device_channel;
    device_handle_ = device_channel->device_handle_;
  }
  return ret;
}

/******************             AsyncIOChannel              **********************/
ObAsyncIOChannel::ObAsyncIOChannel()
  : is_inited_(false),
    tg_id_(-1),
    io_context_(nullptr),
    io_events_(nullptr),
    polling_timeout_({0, 0}),
    submit_count_(0)
{

}

ObAsyncIOChannel::~ObAsyncIOChannel()
{
  destroy();
}

void switch_check_io_hang_errsim()
{
  ATOMIC_FAA(&clear_io_hang_errsim, 1);
}

static inline int check_io_hang_errsim()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_E(EventTable::EN_IO_HANG_ERROR) OB_SUCCESS;

  STATIC_ASSERT(0 == OB_SUCCESS, "OB_SUCCESS is not 0");

  if (OB_SUCCESS != tmp_ret) {
    int errsim = -tmp_ret;
    int hang_ms = (errsim / 10) * 10;
    int errcode = (errsim % 10);
    if (0 == errcode) {
      ret = OB_SUCCESS;
    } else {
      ret = OB_IO_ERROR;
    }
    if (0 == clear_io_hang_errsim % 2) {
      LOG_WARN("errsim: EN_IO_HANG_ERROR", K(ret), K(tmp_ret), K(hang_ms));
    } else {
      LOG_WARN("errsim: EN_IO_HANG_ERROR is ignored", K(ret), K(tmp_ret), K(hang_ms));
    }
    while (hang_ms > 0 && 0 == ATOMIC_LOAD(&clear_io_hang_errsim) % 2) {
      oceanbase::lib::Thread::WaitGuard guard(oceanbase::lib::Thread::WAIT_FOR_LOCAL_RETRY);
      ObClockGenerator::msleep(10);
      hang_ms = hang_ms - 10;
    }
  }
  return ret;
}

int ObAsyncIOChannel::init(ObDeviceChannel *device_channel)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(is_inited_));
  } else if (OB_FAIL(base_init(device_channel))) {
    LOG_WARN("base init failed", K(ret), KP(device_channel));
  } else if (OB_FAIL(depth_cond_.init(ObWaitEventIds::IO_CHANNEL_COND_WAIT))) {
    LOG_WARN("init thread cond failed", K(ret));
  } else if (OB_FAIL(device_handle_->io_setup(MAX_AIO_EVENT_CNT, io_context_))) {
    LOG_ERROR("io setup failed, check config aio-max-nr of operating system", K(ret), KP(io_context_));
  } else if (OB_ISNULL(io_events_ = device_handle_->alloc_io_events(MAX_AIO_EVENT_CNT))) {
    ret = OB_ERR_SYS;
    LOG_WARN("alloc io events failed", K(ret), KP(io_events_));
  } else {
    polling_timeout_.tv_sec = 0;
    polling_timeout_.tv_nsec = AIO_POLLING_TIMEOUT_NS;
    submit_count_ = 0;
    is_inited_ = true;
  }
  if (OB_UNLIKELY(!is_inited_)) {
    destroy();
  }
  return ret;
}

int ObAsyncIOChannel::start()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(start_thread())) {
    LOG_WARN("start async io channel failed", K(ret));
  }
  return ret;
}

void ObAsyncIOChannel::stop()
{
  if (tg_id_ >= 0) {
    TG_STOP(tg_id_);
  }
}

void ObAsyncIOChannel::wait()
{
  if (tg_id_ >= 0) {
    TG_WAIT(tg_id_);
  }
}

void ObAsyncIOChannel::destroy()
{
    // wait flying request
  const int64_t max_wait_ts = ObTimeUtility::fast_current_time() + 1000L * 1000L * 30L; // 30s
  while (submit_count_ > 0 && ObTimeUtility::fast_current_time() < max_wait_ts) {
    ob_usleep(1000 * 10);
  }
  if (submit_count_ > 0) {
    LOG_WARN_RET(OB_ERR_UNEXPECTED, "some request have not returned from file system", K(submit_count_));
  }
  destroy_thread();
  if (nullptr != io_context_) {
    device_handle_->io_destroy(io_context_);
    io_context_ = nullptr;
  }
  if (nullptr != io_events_) {
    device_handle_->free_io_events(io_events_);
    io_events_ = nullptr;
  }
  polling_timeout_.tv_sec = 0;
  polling_timeout_.tv_nsec = 0;
  submit_count_ = 0;
  device_handle_ = nullptr;
  depth_cond_.destroy();
  is_inited_ = false;
}

void ObAsyncIOChannel::run1()
{
  int ret = OB_SUCCESS;
  const int64_t thread_id = get_thread_idx();
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else {
    char device_name[32] = "";
    (void)device_handle_->get_device_name(device_name, sizeof(device_name));
    ObDIActionGuard program("IOManager", nullptr, nullptr);
    ObDIActionGuard module(ObDIActionGuard::NS_MODULE, "AsyncIO_Device:%s", device_name);
    set_thread_name("IO_GETEVENT", thread_id);
    LOG_INFO("io get_events thread started", K(thread_id), K(tg_id_));
    while (!has_set_stop()) {
      get_events();
    }
    LOG_INFO("io get_events thread stopped", K(thread_id), K(tg_id_));
  }
}

static int64_t get_io_depth(const int64_t io_size)
{
  const int64_t IO_SPLIT_SIZE = 512L * 1024L; // 512KB
  return upper_align(io_size, IO_SPLIT_SIZE) / IO_SPLIT_SIZE;
}

int ObAsyncIOChannel::submit(ObIORequest &req)
{
  int ret = OB_SUCCESS;
  const int64_t current_ts = ObTimeUtility::current_time();
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(device_handle_ != req.fd_.device_handle_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(req), KP(device_handle_));
  } else if (OB_ISNULL(req.io_result_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if ((!req.get_flag().is_detect()) && (submit_count_ >= MAX_AIO_EVENT_CNT - MAX_DETECT_DISK_HUNG_IO_CNT)) {
    ret = OB_EAGAIN;
    if (REACH_TIME_INTERVAL(1000000L)) {
      LOG_WARN("too many io requests", K(ret), K(submit_count_));
    }
  } else if (OB_UNLIKELY(current_ts > req.timeout_ts())) {
    ret = OB_TIMEOUT;
    LOG_WARN("io timeout because current time is larger than timeout timestamp", K(ret), K(current_ts), K(req));
  } else if ((!req.get_flag().is_detect()) && (device_channel_->used_io_depth_ > device_channel_->max_io_depth_ - MAX_DETECT_DISK_HUNG_IO_CNT)) {
    ret = OB_EAGAIN;
    if (REACH_TIME_INTERVAL(1 * 1000L * 1000L)) {
      FLOG_INFO("reach max io depth", K(ret), K(device_channel_->used_io_depth_), K(device_channel_->max_io_depth_));
    }
  } else {
    ATOMIC_INC(&submit_count_);
    ATOMIC_FAA(&device_channel_->used_io_depth_, get_io_depth(req.get_align_size()));
    if (OB_NOT_NULL(req.io_result_)) {
      req.io_result_->time_log_.submit_ts_ = ObTimeUtility::fast_current_time();
    }
    req.inc_ref("os_inc"); // ref for file system
    if (OB_FAIL(device_handle_->io_submit(io_context_, req.control_block_))) {
      ATOMIC_DEC(&submit_count_);
      req.dec_ref("os_dec"); // ref for file system
      LOG_WARN("io_submit failed", K(ret), K(submit_count_), K(req));
    } else {
      LOG_DEBUG("Success to submit io request, ", K(ret), K(submit_count_), KP(&req), KP(io_context_));
    }
  }
  return ret;
}

void ObAsyncIOChannel::cancel(ObIORequest &req)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else if (OB_ISNULL(req.io_result_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("io result is null", KR(ret), K(req));
  } else if (0 != req.io_result_->time_log_.submit_ts_ && 0 == req.io_result_->time_log_.return_ts_) {
    // Note: here if ob_io_cancel failed (possibly due to kernel not supporting io_cancel),
    // neither we or the get_events thread would call control.callback_->process(),
    // as we previously set need_callback to false.
    if (OB_FAIL(device_handle_->io_cancel(io_context_, req.control_block_))) {
      LOG_DEBUG("cancel io request failed", K(ret), K(req), KP(io_context_));
    } else {
      RequestHolder holder(&req);
      ATOMIC_DEC(&submit_count_);
      ATOMIC_FAS(&device_channel_->used_io_depth_, get_io_depth(req.get_align_size()));
      req.dec_ref("os_dec"); // ref for file system
      LOG_DEBUG("The IO Request has been canceled!");
      LOG_WARN("Shouldn't go here, io cancel not supported", K(ret), K(req));
    }
  }
}

int64_t ObAsyncIOChannel::get_queue_count() const
{
  return submit_count_;
}

void ObAsyncIOChannel::get_events()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else if (OB_FAIL(device_handle_->io_getevents(io_context_, 1/*min_nr*/, io_events_, &polling_timeout_))) {
    if (REACH_TIME_INTERVAL(10 * 1000 * 1000)) {
      LOG_ERROR("io get_events failed", K(ret));
    }
  } else if (io_events_->get_complete_cnt() > 0) {
    const int64_t io_return_time = ObTimeUtility::fast_current_time();
    ObIORequest *req = nullptr;
    for (int64_t i = 0; i < io_events_->get_complete_cnt(); ++i) { // ignore ret
      if (OB_ISNULL(req = reinterpret_cast<ObIORequest *>(io_events_->get_ith_data(i)))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("req is null", K(ret));
      } else {
        RequestHolder holder(req);
        ObTraceIdGuard trace_id_guard(req->trace_id_);
        req->dec_ref("os_dec"); // ref for file system
        if (OB_NOT_NULL(req->io_result_)) {
          req->io_result_->time_log_.return_ts_ = ObTimeUtility::fast_current_time();
        }
        int64_t io_size = req->get_align_size();
        ATOMIC_FAS(&device_channel_->used_io_depth_, get_io_depth(io_size));
        const int system_errno = io_events_->get_ith_ret_code(i);
        const int complete_size = io_events_->get_ith_ret_bytes(i);
        if (OB_LIKELY(0 == system_errno)) { // io succ
          ObDIActionGuard("IO success");
          if (complete_size == io_size) { // full complete
            LOG_DEBUG("Success to get io event", K(*req), K(complete_size));
            if (OB_FAIL(on_full_return(*req, io_size))) {
              LOG_WARN("process full return io request failed", K(ret), K(*req));
            }
          } else if (complete_size >= 0 && complete_size < io_size) { // partial complete
            LOG_WARN("io request partial finished", K(*req), K(complete_size));
            int64_t aligned_size = 0;
            if (OB_ISNULL(req->fd_.device_handle_)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("device handle is null", K(ret), K(req));
            } else if (OB_FAIL(req->fd_.device_handle_->get_io_aligned_size(aligned_size))) {
              LOG_WARN("fail to get io aligned size", K(ret), K(*req));
            } else if (0 == complete_size || !is_io_aligned(complete_size, aligned_size)) { // reach end of file
              if (OB_FAIL(on_partial_return(*req, complete_size))) {
                LOG_WARN("process partial return io request failed", K(ret), K(complete_size), K(*req));
              }
            } else {
              if (OB_FAIL(on_partial_retry(*req, complete_size))) { // partial retry
                LOG_WARN("partial retry io request failed", K(ret), K(complete_size), K(*req));
              }
            }
          } else { // invalid complete size
            LOG_WARN("invalid complete size", K(*req), K(complete_size));
            if (OB_FAIL(on_failed(*req, ObIORetCode(OB_IO_ERROR, complete_size)))) { // use complete_size as errno here
              LOG_WARN("process failed io request failed", K(ret), K(*req));
            }
          }
        } else { // io failed
          ObDIActionGuard("IO failed");
          LOG_ERROR("io request failed", K(system_errno), K(complete_size), K(*req));
          if (-EAGAIN == system_errno) { //retry
            if (OB_FAIL(on_full_retry(*req))) {
              LOG_WARN("retry io request failed", K(ret), K(system_errno), K(*req));
            }
          } else {
            if (OB_FAIL(on_failed(*req, ObIORetCode(OB_IO_ERROR, system_errno)))) {
              LOG_WARN("process failed io request failed", K(ret), K(*req));
            }
          }
        }
      }
      ATOMIC_DEC(&submit_count_);
    }
  }
}

int ObAsyncIOChannel::start_thread()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(TG_CREATE(lib::TGDefIDs::IO_CHANNEL, tg_id_))) {
    LOG_WARN("create thread group id failed", K(ret));
  } else if (OB_FAIL(TG_SET_RUNNABLE_AND_START(tg_id_, *this))) {
    LOG_WARN("start channel thread failed", K(ret), K(tg_id_));
  }
  return ret;
}

void ObAsyncIOChannel::destroy_thread()
{
  if (tg_id_ >= 0) {
    TG_STOP(tg_id_);
    TG_WAIT(tg_id_);
    TG_DESTROY(tg_id_);
    tg_id_ = -1;
  }
}

int ObAsyncIOChannel::on_full_return(ObIORequest &req, const int64_t complete_size)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(req.io_result_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("io result is null", K(ret));
  } else {
    req.io_result_->complete_size_ = complete_size;
    if (req.can_callback()) {
      if (OB_FAIL(req.tenant_io_mgr_.get_ptr()->enqueue_callback(req))) {
        LOG_WARN("push io request into callback queue failed", K(ret), K(req));
        req.io_result_->finish(ret, &req);
      }
    } else {
      if (OB_FAIL(req.recycle_buffer())) {
        LOG_WARN("recycle io raw buffer failed", K(ret), K(req));
      }
      req.io_result_->finish(ret, &req);
    }
  }
  return ret;
}

int ObAsyncIOChannel::on_partial_return(ObIORequest &req, const int64_t complete_size)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(req.io_result_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("io result is null", K(ret));
  } else {
    // partial return ignore callback
    req.io_result_->complete_size_ += complete_size;
    if (req.get_data_size() >= req.io_result_->size_) {
      // in case of aligned_size > file_size > user_need_size
      if (req.can_callback()) {
        // the callback is not aware of complete size, not supported for now
        req.io_result_->finish(OB_NOT_SUPPORTED, &req);
      } else {
        if (OB_FAIL(req.recycle_buffer())) {
          LOG_WARN("recycle io raw buffer failed", K(ret), K(req));
        }
        req.io_result_->finish(ret, &req);
      }
    } else {
      if (OB_FAIL(req.recycle_buffer())) {
        LOG_WARN("recycle io raw buffer failed", K(ret), K(req));
      }
      req.io_result_->finish(OB_DATA_OUT_OF_RANGE, &req);
    }
  }
  return ret;
}

int ObAsyncIOChannel::on_partial_retry(ObIORequest &req, const int64_t complete_size)
{
  int ret = OB_SUCCESS;
  int64_t aligned_size = 0;
  if (OB_ISNULL(req.fd_.device_handle_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("device handle is null", K(ret), K(req));
  } else if (OB_FAIL(req.fd_.device_handle_->get_io_aligned_size(aligned_size))) {
    LOG_WARN("fail to get io aligned size", K(ret), K(req));
  } else if (OB_UNLIKELY(!is_io_aligned(complete_size, aligned_size))) {
    ret = OB_ERR_SYS;
    LOG_WARN("complete size not aligned", K(ret), K(complete_size), K(aligned_size));
  } else if (OB_ISNULL(req.io_result_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("io result is null", K(ret));
  } else {
    int64_t next_offset = req.get_align_offset();
    int64_t next_size = req.get_align_size();
    req.io_result_->complete_size_ += complete_size;
    next_offset += req.io_result_->complete_size_;
    next_size -= req.io_result_->complete_size_;
    char *retry_buf = req.calc_io_buf() + req.io_result_->complete_size_;

    if (OB_FAIL(req.prepare(retry_buf, next_size, next_offset))) {
      LOG_WARN("prepare io request failed", K(ret), K(req));
    } else if (OB_FAIL(submit(req))) {
      LOG_WARN("submit io request failed", K(ret), K(req));
    } else if (OB_ISNULL(req.control_block_)) {
      ret = OB_ERR_SYS;
      LOG_WARN("io control block is null", KR(ret), K(req));
    } else {
      req.control_block_->set_is_retry(true);
    }
  }
  if (OB_FAIL(ret)) {
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = on_failed(req, ObIORetCode(ret)))) {
      LOG_WARN("deal with failed request failed", K(tmp_ret), K(ret), K(req));
    }
  }
  return ret;
}

int ObAsyncIOChannel::on_full_retry(ObIORequest &req)
{
  int ret = OB_SUCCESS;
  static const int64_t MAX_RETRY_COUNT = 10;
  if (++req.retry_count_ > MAX_RETRY_COUNT) {
    ret = OB_IO_ERROR;
    LOG_WARN("retry too many times", K(ret), K(req));
  } else if (OB_ISNULL(req.io_result_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("io result is null", K(ret));
  } else if (FALSE_IT(req.io_result_->complete_size_ = 0)) {
  } else if (OB_FAIL(req.prepare())) {
    LOG_WARN("prepare io request failed", K(ret), K(req));
  } else if (OB_FAIL(submit(req))) {
    LOG_WARN("submit io request failed", K(ret));
  } else if (OB_ISNULL(req.control_block_)) {
    ret = OB_ERR_SYS;
    LOG_WARN("io control block is null", KR(ret), K(req));
  } else {
    req.control_block_->set_is_retry(true);
  }
  if (OB_FAIL(ret)) {
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = on_failed(req, ObIORetCode(ret)))) {
      LOG_WARN("deal with failed request failed", K(tmp_ret), K(ret), K(req));
    }
  }
  return ret;
}

int ObAsyncIOChannel::on_failed(ObIORequest &req, const ObIORetCode &ret_code)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(req.io_result_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("io result is null", K(ret));
  } else {
    req.io_result_->finish(ret_code, &req);
  }
  return ret;
}


/******************             SyncIOChannel              **********************/
ObSyncIOChannel::ObSyncIOChannel()
  : is_inited_(false), used_io_depth_(0)
{
}

ObSyncIOChannel::~ObSyncIOChannel()
{
  destroy();
}

int ObSyncIOChannel::init(ObDeviceChannel *device_channel, const int64_t thread_num)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(is_inited_));
  } else if (thread_num < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(thread_num));
  } else if (OB_FAIL(base_init(device_channel))) {
    LOG_WARN("base init failed", K(ret), KP(device_channel));
  } else if (OB_FAIL(start_thread(cal_thread_count(thread_num), SYNC_IO_TASK_COUNT))) {
    LOG_WARN("start thread failed", K(ret), K(thread_num));
  } else {
    is_inited_ = true;
  }

  if (OB_UNLIKELY(!is_inited_)) {
    destroy();
  }
  return ret;

}

void ObSyncIOChannel::destroy()
{
  destroy_thread();
  device_handle_ = nullptr;
  is_inited_ = false;
}

void ObSyncIOChannel::handle(void *task)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else if (OB_ISNULL(task)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("task is NULL", K(ret));
  } else {
    char device_name[32] = "";
    (void)device_handle_->get_device_name(device_name, sizeof(device_name));
    ObDIActionGuard program("IOManager", nullptr, nullptr);
    ObDIActionGuard module(ObDIActionGuard::NS_MODULE, "SyncIO_Device:%s", device_name);
    ObIORequest *req = static_cast<ObIORequest *>(task);
    RequestHolder holder(req);
    ObTraceIdGuard trace_id_guard(req->trace_id_);
    if (OB_FAIL(do_sync_io(*req))) {
      LOG_WARN("do sync io failed", K(ret), KPC(req));
    }
    ATOMIC_FAA(&used_io_depth_, -get_io_depth(req->get_align_size()));
    req->dec_ref("sync_dec"); // ref for file system
  }
  UNUSED(ret);
}

int ObSyncIOChannel::submit(ObIORequest &req)
{
  int ret = OB_SUCCESS;
  const int64_t current_ts = ObTimeUtility::current_time();
  const int64_t io_depth = get_io_depth(min(max(GMEMCONF.get_server_memory_limit() / 10, static_cast<int64_t>(500 * 1024L * 1024L)), static_cast<int64_t>(4 * 1024L * 1024L * 1024L)));
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(used_io_depth_ >= io_depth)) {
    ret = OB_EAGAIN;
    if (REACH_TIME_INTERVAL(1 * 1000L * 1000L)) {
      LOG_WARN("reach io depth", K(ret), K(io_depth), K(used_io_depth_));
    }
  } else {
    req.inc_ref("sync_inc"); // ref for file system
    ATOMIC_FAA(&used_io_depth_, get_io_depth(req.get_align_size()));
    if (OB_FAIL(ObSimpleThreadPool::push(&req))) {
      req.dec_ref("sync_dec"); // ref for file system
      if (OB_EAGAIN != ret) {
        LOG_WARN("push request failed", K(ret), K(req));
      } else {
        // io sender will retry submitting request
        if (REACH_TIME_INTERVAL(1000000L)) {
          LOG_WARN("too many io request in the sync channel", K(ret), K(get_queue_num()));
        }
      }
    }
  }
  return ret;
}

void ObSyncIOChannel::cancel(ObIORequest &req)
{
  int ret = OB_NOT_SUPPORTED;
  LOG_WARN("cancel is not supported on sync io channel", K(ret), K(req));
}

int64_t ObSyncIOChannel::get_queue_count() const
{
  return get_queue_num();
}

int ObSyncIOChannel::set_thread_count(const int64_t conf_thread_count)
{
  return ObSimpleThreadPool::set_max_thread_count(cal_thread_count(conf_thread_count));
}

int ObSyncIOChannel::start_thread(const int64_t thread_num, const int64_t task_num)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObSimpleThreadPool::set_adaptive_thread(1, thread_num))) {
    LOG_WARN("simple thread pool set adaptive thread failed", K(ret), K(thread_num));
  } else if (OB_FAIL(ObSimpleThreadPool::init(thread_num, task_num, "IO_SYNC_CH"))) {
    LOG_WARN("simple thread pool init failed", K(ret), K(thread_num), K(task_num));
  }
  return ret;
}

void ObSyncIOChannel::destroy_thread()
{
  ObSimpleThreadPool::destroy();
}

int ObSyncIOChannel::do_sync_io(ObIORequest &req)
{
  int ret = OB_SUCCESS;
  const int64_t start_us = ObTimeUtility::current_time();
  int64_t io_size = -1;
  int64_t io_offset = -1;
  ObIODevice *device_handle = req.fd_.device_handle_;

  const int64_t timeout_us = MIN(req.get_remained_io_timeout_us(), 
      OB_IO_MANAGER.get_object_storage_io_timeout_ms(req.tenant_id_) * 1000LL);
  ObObjectStorageTenantGuard guard(req.tenant_id_, timeout_us);

  // no need to perform io for req that has already been canceled
  if (req.is_canceled()) {
    ret = OB_CANCELED;
    LOG_WARN("req is canceled", K(ret), K(req));
  } else if (OB_ISNULL(device_handle)) {
    ret = OB_ERR_SYS;
    LOG_WARN("device handle is null", K(ret));
  } else if (OB_ISNULL(req.io_result_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("io result is null", K(ret));
  } else if (FALSE_IT(io_offset = static_cast<int64_t>(req.io_result_->offset_))) {
  } else if (FALSE_IT(req.io_result_->time_log_.submit_ts_ = ObTimeUtility::fast_current_time())) {
  } else if (req.get_flag().is_read()) {
    if (OB_FAIL(check_io_hang_errsim())) {
    } else if (OB_FAIL(device_handle->pread(req.fd_, io_offset, req.io_result_->size_, req.calc_io_buf(), io_size))) {
        LOG_WARN("pread failed", K(ret), K(req));
    }
  } else if (req.get_flag().is_write()) {
    if (device_handle->is_local_device()) {
      if (OB_FAIL(check_io_hang_errsim())) {
      } else if (OB_FAIL(device_handle->pwrite(req.fd_, io_offset, req.io_result_->size_, req.calc_io_buf(), io_size))) {
        LOG_WARN("pwrite failed", K(ret), K(req));
      }
    } else {
      int flag = -1;
      ObFdSimulator::get_fd_flag(req.fd_, flag);
      if (ObStorageAccessType::OB_STORAGE_ACCESS_OVERWRITER == flag) {
        if (0 == req.io_result_->size_) {
          char buf = '\0';
          if (OB_FAIL(device_handle->write(req.fd_, &buf, req.io_result_->size_, io_size))) {
            LOG_WARN("write failed", K(ret), K(req));
          }
        } else {
          if (OB_FAIL(device_handle->write(req.fd_, req.calc_io_buf(), req.io_result_->size_, io_size))) {
            LOG_WARN("write failed", K(ret), K(req));
          }
        }
      } else if ((ObStorageAccessType::OB_STORAGE_ACCESS_APPENDER == flag)
                 || (ObStorageAccessType::OB_STORAGE_ACCESS_MULTIPART_WRITER == flag)) {
        if (OB_FAIL(device_handle->pwrite(req.fd_, io_offset, req.io_result_->size_, req.calc_io_buf(), io_size))) {
          LOG_WARN("pwrite failed", K(ret), K(req));
        }
      } else if (OB_STORAGE_ACCESS_DIRECT_MULTIPART_WRITER == flag) {
        if (OB_FAIL(device_handle->upload_part(req.fd_, req.calc_io_buf(),
                                                req.io_result_->size_,
                                                req.part_id_,
                                                io_size))) {
          LOG_WARN("direct upload part failed", K(ret), K(req), K(flag), K(req.io_result_));
        }
      } else if (OB_STORAGE_ACCESS_BUFFERED_MULTIPART_WRITER == flag) {
        // buffered multipart writer does not require reallocation of memory and data copying
        if (OB_FAIL(device_handle->upload_part(req.fd_, nullptr,
                                                req.io_result_->size_,
                                                req.part_id_,
                                                io_size))) {
          LOG_WARN("buffered upload part failed", K(ret), K(req), K(flag), K(req.io_result_));
        }
      } else {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("storage access type not supported", K(ret), K(flag), K(req));
      }
    }
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("not supported io mode", K(ret), K(req));
  }
  if (OB_SUCC(ret)) {
    req.io_result_->time_log_.return_ts_ = ObTimeUtility::fast_current_time();
    req.io_result_->complete_size_ = io_size;
    if (req.can_callback()) {
      if (OB_FAIL(req.tenant_io_mgr_.get_ptr()->enqueue_callback(req))) {
        LOG_WARN("push io request into callback queue failed", K(ret), K(req));
        req.io_result_->finish(ret, &req);
      }
    } else {
      if (OB_FAIL(req.recycle_buffer())) {
        LOG_WARN("recycle io raw buffer failed", K(ret), K(req));
      }
      req.io_result_->finish(ret, &req);
    }
  } else {
    req.io_result_->finish(ret, &req);
  }
  const int64_t cost_us = ObTimeUtility::current_time() - start_us;
  if (OB_UNLIKELY(cost_us > 3 * 1000 * 1000LL)) { // 3s
    LOG_WARN("slow sync io", KR(ret), K(cost_us), K(req));
  }
  return ret;
}

int64_t ObSyncIOChannel::cal_thread_count(const int64_t conf_thread_count)
{
  int64_t thread_count = conf_thread_count;
  if (0 == conf_thread_count) {
    const int64_t cpu_num = get_cpu_count();
    if (cpu_num <= 16) {
      thread_count = cpu_num * 6;
    } else {
      thread_count = 16 * 6 + (cpu_num - 16) * 2;
      thread_count = min(thread_count, 1024);
    }
  }
  return thread_count;
}

/******************             DeviceChannel              **********************/
ObDeviceChannel::ObDeviceChannel()
  : is_inited_(false),
    allocator_(nullptr),
    device_handle_(nullptr),
    used_io_depth_(0),
    max_io_depth_(0)
{

}

ObDeviceChannel::~ObDeviceChannel()
{
  destroy();
}

int ObDeviceChannel::init(ObIODevice *device_handle,
                          const int64_t async_channel_thread_count,
                          const int64_t sync_channel_thread_count,
                          const int64_t max_io_depth,
                          ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(nullptr == device_handle
        || async_channel_thread_count < 0  /* object device async channel count allow set 0 */
        || sync_channel_thread_count < 0
        || max_io_depth <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(device_handle), K(async_channel_thread_count), K(sync_channel_thread_count), K(max_io_depth));
  } else {
    device_handle_ = device_handle;
    used_io_depth_ = 0;
    max_io_depth_ = max_io_depth;
    allocator_ = &allocator;
    // one thread per channel
    for (int64_t i = 0; OB_SUCC(ret) && i < async_channel_thread_count; ++i) {
      ObAsyncIOChannel *ch = nullptr;
      void *buf = nullptr;
      if (OB_ISNULL(buf = allocator.alloc(sizeof(ObAsyncIOChannel)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("alloc async channel failed", K(ret), K(i), K(async_channel_thread_count));
      } else if (FALSE_IT(ch = new (buf) ObAsyncIOChannel())) {
      } else if (OB_FAIL(ch->init(this))) {
        LOG_WARN("init async channel failed", K(ret));
      } else if (OB_FAIL(ch->start())) {
        LOG_WARN("start async channel failed", K(ret));
      } else if (OB_FAIL(async_channels_.push_back(ch))) {
        LOG_WARN("push back async channel failed", K(ret), KPC(ch));
      } else {
        ch = nullptr;
      }
      if (OB_UNLIKELY(nullptr != ch)) {
        ch->~ObAsyncIOChannel();
        allocator.free(ch);
      }
    }
    // only one sync io channel
    {
      ObSyncIOChannel *ch = nullptr;
      void *buf = nullptr;
      if (OB_ISNULL(buf = allocator.alloc(sizeof(ObSyncIOChannel)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("alloc sync channel failed", K(ret), K(sync_channel_thread_count));
      } else if (FALSE_IT(ch = new (buf) ObSyncIOChannel())) {
      } else if (OB_FAIL(ch->init(this, sync_channel_thread_count))) { // init and start thread pool
        LOG_WARN("init async channel failed", K(ret));
      } else if (OB_FAIL(sync_channels_.push_back(ch))) {
        LOG_WARN("push back async channel failed", K(ret), KPC(ch));
      } else {
        ch = nullptr;
      }
      if (OB_UNLIKELY(nullptr != ch)) {
        ch->~ObSyncIOChannel();
        allocator.free(ch);
      }
    }
    if (OB_SUCC(ret)) {
      is_inited_ = true;
    }
  }
  if (OB_UNLIKELY(!is_inited_)) {
    destroy();
  }
  return ret;
}

void ObDeviceChannel::destroy()
{
  is_inited_ = false;
  for (int64_t i = 0; i < async_channels_.count(); ++i) {
    ObAsyncIOChannel *ch = static_cast<ObAsyncIOChannel *>(async_channels_.at(i));
    ch->stop();
  }
  for (int64_t i = 0; i < async_channels_.count(); ++i) {
    ObAsyncIOChannel *ch = static_cast<ObAsyncIOChannel *>(async_channels_.at(i));
    ch->wait();
  }
  for (int64_t i = 0; i < async_channels_.count(); ++i) {
    ObIOChannel *ch = async_channels_.at(i);
    if (nullptr != allocator_ && nullptr != ch) {
      ch->~ObIOChannel();
      allocator_->free(ch);
    }
  }
  async_channels_.destroy();
  for (int64_t i = 0; i < sync_channels_.count(); ++i) {
    ObIOChannel *ch = sync_channels_.at(i);
    if (nullptr != allocator_ && nullptr != ch) {
      ch->~ObIOChannel();
      allocator_->free(ch);
    }
  }
  sync_channels_.destroy();
  allocator_ = nullptr;
}

int ObDeviceChannel::reload_config(const ObIOConfig &conf)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < sync_channels_.count(); i++) {
    ObSyncIOChannel *ch = static_cast<ObSyncIOChannel *>(sync_channels_.at(i));
    if (OB_FAIL(ch->set_thread_count(conf.sync_io_thread_count_))) {
      LOG_WARN("set thread count failed", K(ret), K(conf));
    }
  }
  return ret;
}

int ObDeviceChannel::submit(ObIORequest &req)
{
  int ret = OB_SUCCESS;
  ObIOChannel *ch = nullptr;
  RequestHolder holder(&req);
  const bool is_sync = req.io_result_ != nullptr && req.get_flag().is_sync();
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else {
    if (is_sync) {
      lock_for_sync_io_.lock();
    }
    if (OB_FAIL(get_random_io_channel(is_sync ? sync_channels_ : async_channels_, ch))) {
      LOG_WARN("get random io channel failed", K(ret), K(sync_channels_.count()), K(is_sync));
    } else if (OB_FAIL(ch->submit(req))) {
      if (OB_EAGAIN != ret) {
        LOG_WARN("submit request failed", K(ret), K(req));
      }
    }
    if (is_sync) {
      lock_for_sync_io_.unlock();
    }
  }
  return ret;
}

void ObDeviceChannel::print_status()
{
  char buf[256];
  int64_t pos = 0;
  int64_t sync_total = 0;
  int64_t async_total = 0;
  common::databuff_printf(buf, sizeof(buf), pos, ", async_cnt=");
  for (int i = 0; i < async_channels_.count(); ++i) {
    ObIOChannel *channel = async_channels_.at(i);
    if (OB_NOT_NULL(channel)) {
      int64_t cnt = channel->get_queue_count();
      common::databuff_printf(buf, sizeof(buf), pos, " %ld", cnt);
      async_total += cnt;
    }
  }
  common::databuff_printf(buf, sizeof(buf), pos, ", sync_cnt=");
  for (int i = 0; i < sync_channels_.count(); ++i) {
    ObIOChannel *channel = sync_channels_.at(i);
    if (OB_NOT_NULL(channel)) {
      int64_t cnt = max(channel->get_queue_count(), 0);
      common::databuff_printf(buf, sizeof(buf), pos, " %ld", cnt);
      sync_total += cnt;
    }
  }
  if (0 != pos) {
    _LOG_INFO("[IO STATUS CHANNEL] device_id=%p, req_in_channel, async_total=%ld, sync_total=%ld%s", this, async_total, sync_total, buf);
  }
}

int ObDeviceChannel::get_random_io_channel(ObIArray<ObIOChannel *> &io_channels, ObIOChannel *&ch)
{
  int ret = OB_SUCCESS;
  ch = nullptr;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(io_channels.count() <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(io_channels.count()));
  } else {
    int64_t idx = 0;
    for (idx = 0; idx < io_channels.count(); idx++) {
      if (0 == io_channels.at(idx)->get_queue_count()) {
        break;
      }
    }
    if (idx == io_channels.count()) {
      const int64_t idx1 = ObRandom::rand(0, io_channels.count() - 1);
      const int64_t idx2 = ObRandom::rand(0, io_channels.count() - 1);
      idx = (io_channels.at(idx1)->get_queue_count() < io_channels.at(idx2)->get_queue_count() ? idx1 : idx2);
    }
    ch = io_channels.at(idx);
  }
  return ret;
}

/******************             IOCallbackManager              **********************/

ObIOCallbackManager::ObIOCallbackManager()
  : is_inited_(false),
    config_thread_count_(0)
{

}

ObIOCallbackManager::~ObIOCallbackManager()
{
  destroy();
}

int ObIOCallbackManager::init(const int64_t tenant_id, const int64_t thread_count,
                              const int32_t queue_depth)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(thread_count <= 0 || queue_depth <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(thread_count), K(queue_depth));
  } else {
    config_thread_count_ = thread_count;
    if (OB_FAIL(ObLinkQueueThreadPool::set_adaptive_thread(1, thread_count))) {
      LOG_WARN("set adaptive thread failed", K(ret), K(thread_count));
    } else if (OB_FAIL(ObLinkQueueThreadPool::init(thread_count, queue_depth, "DiskCB", tenant_id))) {
      LOG_WARN("init link thread pool failed", K(ret), K(thread_count), K(queue_depth));
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

int ObIOCallbackManager::enqueue_callback(ObIORequest &req)
{
  int ret = OB_SUCCESS;
  const int64_t current_ts = ObTimeUtility::current_time();
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("Not init", K(ret));
  } else if (OB_UNLIKELY(current_ts > req.timeout_ts())) {
    ret = OB_TIMEOUT;
    LOG_WARN("io timeout because current time is larger than timeout timestamp", K(ret), K(current_ts), K(req));
  } else if (OB_NOT_NULL(req.io_result_)) {
    ObThreadCondGuard guard(req.io_result_->get_cond());
    if (OB_FAIL(guard.get_ret())) {
      LOG_ERROR("fail to lock condition", K(ret));
    } else if (req.is_canceled()) {
      ret = OB_CANCELED;
    } else if (!req.can_callback()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(ret), K(req));
    } else {
      req.inc_ref("cb_inc"); // ref for callback queue
      if (OB_NOT_NULL(req.io_result_)) {
        req.io_result_->time_log_.callback_enqueue_ts_ = ObTimeUtility::fast_current_time();
      }
      // Create a wrapper task
      if (OB_FAIL(ObLinkQueueThreadPool::push(&req.req_node_))) {
        req.dec_ref("cb_dec"); // ref for callback queue
        LOG_WARN("push callback failed", K(ret), K(req));
      }
    }
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
    LOG_WARN("invalid argument", K(ret), KP(req));
  } else {
    ObDIActionGuard ag("IOManager", "DiskCB", "handle io callback");
    const int64_t begin_time = ObTimeUtility::fast_current_time();
    req->dec_ref("cb_dec"); // ref for callback queue
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
    { // callback must execute in guard, in case of cancel halfway
      if (OB_ISNULL(req->io_result_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("io result is null", K(ret));
      } else {
        ObTimeGuard time_guard("cb_process", 100000); //100ms
        ObThreadCondGuard guard(req->io_result_->get_cond());
        if (OB_FAIL(guard.get_ret())) {
          LOG_ERROR("Fail to lock req condition, ", K(ret), K(*req));
        } else if (FALSE_IT(time_guard.click("callback_guard"))) {
        } else if (req->is_canceled()) {
          // canceled, do nothing
        } else if (OB_UNLIKELY(begin_time > req->timeout_ts())) {
          ret = OB_TIMEOUT;
          LOG_WARN("io timeout because current time is larger than timeout timestamp", K(ret), "current_time", begin_time, KPC(req));
        } else if (!req->can_callback()) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("io request can not do callback", K(ret), K(*req));
        } else if (OB_FAIL(req->io_result_->ret_code_.io_ret_)) {
          //failed, ignore
        } else {
          if (nullptr != req->get_callback()) {
            if (OB_FAIL(req->get_callback()->process(req->get_io_data_buf(), req->io_result_->size_))) {
              LOG_WARN("fail to callback", K(ret), K(*req), K(MTL_ID()));
            }
            time_guard.click("callback_process");
          }
        }
        if (OB_UNLIKELY(time_guard.get_diff() > 100000)) {// 100ms
          //print req
          LOG_INFO("callback process cost too much time", K(ret), K(time_guard), K(req));
        }
      }
      //recycle buffer after process
      req->free_io_buffer();
    }
    req->io_result_->finish(ret, req);
  }
}

int ObIOCallbackManager::update_thread_count(const int64_t thread_count)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("Not init", K(ret));
  } else if (OB_UNLIKELY(thread_count <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(thread_count));
  } else {
    int64_t cur_thread_count = config_thread_count_;
    if (thread_count == cur_thread_count) {
      // do nothing
    } else {
      config_thread_count_ = thread_count;
      if (OB_FAIL(ObLinkQueueThreadPool::set_max_thread_count(thread_count))) {
        LOG_WARN("set max thread count failed", K(ret), K(thread_count));
      } else {
        LOG_INFO("update io callback thread count", K(ret), "old_thread_cnt", cur_thread_count, "new_thread_cnt", thread_count);
      }
    }
  }
  return ret;
}

int64_t ObIOCallbackManager::to_string(char *buf, const int64_t len) const
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  if (OB_FAIL(databuff_printf(buf, len, pos,
                              "IO_CALLBACK: thread_count=%ld, queue_count=%ld",
                              config_thread_count_, get_queue_num()))) {
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
    case DEVICE_HEALTH_ERROR:
      hstr = "ERROR";
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

ObIOFaultDetector::ObIOFaultDetector(const ObIOConfig &io_config)
  : is_inited_(false),
    lock_(ObLatchIds::IO_FAULT_DETECTOR_LOCK),
    io_config_(io_config),
    is_device_warning_(false),
    last_device_warning_ts_(0),
    is_device_error_(false),
    begin_device_error_ts_(0),
    last_device_error_ts_(0)
{

}

ObIOFaultDetector::~ObIOFaultDetector()
{
  destroy();
}

int ObIOFaultDetector::init()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("io fault detector init twice", K(ret), K(!is_inited_));
  } else {
    is_inited_ = true;
  }
  if (OB_UNLIKELY(!is_inited_)) {
    destroy();
  }
  return ret;
}

void ObIOFaultDetector::destroy()
{
  TG_STOP(TGDefIDs::IO_HEALTH);
  TG_WAIT(TGDefIDs::IO_HEALTH);
  is_device_warning_ = false;
  is_device_error_ = false;
  begin_device_error_ts_ = 0;
  last_device_error_ts_ = 0;
  is_inited_ = false;
}

int ObIOFaultDetector::start()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("io fault detector not init", K(ret), KP(is_inited_));
  } else if (OB_FAIL(TG_SET_HANDLER_AND_START(TGDefIDs::IO_HEALTH, *this))) {
    LOG_WARN("start thread pool failed", K(ret));
  }
  return ret;
}

struct RetryTask
{
  ObIOInfo io_info_;
  int64_t timeout_ms_;
  TO_STRING_KV(K(io_info_), K(timeout_ms_));
};

void ObIOFaultDetector::handle(void *task)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("io fault detector not init", K(ret), KP(is_inited_));
  } else if (OB_ISNULL(task)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(task));
  } else {
    const int64_t LONG_AIO_TIMEOUT_MS = 30000; // 30s
    RetryTask *retry_task = reinterpret_cast<RetryTask *>(task);
    retry_task->io_info_.flag_.set_unlimited();
    retry_task->io_info_.flag_.set_detect();
    if ((is_device_warning_ || is_device_error_) && retry_task->io_info_.flag_.is_time_detect()) {
      //ignore
    } else if (!is_supported_detect_read_(retry_task->io_info_.tenant_id_, retry_task->io_info_.fd_)) {
      //ignore
    } else {
      int64_t timeout_ms = retry_task->timeout_ms_;
      // remain 1s to avoid race condition for retry_black_list_interval
      const int64_t retry_black_list_interval_ms = io_config_.read_failure_black_list_interval_ / 1000L - 1000L;
      // rety_io_timeout must less than black_list_interval
      const int64_t MIN_IO_RETRY_TIMEOUT_MS = min(static_cast<int64_t>(10L * 1000L)/* 10s */, retry_black_list_interval_ms);
      const int64_t MAX_IO_RETRY_TIMEOUT_MS = min(static_cast<int64_t>(180L * 1000L)/* 180s*/, retry_black_list_interval_ms);
      const int64_t diagnose_begin_ts = ObTimeUtility::fast_current_time();
      bool is_retry_succ = false;
      int64_t fs_error_times = 0;
      while (OB_SUCC(ret) && !OB_IO_MANAGER.is_stopped() && !is_retry_succ && !is_device_error_) {
        ObIOHandle handle;
        const int64_t current_retry_ts = ObTimeUtility::fast_current_time();
        const int64_t warn_ts = diagnose_begin_ts + io_config_.data_storage_warning_tolerance_time_;
        const int64_t error_ts = diagnose_begin_ts + io_config_.data_storage_error_tolerance_time_;
        const int64_t left_timeout_ms = !is_device_warning_ ?
          (warn_ts - current_retry_ts) / 1000 : (error_ts - current_retry_ts) / 1000;
        // timeout of retry io increase exponentially
        timeout_ms = min(left_timeout_ms, min(MAX_IO_RETRY_TIMEOUT_MS, max(timeout_ms * 2, MIN_IO_RETRY_TIMEOUT_MS)));
        int sys_io_errno = 0;
        if (timeout_ms > 0) {
          // do retry io
          retry_task->io_info_.timeout_us_= timeout_ms * 1000;
          if (OB_FAIL(OB_IO_MANAGER.detect_read(retry_task->io_info_, handle))) {
            int tmp_ret = OB_SUCCESS;
            if (OB_TMP_FAIL(handle.get_fs_errno(sys_io_errno))) {
              LOG_WARN("get fs errno num failed", K(ret), K(sys_io_errno));
            }
            if (OB_TIMEOUT == ret || OB_IO_TIMEOUT == ret) {
              LOG_WARN("ObIOManager::read failed", K(ret), K(retry_task->io_info_), K(timeout_ms));
              ret = OB_SUCCESS;
            } else if (OB_EAGAIN == ret) { //maybe channel is busy, wait and retry
              ob_usleep(100 * 1000); // 100ms
              ret = OB_SUCCESS;
            } else if (OB_SUCCESS == tmp_ret && sys_io_errno != 0) {
              ++ fs_error_times;
              ret = OB_SUCCESS;
            } else {
              LOG_WARN("ObIOManager::retry read request failed", K(ret), K(retry_task->io_info_));
            }
          } else {
            is_retry_succ = true;
          }
        }
        if (OB_SUCC(ret) && !is_retry_succ) {
          const int64_t current_ts = ObTimeUtility::fast_current_time();
          if (current_ts >= error_ts || (sys_io_errno != 0 && fs_error_times >= MAX_DETECT_READ_ERROR_TIMES)) {
            set_device_error();
            LOG_WARN("ObIOManager::detect IO retry timeout, device error", K(ret), K(current_ts), K(error_ts), K(retry_task->io_info_));
          } else if (current_ts >= warn_ts || (sys_io_errno != 0 && fs_error_times >= MAX_DETECT_READ_WARN_TIMES)) {
            set_device_warning();
            LOG_WARN("ObIOManager::detect IO retry reach limit, device warning", K(ret), K(sys_io_errno), K(current_ts), K(current_ts), K(fs_error_times), K(retry_task->io_info_));
          }
        }
      }
    }
    op_free(const_cast<RetryTask *>(retry_task));
    retry_task = nullptr;
  }
}

int ObIOFaultDetector::get_device_health_status(ObDeviceHealthStatus &dhs,
    int64_t &device_abnormal_time)
{
  int ret = OB_SUCCESS;
  dhs = DEVICE_HEALTH_NORMAL;
  device_abnormal_time = 0;

  if (is_device_warning_ && last_device_warning_ts_ > 0 && !is_device_error_) {
    const int64_t period = ObTimeUtility::fast_current_time() - last_device_warning_ts_;
    if (period > io_config_.read_failure_black_list_interval_) {
      last_device_warning_ts_ = 0;
      is_device_warning_ = false;
    }
  }

  if (is_device_error_) {
    dhs = DEVICE_HEALTH_ERROR;
    device_abnormal_time = begin_device_error_ts_;
  } else if (is_device_warning_) {
    dhs = DEVICE_HEALTH_WARNING;
    device_abnormal_time = last_device_warning_ts_;
  } else {
    dhs = DEVICE_HEALTH_NORMAL;
    device_abnormal_time = 0;
  }

  return ret;
}

void ObIOFaultDetector::reset_device_health()
{
  is_device_warning_ = false;
  last_device_warning_ts_ = 0;
  is_device_error_ = false;
  begin_device_error_ts_ = 0;
  last_device_error_ts_ = 0;
}

int ObIOFaultDetector::record_timing_task(const int64_t first_id, const int64_t second_id)
{
  int ret = OB_SUCCESS;
  RetryTask *retry_task = nullptr;
  if (OB_ISNULL(retry_task = op_alloc(RetryTask))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("alloc RetryTask failed", K(ret));
  } else {
    retry_task->io_info_.tenant_id_ = OB_SERVER_TENANT_ID;
    retry_task->io_info_.size_ = 4096;
    retry_task->io_info_.user_data_buf_ = nullptr;
    retry_task->io_info_.buf_ = nullptr;
    retry_task->io_info_.flag_.set_mode(ObIOMode::READ);
    retry_task->io_info_.flag_.set_wait_event(ObWaitEventIds::DB_FILE_DATA_READ);
    retry_task->io_info_.flag_.set_time_detect();
    retry_task->io_info_.fd_.first_id_ = first_id;
    retry_task->io_info_.fd_.second_id_ = second_id;
    retry_task->io_info_.fd_.device_handle_ = &LOCAL_DEVICE_INSTANCE;
    retry_task->io_info_.offset_ = 0;
    retry_task->io_info_.callback_ = nullptr;
    retry_task->timeout_ms_ = io_config_.data_storage_warning_tolerance_time_; // default 5s
    if (OB_FAIL(TG_PUSH_TASK(TGDefIDs::IO_HEALTH, retry_task))) {
      LOG_WARN("io fault detector push task failed", K(ret), KP(retry_task));
    }
    if (OB_FAIL(ret)) {
      op_free(retry_task);
      retry_task = nullptr;
    }
  }
  return ret;
}

int ObIOFaultDetector::set_detect_task_io_info_(
    ObIOInfo &io_info, const ObIOResult &result, const ObIORequest &req)
{
  int ret = OB_SUCCESS;
  io_info.tenant_id_ = req.tenant_id_;
  io_info.timeout_us_ = result.timeout_us_;
  io_info.callback_ = nullptr;
  io_info.buf_ = result.buf_;
  io_info.user_data_buf_ = nullptr;
  io_info.flag_ = result.flag_;
  io_info.size_ = result.size_;
  io_info.offset_ = result.offset_;
  io_info.flag_.set_sys_module_id(ObIOModule::DETECT_IO);
  io_info.fd_ = req.fd_;
  return ret;
}

bool ObIOFaultDetector::is_supported_detect_read_(const uint64_t tenant_id, const ObIOFd &fd)
{
  bool bret = true;
  int ret = OB_SUCCESS;
#ifdef OB_BUILD_SHARED_STORAGE
  if (GCTX.is_shared_storage_mode()) {
    if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id))) {
      bret = false;
    } else if (is_virtual_tenant_id(tenant_id)) {
      // In SS mode, server tenant does not have micro cache file,
      // thus it's unnecessary to execute detect tasks
      bret = false;
    } else {
      MAKE_TENANT_SWITCH_SCOPE_GUARD(guard);
      if (OB_SUCC(guard.switch_to(tenant_id, false/*need_check_allow*/))) {
        ObTenantFileManager *tenant_file_mgr = MTL(ObTenantFileManager*);
        const int micro_cache_file_fd = tenant_file_mgr->get_micro_cache_file_fd();
        if (micro_cache_file_fd == OB_INVALID_FD) {
          // micro cache file not exist
          bret = false;
        } else if (micro_cache_file_fd != fd.second_id_) {
          ret = OB_NOT_SUPPORTED;
          bret = false;
          LOG_INFO("in shared_storage mode, only micro_cache_file reads are supported for detection",
              KR(ret), K(tenant_id), K(fd), K(micro_cache_file_fd));
        }
      } else {
        bret = false;
      }
    }
  }
#endif
  return bret;
}

void ObIOFaultDetector::record_io_timeout(const ObIOResult &result, const ObIORequest &req)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("io fault detector not init", K(ret), K(is_inited_));
  } else if (result.flag_.is_detect()) {
    //ignore, do not retry
  } else if (req.get_flag().is_sync()) {
    LOG_INFO("ignore fault detect for sync io", K(req));
  } else if (result.flag_.is_read() && !result.is_object_device_req_) {
    RetryTask *retry_task = nullptr;
    if (OB_ISNULL(retry_task = op_alloc(RetryTask))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("alloc RetryTask failed", K(ret));
    } else if (OB_FAIL(set_detect_task_io_info_(retry_task->io_info_, result, req))) {
      LOG_WARN("fail to set detect task fd", KR(ret), K(result), K(req));
    } else {
      retry_task->timeout_ms_ = io_config_.data_storage_warning_tolerance_time_; // default 5s
      if (OB_FAIL(TG_PUSH_TASK(TGDefIDs::IO_HEALTH, retry_task))) {
        LOG_WARN("io fault detector push task failed", K(ret), KPC(retry_task));
      }
      if (OB_FAIL(ret)) {
        op_free(retry_task);
        retry_task = nullptr;
      }
    }
  } else if (result.flag_.is_write()) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("write IORequest failed", K(ret), K(result));
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("not supported io mode", K(ret), K(result));
  }
}

void ObIOFaultDetector::record_io_error(const ObIOResult &result, const ObIORequest &req)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("io fault detector not init", K(ret), K(is_inited_));
  } else if (result.flag_.is_detect()) {
    //ignore, do not retry
  } else if (req.get_flag().is_sync()) {
    LOG_INFO("ignore fault detect for sync io", K(req));
  } else if (result.flag_.is_read() && !result.is_object_device_req_) {
    if (OB_FAIL(record_read_failure_(result, req))) {
      LOG_WARN("record read failure failed", K(ret), K(result), K(req));
    }
  } else if (result.flag_.is_write()) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("write IORequest failed", K(ret), K(result));
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("not supported io mode", K(ret), K(result));
  }
}

int ObIOFaultDetector::record_read_failure_(const ObIOResult &result, const ObIORequest &req)
{
  int ret = OB_SUCCESS;
  RetryTask *retry_task = nullptr;
  if (OB_ISNULL(retry_task = op_alloc(RetryTask))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("alloc RetryTask failed", K(ret));
  } else if (OB_FAIL(set_detect_task_io_info_(retry_task->io_info_, result, req))) {
    LOG_WARN("fail to set detect task fd", KR(ret), K(result), K(req));
  } else {
    retry_task->timeout_ms_ = 5000L; // 5s
    if (OB_FAIL(TG_PUSH_TASK(TGDefIDs::IO_HEALTH, retry_task))) {
      LOG_WARN("io fault detector push task failed", K(ret), KPC(retry_task));
    } else {
      LOG_INFO("io fault detector push task", KPC(retry_task));
    }
    if (OB_FAIL(ret)) {
      op_free(retry_task);
      retry_task = nullptr;
    }
  }
  return ret;
}

// set disk warning and record warn_ts
// until warn_ts + io_config.read_failure_black_list_interval, this server is not allowed to be partition leader
void ObIOFaultDetector::set_device_warning()
{
  last_device_warning_ts_ = ObTimeUtility::fast_current_time();
  is_device_warning_ = true;
  LOG_WARN_RET(OB_IO_ERROR, "disk maybe corrupted");
}

// set disk error and record error_ts
// if the disk is confirmed normal, the administrator can reset disk status by:
// alter system set disk valid server [=] 'ip:port'
void ObIOFaultDetector::set_device_error()
{
  if (!is_device_warning_) {
    set_device_warning();
  }
  if (!is_device_error_) {
    begin_device_error_ts_ = ObTimeUtility::fast_current_time();
  }
  last_device_error_ts_ = ObTimeUtility::fast_current_time();
  is_device_error_ = true;
  LOG_ERROR_RET(OB_IO_ERROR, "set_disk_error: attention!!!");
  LOG_DBA_ERROR_V2(OB_COMMON_DISK_INVALID, OB_DISK_ERROR,
                    "The disk may be corrupted. ",
                    "[suggestion] check disk.");
}

ObIOTracer::ObIOTracer()
  : is_inited_(false), tenant_id_(OB_INVALID_TENANT_ID), trace_map_()
{

}

ObIOTracer::~ObIOTracer()
{
  destroy();
}

int ObIOTracer::init(const uint64_t tenant_id)
{
  int ret = OB_SUCCESS;
  const ObMemAttr attr = SET_USE_500("io_trace_map");
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tenant_id));
  } else if (OB_FAIL(trace_map_.create(1009, attr))) {
    LOG_WARN("create trace map failed", K(ret));
  } else {
    tenant_id_ = tenant_id;
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
  tenant_id_ = OB_INVALID_TENANT_ID;
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
    LOG_WARN("the io tracer is not init", K(ret));
  } else if (OB_UNLIKELY(nullptr == req)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(req), KCSTRING(msg), K(trace_type));
  } else if (ObIOTracer::TraceType::IS_FIRST == trace_type) {
    TraceInfo trace_info;
    char *bt_str = lbt();
    strncpy(trace_info.bt_str_, bt_str, sizeof(trace_info.bt_str_) - 1);
    trace_info.ref_log_.click(msg);
    if (OB_FAIL(trace_map_.set_refactored(reinterpret_cast<int64_t>(req), trace_info))) {
      LOG_WARN("add trace failed", K(ret));
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
      LOG_WARN("modify trace failed", K(ret));
    }
    if (OB_SUCC(ret) && ObIOTracer::TraceType::IS_LAST == trace_type) {
      if (OB_FAIL(trace_map_.erase_refactored(reinterpret_cast<int64_t>(req)))) {
        LOG_WARN("remove trace failed", K(ret));
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
            LOG_WARN("update backtrace count failed", K(ret));
          }
        } else {
          LOG_WARN("insert backtrace count failed", K(ret));
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
    LOG_WARN("init trace counter failed", K(ret));
  } else if (OB_FAIL(trace_map_.foreach_refactored(counter))) {
    LOG_WARN("count io trace failed", K(ret));
  } else if (counter.req_count_ > 0) {
    ObArray<TraceItem> trace_array;
    StoreFn store_fn(trace_array);
    if (OB_FAIL(trace_array.reserve(counter.bt_count_.size()))) {
      LOG_WARN("reserve trace array failed", K(ret));
    } else if (OB_FAIL(counter.bt_count_.foreach_refactored(store_fn))) {
      LOG_WARN("get max backtrace count failed", K(ret));
    } else {
      lib::ob_sort(trace_array.begin(), trace_array.end(), sort_fn);
      databuff_printf(buf, len, pos, "trace_request_count: %ld, distinct_backtrace_count: %ld; ", counter.req_count_, trace_array.count());
      const int64_t print_count = min(5, trace_array.count());
      for (int64_t i = 0; OB_SUCC(ret) && i < print_count; ++i) {
        const TraceItem &item = trace_array.at(i);
        databuff_printf(buf, len, pos, "top: %ld, count: %ld, ref_log: %s, backtrace: %s; ", i + 1, item.count_, to_cstring(item.trace_info_.ref_log_), item.trace_info_.bt_str_);
      }
    }
  }
  return pos;
}
void ObIOTracer::print_status()
{
  LOG_INFO("[IO STATUS TRACER]", K_(tenant_id), K(*this));
}
