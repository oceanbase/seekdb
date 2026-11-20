/*
 * Copyright (c) 2026 OceanBase.
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
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "lib/ob_errno.h"
#include "sql/engine/basic/ob_file_scan_utils.h"

namespace oceanbase
{
namespace sql
{
class ObParquetReader
{
public:
  ObParquetReader();
  ~ObParquetReader();

  static int infer_schema(const std::string &path,
                          std::vector<ObFileColumnSchema> &columns,
                          int64_t &row_count);
  int open(const std::string &path,
           const std::vector<ObFileColumnSchema> &columns,
           uint64_t expected_device,
           uint64_t expected_inode,
           int64_t expected_file_size,
           int64_t expected_modified_time_ns,
           const std::vector<bool> &projected_columns);
  int get_next_row(std::vector<ObFileCell> &cells);
  int rescan();
  void close();

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace sql
} // namespace oceanbase
