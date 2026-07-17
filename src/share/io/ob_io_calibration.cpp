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


#include "ob_io_calibration.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "share/ob_io_device_helper.h"

using namespace oceanbase::lib;
using namespace oceanbase::common;


/******************             IOBenchLoad              **********************/

ObIOBenchLoad::ObIOBenchLoad()
  : mode_(ObIOMode::MAX_MODE),
    size_(0)
{

}

ObIOBenchLoad::~ObIOBenchLoad()
{

}

void ObIOBenchLoad::reset()
{
  mode_ = ObIOMode::MAX_MODE;
  size_ = 0;
}

bool ObIOBenchLoad::is_valid() const
{
  return mode_ < ObIOMode::MAX_MODE && size_ > 0 && size_ <= OB_DEFAULT_MACRO_BLOCK_SIZE;
}

/******************             IOBenchResult              **********************/

OB_SERIALIZE_MEMBER(ObIOBenchResult, mode_, size_, iops_, rt_us_);

ObIOBenchResult::ObIOBenchResult()
  : mode_(ObIOMode::MAX_MODE),
    size_(0),
    iops_(0),
    rt_us_(0)
{

}

ObIOBenchResult::~ObIOBenchResult()
{

}

void ObIOBenchResult::reset()
{
  mode_ = ObIOMode::MAX_MODE;
  size_ = 0;
  iops_ = 0;
  rt_us_ = 0;
}

bool ObIOBenchResult::is_valid() const
{
  return mode_ < ObIOMode::MAX_MODE
    && size_ > 0
    && iops_ > std::numeric_limits<double>::epsilon()
    && rt_us_ > std::numeric_limits<double>::epsilon();
}

bool ObIOBenchResult::operator==(const ObIOBenchResult &other) const
{
  return mode_ == other.mode_
    && size_ == other.size_
    && fabs(iops_ - other.iops_) < std::numeric_limits<double>::epsilon()
    && fabs(rt_us_ - other.rt_us_) < std::numeric_limits<double>::epsilon();
}

/******************             IOAbility              **********************/


ObIOAbility::ObIOAbility()
  : measure_items_()
{

}

ObIOAbility::~ObIOAbility()
{

}

void ObIOAbility::reset()
{
  for (int64_t i = 0; i < static_cast<int>(ObIOMode::MAX_MODE); ++i) {
    measure_items_[i].reset();
  }
}

bool ObIOAbility::is_valid() const
{
  bool bret = true;
  for (int64_t i = 0; bret && i < static_cast<int>(ObIOMode::MAX_MODE); ++i) {
    const MeasureItemArray &cur_array = measure_items_[i];
    bret = cur_array.count() > 0;
    for (int64_t j = 0; bret && j < cur_array.count(); ++j) {
      bret = cur_array.at(j).is_valid();
    }
  }
  return bret;
}


int ObIOAbility::assign(const ObIOAbility &other)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < static_cast<int>(ObIOMode::MAX_MODE); ++i) {
    if (OB_FAIL(measure_items_[i].assign(other.measure_items_[i]))) {
      LOG_WARN("assign measure items failed", K(ret), K(other));
    }
  }
  return ret;
}

bool ObIOAbility::operator==(const ObIOAbility &other) const
{
  bool is_equal = true;
  for (int64_t i = 0; is_equal && i < static_cast<int>(ObIOMode::MAX_MODE); ++i) {
    const MeasureItemArray &other_item_array = other.measure_items_[i];
    const MeasureItemArray &local_item_array = measure_items_[i];
    if (local_item_array.count() != other_item_array.count()) {
      is_equal = false;
    } else {
      for (int64_t j = 0; is_equal && j < local_item_array.count(); ++j) {
        is_equal = local_item_array.at(j) == other_item_array.at(j);
      }
    }
  }
  return is_equal;
}

int ObIOAbility::add_measure_item(const ObIOBenchResult &item)
{
  struct {
    bool operator()(const ObIOBenchResult &left, const ObIOBenchResult &right) const
    {
      return left.size_ < right.size_;
    }
  } sort_fn;
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!item.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(item));
  } else if (OB_FAIL(measure_items_[static_cast<int>(item.mode_)].push_back(item))) {
    LOG_WARN("push back measure_items failed", K(ret), K(item));
  } else {
    lib::ob_sort(measure_items_[static_cast<int>(item.mode_)].begin(), measure_items_[static_cast<int>(item.mode_)].end(),
              sort_fn);
  }
  return ret;
}

const ObIOAbility::MeasureItemArray &ObIOAbility::get_measure_items(const ObIOMode mode) const
{
  static MeasureItemArray dummy_items;
  const MeasureItemArray *ret_items = nullptr;
  if (mode < ObIOMode::MAX_MODE) {
    ret_items = &measure_items_[static_cast<int64_t>(mode)];
  } else {
    ret_items = &dummy_items;
  }
  return *ret_items;
}

int ObIOAbility::get_iops(const ObIOMode mode, const int64_t size, double &iops) const
{
  int ret = OB_SUCCESS;
  int64_t found_item_idx = -1;
  if (OB_UNLIKELY(mode < ObIOMode::READ || mode >= ObIOMode::MAX_MODE || size <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(mode), K(size));
  } else if (OB_FAIL(find_item(mode, size, found_item_idx))) {
    LOG_WARN("find measure item failed", K(ret), K(mode), K(size));
  } else if (OB_UNLIKELY(found_item_idx < 0)) {
    // there is no measure item of bigger size, assume fixed bandwith
    const ObIOBenchResult &tail_item = measure_items_[static_cast<int>(mode)].at(measure_items_[static_cast<int>(mode)].count() - 1);
    iops = tail_item.iops_ * tail_item.size_ / size;
  } else {
    const ObIOBenchResult &found_item = measure_items_[static_cast<int>(mode)].at(found_item_idx);
    if (size == found_item.size_ // exactly match
        || 0 == found_item_idx) { // size smaller than smallest measure item
      iops = found_item.iops_;
    } else {
      const ObIOBenchResult &prev_item = measure_items_[static_cast<int>(mode)].at(found_item_idx - 1);
      const int64_t step_iops = found_item.iops_ - prev_item.iops_;
      const int64_t step_size = found_item.size_ - prev_item.size_;
      if (0 == step_size) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected io ability", K(ret), K(prev_item), K(found_item));
      } else {
        iops = prev_item.iops_ + step_iops * (((size - prev_item.size_) * 1.0) / step_size);
        LOG_DEBUG("get iops", K(iops), K(prev_item), K(found_item));
      }
    }
  }
  return ret;
}

int ObIOAbility::get_rt(const ObIOMode mode, const int64_t size, double &rt_us) const
{
  int ret = OB_SUCCESS;
  int64_t found_item_idx = -1;
  if (OB_UNLIKELY(mode < ObIOMode::READ || mode >= ObIOMode::MAX_MODE || size <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(mode), K(size));
  } else if (OB_FAIL(find_item(mode, size, found_item_idx))) {
    LOG_WARN("find measure item failed", K(ret), K(mode), K(size));
  } else if (OB_UNLIKELY(found_item_idx < 0)) {
    // there is no measure item of bigger size, assume linear growth for rt
    const ObIOBenchResult &tail_item = measure_items_[static_cast<int>(mode)].at(measure_items_[static_cast<int>(mode)].count() - 1);
    rt_us = (double)size / tail_item.size_ * tail_item.rt_us_;
  } else {
    const ObIOBenchResult &found_item = measure_items_[static_cast<int>(mode)].at(found_item_idx);
    if (size == found_item.size_ // exactly match
        || 0 == found_item_idx) { // size smaller than smallest measure item
      rt_us = found_item.rt_us_;
    } else {
      const ObIOBenchResult &prev_item = measure_items_[static_cast<int>(mode)].at(found_item_idx - 1);
      const double slope = (found_item.rt_us_ - prev_item.rt_us_) / static_cast<double>(found_item.size_ - prev_item.size_);
      rt_us = prev_item.rt_us_ + slope * (size - prev_item.size_);
    }
  }
  return ret;
}

int ObIOAbility::find_item(const ObIOMode mode, const int64_t size, int64_t &item_idx) const
{
  struct {
    bool operator()(const ObIOBenchResult &left, const int64_t size) const
    {
      return left.size_ < size;
    }
  } bound_fn;
  int ret = OB_SUCCESS;
  const MeasureItemArray &item_array = measure_items_[static_cast<int>(mode)];
  if (OB_UNLIKELY(item_array.count() <= 0)) {
    ret = OB_ERR_SYS;
    LOG_WARN("invalid measure_items", K(ret), K(mode), K(item_array.count()));
  } else {
    MeasureItemArray::const_iterator found_it = std::lower_bound(item_array.begin(), item_array.end(), size,
                                                                 bound_fn);
    if (found_it != item_array.end()) {
      item_idx = found_it - item_array.begin();
    } else {
      item_idx = -1;
    }
  }
  return ret;
}

/******************             IOBenchRunner              **********************/

ObIOBenchRunner::ObIOBenchRunner()
  : lib::Threads(1),
    is_inited_(false),
    thread_inited_(false),
    block_handles_(),
    load_(),
    io_count_(0),
    rt_us_(0),
    write_buf_(nullptr),
    read_buf_(nullptr),
    block_count_(0)
{

}

ObIOBenchRunner::~ObIOBenchRunner()
{
  destroy();
}

// moved definition to storage/blocksstable/ob_block_manager.cpp(disk benchmark, block_mgr real user)

int ObIOBenchRunner::do_benchmark(const ObIOBenchLoad &load, const int64_t thread_count, ObIOBenchResult &result)
{
  int ret = OB_SUCCESS;
  result.reset();
  const int64_t BENCHMARK_TIMEOUT_S = 5L; // 5s
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!load.is_valid() || thread_count <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(load), K(thread_count));
  } else {
    load_ = load;
    io_count_ = 0;
    rt_us_ = 0;
    if (thread_inited_) {
      lib::Threads::stop();
      lib::Threads::wait();
      lib::Threads::destroy();
      thread_inited_ = false;
    }
    if (OB_FAIL(lib::Threads::set_thread_count(thread_count))) {
      LOG_WARN("set thread count failed", K(ret), K(thread_count));
    } else if (OB_FAIL(lib::Threads::init())) {
      LOG_WARN("init benchmark threads failed", K(ret), K(thread_count));
    } else if (OB_FAIL(lib::Threads::start())) {
      LOG_WARN("start thread failed", K(ret));
    } else {
      thread_inited_ = true;
#ifdef _WIN32
      Sleep(static_cast<DWORD>(BENCHMARK_TIMEOUT_S * 1000));
#else
      sleep(BENCHMARK_TIMEOUT_S);
#endif
      lib::Threads::stop();
      lib::Threads::wait();
      lib::Threads::destroy();
      thread_inited_ = false;
      if (io_count_ <= 0) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid io count", K(ret), K(io_count_));
      } else {
        result.mode_ = load_.mode_;
        result.size_ = load_.size_;
        result.iops_ = io_count_ / BENCHMARK_TIMEOUT_S;
        result.rt_us_ = rt_us_ / io_count_;

      }
      LOG_INFO("IO BENCHMARK finished", K(ret), K_(load), K(result));
    }
    if (OB_FAIL(ret) && thread_inited_) {
      lib::Threads::stop();
      lib::Threads::wait();
      lib::Threads::destroy();
      thread_inited_ = false;
    }
  }
  return ret;
}

void ObIOBenchRunner::destroy()
{
  if (thread_inited_) {
    lib::Threads::stop();
    lib::Threads::wait();
    lib::Threads::destroy();
    thread_inited_ = false;
  }
  if (nullptr != write_buf_) {
    ob_free(write_buf_);
    write_buf_ = nullptr;
  }
  if (nullptr != read_buf_) {
    ob_free(read_buf_);
    read_buf_ = nullptr;
  }
  is_inited_ = false;
  block_handles_.reset();
  load_.reset();
  io_count_ = 0;
  rt_us_ = 0;
}

// moved definition to storage/blocksstable/ob_block_manager.cpp(disk benchmark, block_mgr real user)

/******************             IOBenchController              **********************/

ObIOBenchController::ObIOBenchController()
  : lib::Threads(1),
    thread_inited_(false),
    running_mutex_(),
    start_ts_(0),
    finish_ts_(0),
    ret_code_(OB_SUCCESS)
{

}

ObIOBenchController::~ObIOBenchController()
{
  if (thread_inited_) {
    lib::Threads::stop();
    lib::Threads::wait();
    lib::Threads::destroy();
    thread_inited_ = false;
  }
}

int ObIOBenchController::start_io_bench()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(running_mutex_.trylock())) {
    if (OB_UNLIKELY(OB_EAGAIN != ret)) {
      LOG_WARN("try lock failed", K(ret));
    } else {
      // benchmark is running, ignore this request
      ret = OB_SUCCESS;
    }
  } else {
    if (thread_inited_) {
      lib::Threads::stop();
      lib::Threads::wait();
      lib::Threads::destroy();
      thread_inited_ = false;
    }
    if (OB_FAIL(lib::Threads::init())) {
      LOG_WARN("init thread failed", K(ret));
    } else if (OB_FAIL(lib::Threads::start())) {
      LOG_WARN("start thread failed", K(ret));
    } else {
      thread_inited_ = true;
    }
    if (OB_FAIL(ret) && thread_inited_) {
      lib::Threads::stop();
      lib::Threads::wait();
      lib::Threads::destroy();
      thread_inited_ = false;
    }
    int tmp_ret = running_mutex_.unlock();
    if (OB_UNLIKELY(OB_SUCCESS != tmp_ret)) {
      LOG_WARN("unlock running_mutex failed", K(ret));
    }
  }
  return ret;
}

// moved definition to storage/blocksstable/ob_block_manager.cpp(disk benchmark, block_mgr real user)

int64_t ObIOBenchController::get_start_timestamp()
{
  return start_ts_;
}

int64_t ObIOBenchController::get_finish_timestamp()
{
  return finish_ts_;
}

int ObIOBenchController::get_ret_code()
{
  return ret_code_;
}

/******************             IOCalibration              **********************/

ObIOCalibration::ObIOCalibration()
  : is_inited_(false),
    baseline_iops_(0),
    io_ability_(),
    lock_()
{
}

ObIOCalibration::~ObIOCalibration()
{
  destroy();
}

ObIOCalibration &ObIOCalibration::get_instance()
{
  static ObIOCalibration instance;
  return instance;
}

int ObIOCalibration::init()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("io calibration init twice", K(ret), K(is_inited_));
  } else {
    is_inited_ = true;
  }
  if (OB_UNLIKELY(!is_inited_)) {
    destroy();
  }
  return ret;
}

void ObIOCalibration::destroy()
{
  is_inited_ = false;
  baseline_iops_ = 0;
  io_ability_.reset();
}

int ObIOCalibration::update_io_ability(const ObIOAbility &io_ability)
{
  int ret = OB_SUCCESS;
  double tmp_baseline_iops = baseline_iops_;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("io calibration not init", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!io_ability.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(io_ability));
  } else if (OB_FAIL(io_ability.get_iops(BASELINE_IO_MODE, BASELINE_IO_SIZE, tmp_baseline_iops))) {
    LOG_WARN("get baseline iops failed", K(ret));
  } else if (tmp_baseline_iops < std::numeric_limits<double>::epsilon()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid baseline iops", K(ret), K(tmp_baseline_iops));
  } else {
    DRWLock::WRLockGuard guard(lock_);
    if (OB_FAIL(io_ability_.assign(io_ability))) {
      LOG_WARN("assign io ability failed", K(ret));
    } else {
      baseline_iops_ = tmp_baseline_iops;
    }
  }
  LOG_INFO("update io ability", K(ret), K(io_ability), K(baseline_iops_));
  return ret;
}

int ObIOCalibration::reset_io_ability()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("io calibration not init", K(ret), K(is_inited_));
  } else {
    DRWLock::WRLockGuard guard(lock_);
    io_ability_.reset();
  }
  return ret;
}

int ObIOCalibration::get_io_ability(ObIOAbility &io_ability)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("io calibration not init", K(ret), K(is_inited_));
  } else {
    DRWLock::RDLockGuard guard(lock_);
    if (OB_FAIL(io_ability.assign(io_ability_))) {
      LOG_WARN("assign io ability failed", K(ret), K(io_ability_));
    }
  }
  return ret;
}

void ObIOCalibration::get_iops_scale(const ObIOMode mode, const int64_t size, double &iops_scale, bool &is_io_ability_valid)
{
  int ret = OB_SUCCESS;
  is_io_ability_valid = false;
  iops_scale = 1.0 * BASELINE_IO_SIZE / size;
  if (OB_UNLIKELY(!is_inited_)) {
    // do nothing
  } else if (OB_UNLIKELY(mode >= ObIOMode::MAX_MODE)) {
    // do nothing
  } else if (size <= 0) {
    iops_scale = 1.0;
    LOG_WARN("invalid size", K(mode), K(size), K(iops_scale));
  } else {
    DRWLock::RDLockGuard guard(lock_);
    if (!io_ability_.is_valid()) {
    // do nothing
    } else {
      double iops = 0;
      if (OB_FAIL(io_ability_.get_iops(mode, size, iops))) {
        LOG_WARN("get iops failed", K(ret), K(mode), K(size));
      } else {
        iops_scale = iops / baseline_iops_;
        is_io_ability_valid = true;
      }
    }
  }
}

int ObIOCalibration::refresh(const bool only_refresh, const ObIArray<ObIOBenchResult> &items)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("io calibration not init", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(only_refresh && items.count() > 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(only_refresh), K(items.count()));
  } else if (only_refresh) {
    // no-op: persistence path is removed.
  } else if (items.count() > 0) {
    ObIOAbility io_ability;
    for (int64_t i = 0; OB_SUCC(ret) && i < items.count(); ++i) {
      const ObIOBenchResult &item = items.at(i);
      if (OB_FAIL(io_ability.add_measure_item(item))) {
        LOG_WARN("add item failed", K(ret), K(i), K(item));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_UNLIKELY(!io_ability.is_valid())) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid argument", K(ret), K(io_ability));
      } else if (OB_FAIL(update_io_ability(io_ability))) {
        LOG_WARN("update io ability failed", K(ret), K(io_ability));
      }
    }
  } else {
    if (OB_FAIL(reset_io_ability())) {
      LOG_WARN("reset io ability failed", K(ret));
    }
  }
  ObIOAbility io_ability;
  int tmp_ret = OB_SUCCESS;
  if (OB_SUCCESS != (tmp_ret = ObIOCalibration::get_instance().get_io_ability(io_ability))) {
    LOG_WARN("get io ability failed", KR(tmp_ret));
  }
  LOG_INFO("refresh io calibration", K(ret), K(only_refresh), K(items), K(io_ability));
  return ret;
}

int ObIOCalibration::execute_benchmark()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(benchmark_controller_.start_io_bench())) {
    LOG_WARN("start io benchmark failed", K(ret));
  }
  return ret;
}

int ObIOCalibration::get_benchmark_status(int64_t &start_ts, int64_t &finish_ts, int &ret_code)
{
  int ret = OB_SUCCESS;
  start_ts = benchmark_controller_.get_start_timestamp();
  finish_ts = benchmark_controller_.get_finish_timestamp();
  ret_code = benchmark_controller_.get_ret_code();
  return ret;
}

int ObIOCalibration::parse_calibration_string(const ObString &calibration_string, ObIOBenchResult &item)
{
  int ret = OB_SUCCESS;
  // format: mode:size:latency:iops
  char mode_str[64] = { 0 };
  char size_str[64] = { 0 };
  char latency_str[64] = { 0 };
  bool is_valid = false;
  const int64_t MAX_IO_CALIBRAITON_STRING_LENGTH = 256;
  if (OB_UNLIKELY(calibration_string.empty()
        || calibration_string.length() >= MAX_IO_CALIBRAITON_STRING_LENGTH)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("calibration string is empty", K(ret), K(calibration_string));
  } else {
    // duplicate and replace ':' with ' '
    char dup_str[MAX_IO_CALIBRAITON_STRING_LENGTH] = { 0 };
    strncpy(dup_str, calibration_string.ptr(), sizeof(dup_str) - 1);
    for (int64_t i = 0; i < sizeof(dup_str); ++i) {
      if (':' == dup_str[i]) {
        dup_str[i] = ' ';
      }
    }
    int scan_ret = sscanf(dup_str, "%s %s %s %lf", mode_str, size_str, latency_str, &item.iops_);
    if (OB_UNLIKELY(4 != scan_ret)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(ret), K(scan_ret), K(calibration_string));
    }
  }
  if (OB_FAIL(ret)) {
  } else if (item.iops_ <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid iops string", K(ret), K(calibration_string), K(item.iops_));
  } else if (FALSE_IT(item.mode_ = get_io_mode_enum(mode_str))) {
  } else if (item.mode_ >= ObIOMode::MAX_MODE) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid mode name", K(ret), K(mode_str), K(item.mode_));
  } else if (FALSE_IT(item.size_ = ObConfigCapacityParser::get(size_str, is_valid))) {
  } else if (!is_valid) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid size string", K(ret), K(calibration_string), K(size_str));
  } else if (FALSE_IT(item.rt_us_ = ObConfigTimeParser::get(latency_str, is_valid))) {
  } else if (!is_valid) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid latency string", K(ret), K(calibration_string), K(latency_str));
  } else if (!item.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(calibration_string), K(item));
  }
  return ret;
}
