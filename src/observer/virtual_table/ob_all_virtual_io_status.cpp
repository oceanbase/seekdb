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
    LOG_WARN("invalid argument", K(ret), K(addr));
  } else {
    addr_ = addr;
    MEMSET(ip_buf_, 0, sizeof(ip_buf_));
    if (!addr_.ip_to_string(ip_buf_, sizeof(ip_buf_))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ip to string failed", K(ret), K(addr_));
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
  } else if (OB_FAIL(ObIOCalibration::get_instance().get_benchmark_status(start_ts_, finish_ts_, ret_code_))) {
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
    LOG_WARN("not init", K(ret), KP(cur_row_.cells_), K(is_inited_));
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
          LOG_WARN("invalid column id", K(ret), K(column_id), K(i), K(output_column_ids_));
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
  : io_ability_(), mode_pos_(0), size_pos_(0)
{

}

ObAllVirtualIOBenchmark::~ObAllVirtualIOBenchmark()
{

}

int ObAllVirtualIOBenchmark::init(const common::ObAddr &addr)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(init_addr(addr))) {
  } else if (OB_FAIL(ObIOCalibration::get_instance().get_io_ability(io_ability_))) {
  } else {
    is_inited_ = true;
  }
  return ret;
}

void ObAllVirtualIOBenchmark::reset()
{
  ObAllVirtualIOStatusIterator::reset();
  io_ability_.reset();
  mode_pos_ = 0;
  size_pos_ = 0;
}

int ObAllVirtualIOBenchmark::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  row = nullptr;
  ObObj *cells = cur_row_.cells_;
  if (OB_UNLIKELY(!is_inited_ || nullptr == cells)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), KP(cur_row_.cells_), K(is_inited_));
  } else if (!io_ability_.is_valid()) {
    row = nullptr;
    ret = OB_ITER_END;
  } else {
    ObIOBenchResult item;
    while (mode_pos_ < static_cast<int64_t>(ObIOMode::MAX_MODE)) {
      const ObIArray<ObIOBenchResult> &bench_items = io_ability_.get_measure_items(static_cast<ObIOMode>(mode_pos_));
      if (size_pos_ < bench_items.count()) {
        item = bench_items.at(size_pos_);
        ++size_pos_;
        break;
      } else {
        ++mode_pos_;
        size_pos_ = 0;
      }
    }
    if (mode_pos_ >= static_cast<int64_t>(ObIOMode::MAX_MODE)) {
      row = nullptr;
      ret = OB_ITER_END;
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < output_column_ids_.count(); ++i) {
      const uint64_t column_id = output_column_ids_.at(i);
      switch (column_id) {
        case STORAGE_NAME: {
          cells[i].set_varchar("DATA");
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case MODE: {
          const char *io_mode_string = get_io_mode_string(item.mode_);
          cells[i].set_varchar(io_mode_string);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case SIZE: {
          cells[i].set_int(item.size_);
          break;
        }
        case IOPS: {
          cells[i].set_int(static_cast<int64_t>(item.iops_));
          break;
        }
        case MBPS: {
          int64_t mbps = item.size_ * item.iops_ / 1024L / 1024L; // unit MB/s
          cells[i].set_int(mbps);
          break;
        }
        case LATENCY: {
          cells[i].set_int(static_cast<int64_t>(item.rt_us_));
          break;
        }
        default: {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid column id", K(ret), K(column_id), K(i), K(output_column_ids_));
          break;
        }
      } // end switch
    } // end for-loop
    if (OB_SUCC(ret)) {
      row = &cur_row_;
    }
  }
  return ret;
}

/******************               IOQuota                *******************/

ObAllVirtualIOQuota::QuotaInfo::QuotaInfo()
  : group_id_(0),
    group_mode_(ObIOGroupMode::LOCALREAD),
    size_(0),
    real_iops_(0),
    min_iops_(0),
    max_iops_(0),
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
          LOG_WARN("get io service failed", K(ret));
        } else {
          ret = OB_ENTRY_NOT_EXIST;
          LOG_WARN("io service does not exist", K(ret));
        }
      } else if (OB_FAIL(record_user_group( service_holder.get_ptr()->get_io_usage(), service_holder.get_ptr()->get_io_config()))) {
      } else if (OB_FAIL(record_sys_group( service_holder.get_ptr()->get_sys_io_usage()))) {
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
    io_usage.calculate_io_usage();
    const ObIOUsageInfoArray &info = io_usage.get_io_usage();
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
        item.size_ = static_cast<int64_t>(info.at(i).avg_byte_);
        item.real_iops_ = static_cast<int64_t>(info.at(i).avg_iops_);
        item.schedule_us_ = info.at(i).avg_schedule_delay_us_;
        item.io_delay_us_ = info.at(i).avg_device_delay_us_;
        item.total_us_ = info.at(i).avg_total_delay_us_;
        int64_t group_min = 0, group_max = 0, group_weight = 0;
        double iops_scale = 0;
        if (OB_FAIL(io_config.calc_group_config(group_config_index,
                                               group_min,
                                               group_max,
                                               group_weight))) {
        } else {
          LOG_INFO("get group config", K(ret), K(group_config_index), K(io_config), K(item), K(group_min), K(group_max), K(group_weight));
        }
        if (OB_SUCC(ret)) {
          const ObIOMode access_mode = (ObIOGroupMode::LOCALREAD == item.group_mode_ ? ObIOMode::READ : ObIOMode::WRITE);
          bool is_io_ability_valid = false; // useless
          ObIOCalibration::get_instance().get_iops_scale(access_mode,
                                                         info.at(i).avg_byte_,
                                                         iops_scale,
                                                         is_io_ability_valid);
          if (!is_io_ability_valid) {
            group_min = group_max = INT64_MAX;
            LOG_INFO("invalid io ability", K(ret), K(item), K(access_mode), K(info), K(iops_scale));
          }
        }
        if (OB_SUCC(ret)) {
          item.min_iops_ = group_min == INT64_MAX ? INT64_MAX : static_cast<int64_t>((double)group_min * iops_scale);
          item.max_iops_ = group_max == INT64_MAX ? INT64_MAX : static_cast<int64_t>((double)group_max * iops_scale);
          if (OB_FAIL(quota_infos_.push_back(item))) {
          } else {
            LOG_INFO("push back item", K(ret), K(item));
          }
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
    sys_io_usage.calculate_io_usage();
    const ObIOUsageInfoArray &info = sys_io_usage.get_io_usage();
    int tmp_ret = OB_SUCCESS;
    uint64_t group_config_index = 0;
    for (uint64_t i = 0; i < info.count(); ++i) {
      if (OB_TMP_FAIL(oceanbase::common::transform_usage_index_to_group_config_index(i, group_config_index))) {
      } else if (info.at(i).avg_byte_ <= std::numeric_limits<double>::epsilon()) {
      } else {
        QuotaInfo item;
        
        item.group_mode_ = static_cast<ObIOGroupMode>(i % GROUP_MODE_CNT);
        item.group_id_ = SYS_MODULE_START_ID + i / GROUP_MODE_CNT;
        item.size_ = static_cast<int64_t>(info.at(i).avg_byte_);
        item.real_iops_ = static_cast<int64_t>(info.at(i).avg_iops_);
        item.min_iops_ = 0;
        item.max_iops_ = 0;
        item.schedule_us_ = info.at(i).avg_schedule_delay_us_;
        item.io_delay_us_ = info.at(i).avg_device_delay_us_;
        item.total_us_ = info.at(i).avg_total_delay_us_;
        if (OB_FAIL(quota_infos_.push_back(item))) {
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
    LOG_WARN("not init", K(ret), KP(cur_row_.cells_), K(is_inited_));
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
          cells[i].set_int(static_cast<int64_t>(item.min_iops_));
          break;
        }
        case MAX_IOPS: {
          cells[i].set_int(static_cast<int64_t>(item.max_iops_));
          break;
        }
        case REAL_IOPS: {
          cells[i].set_int(static_cast<int64_t>(item.real_iops_));
          break;
        }
        case MIN_MBPS: {
          if (item.min_iops_ == INT64_MAX) {
            cells[i].set_int(INT64_MAX);
          } else {
            cells[i].set_int(static_cast<int64_t>(item.min_iops_ * item.size_ / 1024L / 1024L));
          }
          break;
        }
        case MAX_MBPS: {
          if (item.max_iops_ == INT64_MAX){
            cells[i].set_int(INT64_MAX);
          } else {
            cells[i].set_int(static_cast<int64_t>(item.max_iops_ * item.size_ / 1024L / 1024L));
          }
          break;
        }
        case REAL_MBPS: {
          cells[i].set_int(static_cast<int64_t>(item.real_iops_ * item.size_ / 1024L / 1024L));
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
          LOG_WARN("invalid column id", K(ret), K(column_id), K(i), K(output_column_ids_));
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
            LOG_WARN("get io service failed", K(ret));
          } else {
            ret = OB_ENTRY_NOT_EXIST;
            LOG_WARN("io service does not exist", K(ret));
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
    LOG_WARN("io manager is null", K(ret));
  } else {
    ObIOUsage io_usage;
    if (OB_FAIL(io_usage.init(2))) {
    } else if (OB_FAIL(io_usage.assign(io_manager->get_io_usage()))) {
    } else {
      int tmp_ret = OB_SUCCESS;
      const ObIOServiceConfig io_config = io_manager->get_io_config();
      const ObIOUsageInfoArray &info = io_usage.get_io_usage();
      const int64_t GROUP_MODE_CNT = static_cast<int64_t>(ObIOGroupMode::MODECNT);
      uint64_t group_config_index = 0;

      if (info.count() % GROUP_MODE_CNT != 0 ) {
        LOG_WARN("unexpected group count", K(ret), K(info.count()));
      } else {
        for (int64_t left = 0; left < info.count() && OB_SUCC(ret); left += GROUP_MODE_CNT) {
          const int64_t local_read_index = left + static_cast<int64_t>(ObIOGroupMode::LOCALREAD);
          const int64_t local_write_index = left + static_cast<int64_t>(ObIOGroupMode::LOCALWRITE);

          int64_t group_min_iops = 0, group_max_iops = 0, group_iops_weight = 0;

          if (OB_TMP_FAIL(oceanbase::common::transform_usage_index_to_group_config_index(
                  local_read_index, group_config_index))) {
          } else if (group_config_index >= io_config.group_configs_.count()) {
            LOG_WARN("unexpected group config index", K(ret), K(group_config_index), K(io_config.group_configs_.count()));
          } else if (io_config.group_configs_.at(group_config_index).cleared_ ||
                     io_config.group_configs_.at(group_config_index).deleted_) {
            // do nothing
          } else if (OB_FAIL(io_config.calc_group_config(group_config_index,
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
            read_item.norm_iops_ = oceanbase::common::get_norm_iops(
                info.at(local_read_index).avg_byte_, info.at(local_read_index).avg_iops_, ObIOMode::READ);
            if (OB_FAIL(group_io_stats_.push_back(read_item))) {
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
              write_item.norm_iops_ = oceanbase::common::get_norm_iops(
                  info.at(local_write_index).avg_byte_, info.at(local_write_index).avg_iops_, ObIOMode::WRITE);
              if (OB_FAIL(group_io_stats_.push_back(write_item))) {
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
    LOG_WARN("io manager is null", K(ret));
  } else {
    const int64_t GROUP_MODE_CNT = static_cast<int64_t>(ObIOGroupMode::MODECNT);
    ObIOUsage sys_io_usage;
    if (OB_FAIL(sys_io_usage.init(2))) {
    } else if (OB_FAIL(sys_io_usage.assign(io_manager->get_sys_io_usage()))) {
    } else {
      sys_io_usage.calculate_io_usage();
      const ObIOUsageInfoArray &info = sys_io_usage.get_io_usage();

      if (info.count() % GROUP_MODE_CNT != 0 ) {
        LOG_WARN("unexpected group count", K(ret), K(info.count()));
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
          read_item.real_iops_ = static_cast<int64_t>(info.at(local_read_index).avg_iops_);
          read_item.norm_iops_ = oceanbase::common::get_norm_iops(
              info.at(local_read_index).avg_byte_, info.at(local_read_index).avg_iops_, ObIOMode::READ);
          if (OB_FAIL(group_io_stats_.push_back(read_item))) {
          } else {
            GroupIoStat write_item;
            write_item.mode_ = ObIOMode::WRITE;
            write_item.group_id_ = sys_group_id;
            snprintf(write_item.group_name_, sizeof(write_item.group_name_), "%s", group_name);
            write_item.min_iops_ = 0;
            write_item.max_iops_ = INT64_MAX;
            write_item.real_iops_ = static_cast<int64_t>(info.at(local_write_index).avg_iops_);
            write_item.norm_iops_ = oceanbase::common::get_norm_iops(
                info.at(local_write_index).avg_byte_, info.at(local_write_index).avg_iops_, ObIOMode::WRITE);
            if (OB_FAIL(group_io_stats_.push_back(write_item))) {
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
    LOG_WARN("not init", K(ret), KP(cur_row_.cells_), K(is_inited_));
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
          cells[i].set_int(item.max_iops_);
          break;
        }
        case MIN_IOPS: {
          cells[i].set_int(item.min_iops_);
          break;
        }
        case NORM_IOPS: {
          cells[i].set_int(item.norm_iops_);
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
