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
#include "share/io/ob_io_manager.h"
#include "share/config/ob_config_helper.h"
#include "lib/container/ob_se_array.h"

using namespace oceanbase::lib;
using namespace oceanbase::common;


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

ObIOCalibration::ObIOCalibration()
  : is_inited_(false)
{
}

ObIOCalibration::~ObIOCalibration() = default;

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
  if (is_inited_) { reset_calibration(); }
  is_inited_ = false;
}

int ObIOCalibration::replace(const ObIArray<ObIOBenchResult> &items)
{
  if (!is_inited_) { return OB_NOT_INIT; }
  ObRefHolder<ObIOService> holder;
  int ret = OB_IO_MANAGER.get_io_service(holder);
  if (OB_SUCC(ret)) {
    ObSEArray<storage_io::CalibrationSample, 20> samples;
    for (int64_t i = 0; OB_SUCC(ret) && i < items.count(); ++i) {
      const auto &item = items.at(i);
      ret = samples.push_back({static_cast<int32_t>(item.mode_), item.size_, item.iops_, item.rt_us_});
    }
    if (OB_SUCC(ret)) {
      ret = holder.get_ptr()->replace_calibration({samples.get_data(), static_cast<size_t>(samples.count())});
    }
  }
  return ret;
}

int ObIOCalibration::reset_calibration()
{
  if (!is_inited_) { return OB_NOT_INIT; }
  ObRefHolder<ObIOService> holder;
  int ret = OB_IO_MANAGER.get_io_service(holder);
  if (OB_SUCC(ret)) { ret = holder.get_ptr()->reset_calibration(); }
  return ret;
}

rust::Box<oceanbase::storage_io::CalibrationView> ObIOCalibration::snapshot() const
{
  ObRefHolder<ObIOService> holder;
  if (OB_SUCCESS != OB_IO_MANAGER.get_io_service(holder)) {
    return storage_io::empty_calibration_view();
  }
  return holder.get_ptr()->calibration_snapshot();
}

int ObIOCalibration::refresh(const bool only_refresh, const ObIArray<ObIOBenchResult> &items)
{
  if (!is_inited_) { return OB_NOT_INIT; }
  if (only_refresh) { return items.empty() ? OB_SUCCESS : OB_INVALID_ARGUMENT; }
  return items.empty() ? reset_calibration() : replace(items);
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
    }
  }
  if (OB_FAIL(ret)) {
  } else if (item.iops_ <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (FALSE_IT(item.mode_ = get_io_mode_enum(mode_str))) {
  } else if (item.mode_ >= ObIOMode::MAX_MODE) {
    ret = OB_INVALID_ARGUMENT;
  } else if (FALSE_IT(item.size_ = ObConfigCapacityParser::get(size_str, is_valid))) {
  } else if (!is_valid) {
    ret = OB_INVALID_ARGUMENT;
  } else if (FALSE_IT(item.rt_us_ = ObConfigTimeParser::get(latency_str, is_valid))) {
  } else if (!is_valid) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!item.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  }
  return ret;
}
