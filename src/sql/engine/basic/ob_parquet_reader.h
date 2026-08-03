/*
 * Copyright (c) 2026 OceanBase.
 * Licensed under the Apache License, Version 2.0.
 */
#ifndef OCEANBASE_SQL_ENGINE_BASIC_OB_PARQUET_READER_H_
#define OCEANBASE_SQL_ENGINE_BASIC_OB_PARQUET_READER_H_

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

#endif // OCEANBASE_SQL_ENGINE_BASIC_OB_PARQUET_READER_H_
