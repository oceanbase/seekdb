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

#pragma once

#include "sql/engine/basic/ob_file_scan_utils.h"

#include <vector>

namespace oceanbase
{
namespace sql
{

/**
 * @brief The reader of a glob pattern.
 *
 * The reader produces one row per file that matches the pattern, the row holds
 * the path of the file in the single column named "file". The pattern is the
 * one of the shell: '*' matches any sequence of characters that does not cross
 * a directory boundary, '?' matches one character, '[...]' matches a character
 * set and '**' matches any number of directory levels.
 */
class ObGlobFileScanReader : public ObFileScanReader
{
public:
  ObGlobFileScanReader(const ObFileScanOptions &options,
                       const common::ObIArray<ObFileColumnDef> &columns);
  virtual ~ObGlobFileScanReader();
  /**
   * @brief Expand the pattern and prepare the iteration over the matches.
   */
  virtual int open() override;
  /**
   * @brief Produce the path of the next matching file.
   *
   * @return OB_SUCCESS when a file was produced, OB_ITER_END when every file
   *         that matches the pattern was produced
   */
  virtual int next_row() override;
  /**
   * @brief Release the resources of the reader.
   */
  virtual int close() override;
  /**
   * @brief Produce the single column of the reader, it is named "file".
   */
  virtual int infer_schema(common::ObIAllocator &allocator,
                            common::ObIArray<ObFileColumnDef> &columns) override;

private:
  int expand_pattern_(const char *pattern, std::vector<std::string> &files);
  int expand_dir_(const std::string &dir,
                  const std::vector<std::string> &components,
                  const int64_t index,
                  std::vector<std::string> &files);

private:
  ObFileScanOptions options_;
  common::ObArray<ObFileColumnDef, common::ModulePageAllocator, true> columns_;
  common::ObArenaAllocator perm_alloc_;
  std::vector<std::string> files_;
  int64_t file_idx_;
  bool is_opened_;
};

} // end namespace sql
} // end namespace oceanbase
