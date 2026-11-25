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

#define USING_LOG_PREFIX SERVER
#include "observer/virtual_table/ob_all_virtual_io_status.h"
#include "src/share/ob_server_struct.h"
#include "observer/ob_server.h"

namespace oceanbase
{
using namespace common;
using namespace share;

namespace observer
{

namespace {
int64_t metric_integer(double value, int &ret)
{
  const auto converted = storage_io::metric_integer(value);
  if (OB_SUCC(ret)) { ret = converted.error; }
  return converted.value;
}
int64_t metric_mbps(int64_t iops, int64_t size, int &ret)
{
  const auto converted = storage_io::metric_mbps(iops, size);
  if (OB_SUCC(ret)) { ret = converted.error; }
  return converted.value;
}

}

ObAllVirtualIOStatusIterator::ObAllVirtualIOStatusIterator()
  : is_inited_(false), addr_()
{
  memset(ip_buf_, 0, sizeof(ip_buf_));
}

ObAllVirtualIOStatusIterator::~ObAllVirtualIOStatusIterator()
{

}

int ObAllVirtualIOStatusIterator::init_addr(const common::ObAddr &addr)
{
  int ret = OB_SUCCESS;
  if (!addr.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    addr_ = addr;
    MEMSET(ip_buf_, 0, sizeof(ip_buf_));
    if (!addr_.ip_to_string(ip_buf_, sizeof(ip_buf_))) {
      ret = OB_ERR_UNEXPECTED;
    }
  }
  return ret;
}

void ObAllVirtualIOStatusIterator::reset()
{
  ObVirtualTableScannerIterator::reset();
  is_inited_ = false;
  addr_.reset();
  MEMSET(ip_buf_, 0, sizeof(ip_buf_));
}

ObAllVirtualIOStat::ObAllVirtualIOStat()
    : position_(0)
{
}

int ObAllVirtualIOStat::init(const common::ObAddr &addr)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(init_addr(addr))) { return ret; }
  auto statistics = OBSERVER.io_service().statistics_handle();
  auto snapshot = storage_io::new_statistics_snapshot();
  if (OB_FAIL(statistics->statistics_snapshot(ObTimeUtility::fast_current_time(),
      *snapshot))) { return ret; }
  for (size_t lane = 0; OB_SUCC(ret) && lane < (SYS_MODULE_CNT + 1) * 2; ++lane) {
    const auto rates = snapshot->statistics_at(lane);
    if (OB_FAIL(rates.error)) { break; }
    if (rates.iops <= 0) { continue; }
    IOStat item;
    item.module_id_ = lane < 2 ? 0 : SYS_MODULE_START_ID + lane / 2 - 1;
    item.mode_ = static_cast<ObIOMode>(lane % 2);
    item.size_ = metric_integer(rates.average_bytes, ret);
    item.iops_ = metric_integer(rates.iops, ret);
    item.total_us_ = rates.total_us;
    if (OB_SUCC(ret) && OB_FAIL(stats_.push_back(item))) {
    }
  }
  if (OB_SUCC(ret)) { is_inited_ = true; }
  return ret;
}

void ObAllVirtualIOStat::reset()
{
  ObAllVirtualIOStatusIterator::reset();
  stats_.reset();
  position_ = 0;
}

int ObAllVirtualIOStat::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  row = nullptr;
  ObObj *cells = cur_row_.cells_;
  if (OB_UNLIKELY(!is_inited_ || nullptr == cells)) {
    ret = OB_NOT_INIT;
  } else if (position_ >= stats_.count()) {
    ret = OB_ITER_END;
  } else {
    const auto &item = stats_.at(position_);
    for (int64_t i = 0; OB_SUCC(ret) && i < output_column_ids_.count(); ++i) {
      switch (output_column_ids_.at(i)) {
        case MODULE_ID:
          cells[i].set_int(item.module_id_);
          break;
        case MODE:
          cells[i].set_varchar(get_io_mode_string(item.mode_));
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        case SIZE:
          cells[i].set_int(item.size_);
          break;
        case IOPS:
          cells[i].set_int(item.iops_);
          break;
        case MBPS:
          cells[i].set_int(metric_mbps(item.iops_, item.size_, ret));
          break;
        case TOTAL_US:
          cells[i].set_int(item.total_us_);
          break;
        default:
          ret = OB_ERR_UNEXPECTED;
          break;
      }
    }
    if (OB_SUCC(ret)) { row = &cur_row_; }
    ++position_;
  }
  return ret;
}

}// namespace observer
}// namespace oceanbase
