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
#include "share/io/ob_io_manager.h"

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

/******************               IOCalibrationStatus                *******************/

ObAllVirtualIOCalibrationStatus::ObAllVirtualIOCalibrationStatus()
  : is_end_(false), start_ts_(0), finish_ts_(0), ret_code_(OB_SUCCESS)
{

}

ObAllVirtualIOCalibrationStatus::~ObAllVirtualIOCalibrationStatus()
{

}

int ObAllVirtualIOCalibrationStatus::init(const common::ObAddr &addr)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(init_addr(addr))) {
  } else {
    is_inited_ = true;
  }
  return ret;
}

void ObAllVirtualIOCalibrationStatus::reset()
{
  ObAllVirtualIOStatusIterator::reset();
  is_end_ = false;
  start_ts_ = 0;
  finish_ts_ = 0;
  ret_code_ = OB_SUCCESS;
}

int ObAllVirtualIOCalibrationStatus::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  row = nullptr;
  ObObj *cells = cur_row_.cells_;
  if (OB_UNLIKELY(!is_inited_ || nullptr == cells)) {
    ret = OB_NOT_INIT;
  } else if (is_end_) {
    row = nullptr;
    ret = OB_ITER_END;
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < output_column_ids_.count(); ++i) {
      const uint64_t column_id = output_column_ids_.at(i);
      switch (column_id) {
        case STORAGE_NAME: {
          cells[i].set_varchar("DATA");
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case STATUS: {
          if (0 == start_ts_ && 0 == finish_ts_) {
            cells[i].set_varchar("NOT AVAILABLE");
          } else if (start_ts_ > 0 && 0 == finish_ts_) {
            cells[i].set_varchar("IN PROGRESS");
          } else if (start_ts_ > 0 && finish_ts_ > 0) {
            if (OB_SUCCESS == ret_code_) {
              cells[i].set_varchar("READY");
            } else {
              cells[i].set_varchar("FAILED");
            }
          }
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case START_TIME: {
          if (0 == start_ts_) {
            cells[i].set_null();
          } else {
            cells[i].set_timestamp(start_ts_);
          }
          break;
        }
        case FINISH_TIME: {
          if (0 == finish_ts_) {
            cells[i].set_null();
          } else {
            cells[i].set_timestamp(finish_ts_);
          }
          break;
        }
        default: {
          ret = OB_ERR_UNEXPECTED;
          break;
        }
      } // end switch
    } // end for-loop
    if (OB_SUCC(ret)) {
      row = &cur_row_;
    }
    is_end_ = true;
  }
  return ret;
}

/******************               IOBenchmark                *******************/

ObAllVirtualIOBenchmark::ObAllVirtualIOBenchmark()
{

}

ObAllVirtualIOBenchmark::~ObAllVirtualIOBenchmark()
{

}

int ObAllVirtualIOBenchmark::init(const common::ObAddr &addr)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(init_addr(addr))) {

  } else {
    is_inited_ = true;
  }
  return ret;
}

void ObAllVirtualIOBenchmark::reset()
{
  ObAllVirtualIOStatusIterator::reset();
}

int ObAllVirtualIOBenchmark::inner_get_next_row(common::ObNewRow *&row)
{
  row = nullptr;
  return is_inited_ ? OB_ITER_END : OB_NOT_INIT;
}

/******************               IOQuota                *******************/

ObAllVirtualIOQuota::QuotaInfo::QuotaInfo()
  : group_id_(0),
    group_mode_(ObIOGroupMode::LOCALREAD),
    size_(0),
    real_iops_(0),
    schedule_us_(0),
    io_delay_us_(0),
    total_us_(0)
{

}

ObAllVirtualIOQuota::QuotaInfo::~QuotaInfo()
{

}

ObAllVirtualIOQuota::ObAllVirtualIOQuota()
  : quota_infos_(), quota_pos_(0)
{

}

ObAllVirtualIOQuota::~ObAllVirtualIOQuota()
{

}

int ObAllVirtualIOQuota::init(const common::ObAddr &addr)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(init_addr(addr))) {
  } else {
    {
      ObRefHolder<ObIOService> service_holder;
      if (OB_FAIL(OB_IO_MANAGER.get_io_service(service_holder))) {
        if (OB_HASH_NOT_EXIST != ret) {
        } else {
          ret = OB_ENTRY_NOT_EXIST;
        }
      } else {
        const auto snapshot = service_holder.get_ptr()->config_snapshot();
        if (!snapshot) { ret = OB_NOT_INIT; }
        else if (OB_FAIL(record_user_group(*snapshot->usage_, snapshot->config_))) {
        } else if (OB_FAIL(record_sys_group(service_holder.get_ptr()->get_sys_io_usage()))) {
        }
      }
    }
    if (OB_SUCC(ret)) {
      is_inited_ = true;
    }
  }
  return ret;
}

int ObAllVirtualIOQuota::record_user_group(ObIOUsage &io_usage, const ObIOServiceConfig &io_config)
{
  int ret = OB_SUCCESS;
  {
    const int64_t GROUP_MODE_CNT = static_cast<int64_t>(ObIOGroupMode::MODECNT);
    ObIOUsageInfoArray info;
    if (OB_FAIL(io_usage.snapshot(info, storage_io::StatisticsSampleMode::Advance))) { return ret; }
    uint64_t group_config_index = 0;
    int tmp_ret = OB_SUCCESS;
    for (int64_t i = 0; i < info.count(); ++i) {
      if (OB_TMP_FAIL(oceanbase::common::transform_usage_index_to_group_config_index(i, group_config_index))) {
      } else if (group_config_index >= io_config.group_configs_.count()) {
      } else if (io_config.group_configs_.at(group_config_index).deleted_) {
      } else if (info.at(i).avg_byte_ > std::numeric_limits<double>::epsilon()) {
        QuotaInfo item;
        
        item.group_mode_ = static_cast<ObIOGroupMode>(i % GROUP_MODE_CNT);
        item.group_id_ = io_config.group_configs_.at(group_config_index).group_id_;
        item.size_ = metric_integer(info.at(i).avg_byte_, ret);
        item.real_iops_ = metric_integer(info.at(i).avg_iops_, ret);
        item.schedule_us_ = info.at(i).avg_schedule_delay_us_;
        item.io_delay_us_ = info.at(i).avg_device_delay_us_;
        item.total_us_ = info.at(i).avg_total_delay_us_;
        if (OB_SUCC(ret) && OB_FAIL(quota_infos_.push_back(item))) {
        }
      }
    }
  }
  return ret;
}

int ObAllVirtualIOQuota::record_sys_group(ObIOUsage &sys_io_usage)
{
  int ret = OB_SUCCESS;
  {
    const int64_t GROUP_MODE_CNT = static_cast<int64_t>(ObIOGroupMode::MODECNT);
    ObIOUsageInfoArray info;
    if (OB_FAIL(sys_io_usage.snapshot(info, storage_io::StatisticsSampleMode::Advance))) { return ret; }
    int tmp_ret = OB_SUCCESS;
    uint64_t group_config_index = 0;
    for (uint64_t i = 0; i < info.count(); ++i) {
      if (OB_TMP_FAIL(oceanbase::common::transform_usage_index_to_group_config_index(i, group_config_index))) {
      } else if (info.at(i).avg_byte_ <= std::numeric_limits<double>::epsilon()) {
      } else {
        QuotaInfo item;
        
        item.group_mode_ = static_cast<ObIOGroupMode>(i % GROUP_MODE_CNT);
        item.group_id_ = SYS_MODULE_START_ID + i / GROUP_MODE_CNT;
        item.size_ = metric_integer(info.at(i).avg_byte_, ret);
        item.real_iops_ = metric_integer(info.at(i).avg_iops_, ret);
        item.schedule_us_ = info.at(i).avg_schedule_delay_us_;
        item.io_delay_us_ = info.at(i).avg_device_delay_us_;
        item.total_us_ = info.at(i).avg_total_delay_us_;
        if (OB_SUCC(ret) && OB_FAIL(quota_infos_.push_back(item))) {
        }
      }
    }
  }
  return ret;
}

void ObAllVirtualIOQuota::reset()
{
  ObAllVirtualIOStatusIterator::reset();
  quota_infos_.reset();
  quota_pos_ = 0;
}

int ObAllVirtualIOQuota::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  row = nullptr;
  ObObj *cells = cur_row_.cells_;
  if (OB_UNLIKELY(!is_inited_ || nullptr == cells)) {
    ret = OB_NOT_INIT;
  } else if (quota_pos_ >= quota_infos_.count()) {
    row = nullptr;
    ret = OB_ITER_END;
  } else {
    QuotaInfo &item = quota_infos_.at(quota_pos_);
    for (int64_t i = 0; OB_SUCC(ret) && i < output_column_ids_.count(); ++i) {
      const uint64_t column_id = output_column_ids_.at(i);
      switch (column_id) {
        case GROUP_ID: {
          cells[i].set_int(item.group_id_);
          break;
        }
        case MODE: {
          const char *str = get_io_mode_string(item.group_mode_);
          cells[i].set_varchar(str);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case SIZE: {
          cells[i].set_int(static_cast<int64_t>(item.size_));
          break;
        }
        case MIN_IOPS: {
          cells[i].set_null();
          break;
        }
        case MAX_IOPS: {
          cells[i].set_null();
          break;
        }
        case REAL_IOPS: {
          cells[i].set_int(static_cast<int64_t>(item.real_iops_));
          break;
        }
        case MIN_MBPS: {
          cells[i].set_null();
          break;
        }
        case MAX_MBPS: {
          cells[i].set_null();
          break;
        }
        case REAL_MBPS: {
          cells[i].set_int(metric_mbps(item.real_iops_, item.size_, ret));
          break;
        }
        case SCHEDULE_US: {
          cells[i].set_int(item.schedule_us_);
          break;
        }
        case IO_DELAY_US: {
          cells[i].set_int(item.io_delay_us_);
          break;
        }
        case TOTAL_US: {
          cells[i].set_int(item.total_us_);
          break;
        }
        default: {
          ret = OB_ERR_UNEXPECTED;
          break;
        }
      } // end switch
    } // end for-loop
    if (OB_SUCC(ret)) {
      row = &cur_row_;
    }
    ++quota_pos_;
  }
  return ret;
}

ObAllVirtualGroupIOStat::ObAllVirtualGroupIOStat()
  : group_io_stats_(), group_io_stats_pos_(0)
  {}

ObAllVirtualGroupIOStat::~ObAllVirtualGroupIOStat()
  {}

void ObAllVirtualGroupIOStat::reset()
{
  ObAllVirtualIOStatusIterator::reset();
  group_io_stats_.reset();
  is_inited_ = false;
  group_io_stats_pos_ = 0;
}

int ObAllVirtualGroupIOStat::init(const common::ObAddr &addr)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(init_addr(addr))) {
  } else {
    {
      ObRefHolder<ObIOService> service_holder;
      {
        if (OB_FAIL(OB_IO_MANAGER.get_io_service(service_holder))) {
          if (OB_HASH_NOT_EXIST != ret) {
          } else {
            ret = OB_ENTRY_NOT_EXIST;
          }
        } else if (OB_FAIL(record_user_group_io_status(service_holder.get_ptr()))) {
        } else if (OB_FAIL(record_sys_group_io_status(service_holder.get_ptr()))) {
        }
      }
    }
    if (OB_SUCC(ret)) {
      is_inited_ = true;
    }
  }

  return ret;
}

int ObAllVirtualGroupIOStat::record_user_group_io_status(ObIOService *io_manager)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(io_manager)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObIOUsageInfoArray info;
    const auto snapshot = io_manager->config_snapshot();
    if (!snapshot) { ret = OB_NOT_INIT;
    } else if (OB_FAIL(snapshot->usage_->snapshot(info))) {
    } else {
      int tmp_ret = OB_SUCCESS;
      const ObIOServiceConfig &io_config = snapshot->config_;
      const int64_t GROUP_MODE_CNT = static_cast<int64_t>(ObIOGroupMode::MODECNT);
      uint64_t group_config_index = 0;

      if (info.count() % GROUP_MODE_CNT != 0 ) {
      } else {
        for (int64_t left = 0; left < info.count() && OB_SUCC(ret); left += GROUP_MODE_CNT) {
          const int64_t local_read_index = left + static_cast<int64_t>(ObIOGroupMode::LOCALREAD);
          const int64_t local_write_index = left + static_cast<int64_t>(ObIOGroupMode::LOCALWRITE);

          int64_t group_min_iops = 0, group_max_iops = 0, group_iops_weight = 0;

          if (OB_TMP_FAIL(oceanbase::common::transform_usage_index_to_group_config_index(
                  local_read_index, group_config_index))) {
          } else if (group_config_index >= io_config.group_configs_.count()) {
          } else if (io_config.group_configs_.at(group_config_index).cleared_ ||
                     io_config.group_configs_.at(group_config_index).deleted_) {
            // do nothing
          } else if (OB_SUCC(ret) && OB_FAIL(io_config.calc_group_config(group_config_index,
                                                      group_min_iops,
                                                      group_max_iops,
                                                      group_iops_weight))) {
          } else {
            GroupIoStat read_item;
            
            read_item.mode_ = ObIOMode::READ;
            read_item.group_id_ = io_config.group_configs_.at(group_config_index).group_id_;
            snprintf(read_item.group_name_, sizeof(read_item.group_name_), "%s",
                     io_config.group_configs_.at(group_config_index).group_name_);

            read_item.min_iops_ = group_min_iops;
            read_item.max_iops_ = group_max_iops;
            read_item.real_iops_ = info.at(local_read_index).avg_iops_;
            if (OB_SUCC(ret) && OB_FAIL(group_io_stats_.push_back(read_item))) {
            }
            if (OB_FAIL(ret)) {
            } else {
              GroupIoStat write_item;
              
              write_item.mode_ = ObIOMode::WRITE;
              write_item.group_id_ = io_config.group_configs_.at(group_config_index).group_id_;
              snprintf(write_item.group_name_, sizeof(write_item.group_name_), "%s",
                       io_config.group_configs_.at(group_config_index).group_name_);
              
              write_item.min_iops_ = group_min_iops;
              write_item.max_iops_ = group_max_iops;
              write_item.real_iops_ = info.at(local_write_index).avg_iops_;
              if (OB_SUCC(ret) && OB_FAIL(group_io_stats_.push_back(write_item))) {
              }
            }
          }
        }
      }
    }
  }

  return ret;
}

int ObAllVirtualGroupIOStat::record_sys_group_io_status(ObIOService *io_manager)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(io_manager)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const int64_t GROUP_MODE_CNT = static_cast<int64_t>(ObIOGroupMode::MODECNT);
    ObIOUsageInfoArray info;
    if (OB_FAIL(io_manager->get_sys_io_usage().snapshot(info, storage_io::StatisticsSampleMode::Preview))) {
    } else {
      if (info.count() % GROUP_MODE_CNT != 0 ) {
      } else {
        for (int64_t left = 0; left < info.count() && OB_SUCC(ret); left += GROUP_MODE_CNT) {
          const int64_t local_read_index = left + static_cast<int64_t>(ObIOGroupMode::LOCALREAD);
          const int64_t local_write_index = left + static_cast<int64_t>(ObIOGroupMode::LOCALWRITE);
          const int64_t sys_group_id = SYS_MODULE_START_ID + left / GROUP_MODE_CNT;
          const char *group_name = get_io_sys_group_name(static_cast<common::ObIOModule>(sys_group_id));

          GroupIoStat read_item;
          read_item.mode_ = ObIOMode::READ;
          read_item.group_id_ = sys_group_id;
          snprintf(read_item.group_name_, sizeof(read_item.group_name_), "%s", group_name);
          read_item.min_iops_ = 0;
          read_item.max_iops_ = INT64_MAX;
          read_item.real_iops_ = metric_integer(info.at(local_read_index).avg_iops_, ret);
          if (OB_SUCC(ret) && OB_FAIL(group_io_stats_.push_back(read_item))) {
          } else {
            GroupIoStat write_item;
            write_item.mode_ = ObIOMode::WRITE;
            write_item.group_id_ = sys_group_id;
            snprintf(write_item.group_name_, sizeof(write_item.group_name_), "%s", group_name);
            write_item.min_iops_ = 0;
            write_item.max_iops_ = INT64_MAX;
            write_item.real_iops_ = metric_integer(info.at(local_write_index).avg_iops_, ret);
            if (OB_SUCC(ret) && OB_FAIL(group_io_stats_.push_back(write_item))) {
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObAllVirtualGroupIOStat::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;

  row = nullptr;
  ObObj *cells = cur_row_.cells_;
  if (OB_UNLIKELY(!is_inited_ || nullptr == cells)) {
    ret = OB_NOT_INIT;
  } else if (group_io_stats_pos_ >= group_io_stats_.count()) {
    row = nullptr;
    ret = OB_ITER_END;
  } else {
    GroupIoStat &item = group_io_stats_.at(group_io_stats_pos_);
    for (int64_t i = 0; OB_SUCC(ret) && i < output_column_ids_.count(); ++i) {
      const uint64_t column_id = output_column_ids_.at(i);
      switch (column_id) {
        case GROUP_ID: {
          cells[i].set_int(item.group_id_);
          break;
        }
        case GROUP_NAME: {
          cells[i].set_varchar(item.group_name_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case MODE: {
          cells[i].set_varchar(get_io_mode_string(item.mode_));
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case MAX_IOPS: {
          cells[i].set_null();
          break;
        }
        case MIN_IOPS: {
          cells[i].set_null();
          break;
        }
        case NORM_IOPS: {
          cells[i].set_null();
          break;
        }
        case REAL_IOPS: {
          cells[i].set_int(item.real_iops_);
          break;
        }
      }
    }
    if (OB_SUCC(ret)) {
      row = &cur_row_;
    }
    ++group_io_stats_pos_;
  }

  return ret;
}

}// namespace observer
}// namespace oceanbase
