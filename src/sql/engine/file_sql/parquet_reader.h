#pragma once

#include "sql/engine/file_sql/csv_reader.h"
#include <functional>

namespace oceanbase::sql::file_sql {

// Minimal read-only Parquet reader for flat, non-repeated schemas.
// Values are exposed as text so FileSqlScan can reuse one conversion path.
class ParquetReader {
public:
  ParquetReader();
  ~ParquetReader();
  ParquetReader(const ParquetReader &) = delete;
  ParquetReader &operator=(const ParquetReader &) = delete;

  int open(int fd, int64_t file_size, const std::function<bool()> &keep_running);
  int infer(Schema &schema);
  int begin();
  int next(Row &row);
  void close();

private:
  struct Impl;
  Impl *impl_;
};
} // namespace oceanbase::sql::file_sql
