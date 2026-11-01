#pragma once

#include "sql/engine/file_sql/csv_reader.h"
#include "lib/string/ob_string.h"
#include <array>
#include <memory>
#include <optional>
#include <streambuf>
#include <string>
#include <vector>

namespace oceanbase::sql {
class ObSQLSessionInfo;
class ObJsonTableSpec;
class ObEvalCtx;

namespace file_sql {
class JsonlReader;
class ParquetReader;

enum class FileSqlFormat { csv, jsonl, parquet, glob };

class FileBuffer : public std::streambuf {
public:
  void reset(int fd) { fd_ = fd; failed_ = false; status_ = 0; setg(buffer_.data(), buffer_.data(), buffer_.data()); }
  bool failed() const { return failed_; }
  void set_session(ObSQLSessionInfo *session) { session_ = session; }
  int check_status();
  int status() const { return status_; }
protected:
  int_type underflow() override;
private:
  int fd_ = -1;
  bool failed_ = false;
  int status_ = 0;
  ObSQLSessionInfo *session_ = nullptr;
  std::array<char, 65536> buffer_;
};

class FileSqlScan {
public:
  explicit FileSqlScan(FileSqlFormat format = FileSqlFormat::csv);
  ~FileSqlScan();
  FileSqlScan(const FileSqlScan &) = delete;
  FileSqlScan &operator=(const FileSqlScan &) = delete;
  int open(ObSQLSessionInfo &session, const common::ObString &path,
           const common::ObString &expected_fingerprint = common::ObString());
  int infer(bool header, Schema &schema);
  int begin(bool header);
  int next(const ObJsonTableSpec &spec, ObEvalCtx &ctx);
  void close();
  const std::string &canonical_path() const { return canonical_path_; }
  const std::string &fingerprint() const { return fingerprint_; }
private:
  int check_identity();
  int open_glob(ObSQLSessionInfo &session, const std::string &pattern);
  int infer_glob(Schema &schema);
  int next_glob(Row &row);
  int fd_ = -1;
  FileSqlFormat format_ = FileSqlFormat::csv;
  FileBuffer buffer_;
  std::istream input_;
  std::optional<CsvReader> reader_;
  std::unique_ptr<JsonlReader> json_reader_;
  std::unique_ptr<ParquetReader> parquet_reader_;
  std::vector<std::string> glob_files_;
  size_t glob_pos_ = 0;
  Row row_;
  std::string canonical_path_;
  std::string fingerprint_;
};
} // namespace file_sql
} // namespace oceanbase::sql
