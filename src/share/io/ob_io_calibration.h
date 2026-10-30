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

#ifndef OCEANBASE_SHARE_IO_OB_IO_CALIBRATION_H
#define OCEANBASE_SHARE_IO_OB_IO_CALIBRATION_H

#include "lib/container/ob_array_iterator.h"
#include "lib/container/ob_array_wrap.h"
#include "rust/storage_io.h"
#include "share/io/ob_io_define.h"

namespace oceanbase
{
namespace common
{

struct ObIOBenchResult final
{
  OB_UNIS_VERSION(1);
public:
  ObIOBenchResult();
  ~ObIOBenchResult();
  void reset();
  bool is_valid() const;
  bool operator==(const ObIOBenchResult &other) const;
  DEFINE_TO_STRING(BUF_PRINTF("mode:%d, size:%ld, iops:%.2f, rt_us:%.2f", (int)mode_, size_, iops_, rt_us_));
public:
  ObIOMode mode_;
  int64_t size_;
  double iops_;
  double rt_us_;
};

class ObIOCalibration final
{
public:
  static ObIOCalibration &get_instance();
  static int parse_calibration_string(const ObString &calibration_string, ObIOBenchResult &item);
  int init();
  void destroy();
  int replace(const ObIArray<ObIOBenchResult> &items);
  int reset_calibration();
  rust::Box<storage_io::CalibrationView> snapshot() const;
  bool is_initialized() const { return is_inited_; }
  int refresh(const bool only_refresh, const ObIArray<ObIOBenchResult> &items);
private:
  ObIOCalibration();
  ~ObIOCalibration();
  DISALLOW_COPY_AND_ASSIGN(ObIOCalibration);
private:
  bool is_inited_;
};

}// end namespace oceanbase
}// end namespace oceanbase

#endif//OCEANBASE_SHARE_IO_OB_IO_CALIBRATION_H
