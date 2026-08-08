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

#ifndef OCEANBASE_STORAGE_OB_DISK_USAGE_REPORTER_H_
#define OCEANBASE_STORAGE_OB_DISK_USAGE_REPORTER_H_

#include "lib/task/ob_timer.h"
#include "lib/hash/ob_hashmap.h"
#include "lib/container/ob_array.h"
#include "data_plane/report/ob_i_disk_report.h"
#include "lib/allocator/ob_fifo_allocator.h"

namespace oceanbase
{
namespace common
{
  class ObMySQLProxy;
}

namespace storage
{
enum class ObDiskReportFileType : uint8_t
{
  SERVER_DATA = 0,
  LOCAL_STORAGE_META_DATA = 1,
  SERVER_INDEX_DATA = 2,
  SERVER_TMP_DATA = 3,
  SERVER_SLOG_DATA = 4,
  SERVER_CLOG_DATA = 5,
  SERVER_MAJOR_DATA = 6,
  TYPE_MAX
};

struct ObDiskUsageReportKey
{
  ObDiskReportFileType file_type_;

  uint64_t hash() const
  {
    uint64_t hash_value = static_cast<uint64_t>(file_type_) * 10000L;
    return hash_value;
  }

  int hash(uint64_t &hash_val) const
  {
    hash_val = hash();
    return OB_SUCCESS;
  }

  bool operator ==(const ObDiskUsageReportKey report_key) const
  {
    return report_key.file_type_ == file_type_;
  }

  TO_STRING_KV(K_(file_type));
};

typedef hash::HashMapPair<ObDiskUsageReportKey, std::pair<int64_t, int64_t>> ObDiskUsageReportMap;// pair(occupy_size, required_size)

class ObDiskUsageReportTask : public data_plane::ObIDiskReport
{
public:
  ObDiskUsageReportTask();
  virtual ~ObDiskUsageReportTask() {};
  int init(common::ObMySQLProxy &sql_proxy);
  void destroy();
  virtual int delete_usage_stat() override;

  // Get data-disk usage for the local server runtime.
  int get_data_disk_used_size(int64_t &used_size);

private:
  int count_server_data();

  typedef common::hash::ObHashMap<ObDiskUsageReportKey, std::pair<int64_t, int64_t>> ReportResultMap; // pair(occupy_size, required_size)
private:
  bool is_inited_;
  ReportResultMap result_map_;
  common::ObMySQLProxy *sql_proxy_;
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_DISK_USAGE_REPORTER_H_
