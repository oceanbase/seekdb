#define USING_LOG_PREFIX SQL_ENG
#include "file_sql_scan.h"
#include "jsonl_reader.h"
#include "parquet_reader.h"
#include "sql/engine/basic/ob_json_table_op.h"
#include "sql/resolver/ob_resolver_utils.h"
#include "sql/session/ob_sql_session_info.h"
#include "common/timezone/ob_time_convert.h"
#include "lib/charset/ob_charset.h"
#include <charconv>
#include <cerrno>
#include <string_view>
#include <unordered_map>
#if defined(__linux__)
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace oceanbase::sql::file_sql {
using namespace common;
using namespace share;

int FileBuffer::check_status()
{
  if (status_ == OB_SUCCESS) status_ = THIS_WORKER.check_status();
  if (status_ == OB_SUCCESS && session_ != nullptr) status_ = session_->check_session_status();
  return status_;
}

FileBuffer::int_type FileBuffer::underflow()
{
#if defined(__linux__)
  if (gptr() < egptr()) return traits_type::to_int_type(*gptr());
  ssize_t size = 0;
  do {
    if (check_status() != OB_SUCCESS) return traits_type::eof();
    size = ::read(fd_, buffer_.data(), buffer_.size());
  } while (size < 0 && errno == EINTR);
  if (size <= 0) { failed_ = size < 0; return traits_type::eof(); }
  setg(buffer_.data(), buffer_.data(), buffer_.data() + size);
  return traits_type::to_int_type(*gptr());
#else
  return traits_type::eof();
#endif
}

static int malformed(bool jsonl = false)
{
  LOG_USER_ERROR(OB_ERR_PARSE_SQL, jsonl ? "malformed JSONL input" : "malformed CSV input", 8, "file_sql", 1);
  return OB_ERR_PARSE_SQL;
}

#if defined(__linux__)
static std::string identity(const struct stat &value)
{
  return std::to_string(value.st_dev) + ":" + std::to_string(value.st_ino) + ":" +
      std::to_string(value.st_size) + ":" + std::to_string(value.st_mtim.tv_sec) + ":" +
      std::to_string(value.st_mtim.tv_nsec) + ":" + std::to_string(value.st_ctim.tv_sec) + ":" +
      std::to_string(value.st_ctim.tv_nsec);
}
#endif

FileSqlScan::FileSqlScan(FileSqlFormat format) : format_(format), input_(&buffer_) {}
FileSqlScan::~FileSqlScan() { close(); }
void FileSqlScan::close()
{
  reader_.reset();
  json_reader_.reset();
  parquet_reader_.reset();
  glob_files_.clear();
  glob_pos_ = 0;
#if defined(__linux__)
  if (fd_ >= 0) ::close(fd_);
#endif
  fd_ = -1;
  buffer_.reset(-1);
  buffer_.set_session(nullptr);
  input_.clear();
  row_.clear();
}

int FileSqlScan::open(ObSQLSessionInfo &session, const ObString &path, const ObString &expected_fingerprint)
{
  close();
  int ret = OB_SUCCESS;
  bool enabled = false;
  if (OB_FAIL(session.get_sys_variable(SYS_VAR_ENABLE_FILE_SQL, enabled))) {
  } else if (!enabled) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "File SQL is disabled");
  }
#if defined(__linux__)
  ObString secure;
  std::string pathname(path.ptr() == nullptr ? "" : std::string(path.ptr(), path.length()));
  char resolved[PATH_MAX];
  if (OB_FAIL(ret)) {
  } else if (pathname.empty() || pathname.find('\0') != std::string::npos) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "File SQL path");
  } else if (OB_FAIL(session.get_secure_file_priv(secure))) {
  } else if (FileSqlFormat::glob == format_) {
    ret = open_glob(session, pathname);
  } else if (::realpath(pathname.c_str(), resolved) == nullptr) {
    ret = OB_FILE_NOT_EXIST;
  } else if (OB_FAIL(ObResolverUtils::check_secure_path(secure, ObString(resolved)))) {
  } else {
    canonical_path_ = resolved;
    fd_ = ::open(resolved, O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
    struct stat info;
    if (fd_ < 0 || ::fstat(fd_, &info) != 0 || !S_ISREG(info.st_mode)) {
      ret = OB_ERR_NO_PRIVILEGE;
    } else {
      // Validate the object actually opened, closing the path/symlink race.
      const std::string descriptor = "/proc/self/fd/" + std::to_string(fd_);
      if (::realpath(descriptor.c_str(), resolved) == nullptr) {
        ret = OB_ERR_NO_PRIVILEGE;
      } else if (OB_FAIL(ObResolverUtils::check_secure_path(secure, ObString(resolved)))) {
      } else {
        canonical_path_ = resolved;
        fingerprint_ = identity(info);
        if (!expected_fingerprint.empty() && fingerprint_ != std::string(expected_fingerprint.ptr(), expected_fingerprint.length())) {
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "File changed after schema inference");
        } else if (FileSqlFormat::parquet == format_) {
          parquet_reader_.reset(new (std::nothrow) ParquetReader());
          if (parquet_reader_ == nullptr) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
          } else {
            auto keep_running = [this] { return buffer_.check_status() == OB_SUCCESS; };
            ret = parquet_reader_->open(fd_, info.st_size, keep_running);
          }
        }
      }
    }
  }
  if (OB_SUCC(ret)) { buffer_.reset(fd_); buffer_.set_session(&session); input_.clear(); }
#else
  UNUSED(path);
  UNUSED(expected_fingerprint);
  ret = OB_NOT_SUPPORTED;
#endif
  if (OB_FAIL(ret)) close();
  return ret;
}

int FileSqlScan::open_glob(ObSQLSessionInfo &session, const std::string &pattern)
{
  int ret = OB_SUCCESS;
#if defined(__linux__)
  ObString secure;
  if (OB_FAIL(session.get_secure_file_priv(secure))) return ret;
  // Only literal paths are accepted; every resolved match still goes through
  // the regular secure-path check below.
  if (pattern.find('\'') != std::string::npos || pattern.find('\n') != std::string::npos) {
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "File SQL glob pattern");
    return OB_NOT_SUPPORTED;
  }
  glob_t matches;
  memset(&matches, 0, sizeof(matches));
  const int result = ::glob(pattern.c_str(), GLOB_NOSORT | GLOB_ERR, nullptr, &matches);
  if (0 != result && GLOB_NOMATCH != result) {
    ::globfree(&matches);
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "File SQL glob pattern");
    return OB_NOT_SUPPORTED;
  }
  for (size_t i = 0; OB_SUCC(ret) && i < matches.gl_pathc; ++i) {
    char resolved[PATH_MAX];
    struct stat info;
    if (::realpath(matches.gl_pathv[i], resolved) == nullptr) {
      continue;
    } else if (OB_FAIL(ObResolverUtils::check_secure_path(secure, ObString(resolved)))) {
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "File SQL glob path");
      ret = OB_ERR_NO_PRIVILEGE;
      break;
    } else if (::stat(resolved, &info) != 0 || !S_ISREG(info.st_mode)) {
      continue;
    } else {
      glob_files_.emplace_back(resolved);
    }
  }
  if (OB_SUCC(ret)) canonical_path_ = pattern;
#endif
  return ret;
}

int FileSqlScan::check_identity()
{
  if (FileSqlFormat::glob == format_) return OB_SUCCESS;
#if defined(__linux__)
  struct stat value;
  if (fd_ < 0 || ::fstat(fd_, &value) != 0) return OB_IO_ERROR;
  if (identity(value) != fingerprint_) {
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "File changed during scan");
    return OB_NOT_SUPPORTED;
  }
  return OB_SUCCESS;
#else
  return OB_NOT_SUPPORTED;
#endif
}

int FileSqlScan::infer(bool header, Schema &schema)
{
  int ret = OB_SUCCESS;
  if (FileSqlFormat::glob == format_) {
    ret = infer_glob(schema);
  } else if (FileSqlFormat::parquet == format_) {
    if (parquet_reader_ == nullptr) ret = OB_NOT_INIT;
    else ret = parquet_reader_->infer(schema);
  } else if (FileSqlFormat::jsonl == format_) {
    JsonlReader reader(input_);
    ret = reader.infer(schema, [this] { return buffer_.check_status() == OB_SUCCESS; });
  } else {
    const ReadResult result = infer_csv(input_, header, schema, [this] { return buffer_.check_status() == OB_SUCCESS; });
    if (result == ReadResult::io_error) ret = OB_IO_ERROR;
    else if (result == ReadResult::too_large) ret = OB_SIZE_OVERFLOW;
    else if (result != ReadResult::end) ret = OB_ERR_PARSE_SQL;
  }
  if (buffer_.status() != OB_SUCCESS) return buffer_.status();
  if (buffer_.failed()) return OB_IO_ERROR;
  if (ret != OB_SUCCESS) return ret == OB_ERR_PARSE_SQL ? malformed(FileSqlFormat::jsonl == format_) : ret;
  // SQL resolves identifiers with GENERAL_CI, including accent equivalence.
  // Finalize each name against all prior finalized names, including suffixes.
  for (size_t i = 0; i < schema.size(); ++i) {
    const std::string base = schema[i].name;
    size_t suffix = 2;
    for (;;) {
      if (buffer_.check_status() != OB_SUCCESS) return buffer_.status();
      bool duplicate = false;
      const std::string &name = schema[i].name;
      for (size_t j = 0; !duplicate && j < i; ++j) {
        const std::string &prior = schema[j].name;
        duplicate = ObCharset::strcmp(CS_TYPE_UTF8MB4_GENERAL_CI,
            ObString(name.size(), name.data()), ObString(prior.size(), prior.data())) == 0;
      }
      if (!duplicate) break;
      schema[i].name = base + "__" + std::to_string(suffix++);
    }
    if (schema[i].name.size() > 128) {
      LOG_USER_ERROR(OB_WRONG_COLUMN_NAME, static_cast<int>(schema[i].name.size()), schema[i].name.data());
      return OB_WRONG_COLUMN_NAME;
    }
  }
  return check_identity();
}

int FileSqlScan::begin(bool header)
{
  if (FileSqlFormat::glob == format_) {
    glob_pos_ = 0;
    return OB_SUCCESS;
  }
  if (FileSqlFormat::parquet == format_) {
    return parquet_reader_ == nullptr ? OB_NOT_INIT : parquet_reader_->begin();
  }
#if defined(__linux__)
  if (::lseek(fd_, 0, SEEK_SET) < 0) return OB_IO_ERROR;
#else
  return OB_NOT_SUPPORTED;
#endif
  input_.clear();
  buffer_.reset(fd_);
  if (FileSqlFormat::jsonl == format_) {
    json_reader_.reset(new (std::nothrow) JsonlReader(input_));
    return json_reader_ == nullptr ? OB_ALLOCATE_MEMORY_FAILED : buffer_.check_status();
  }
  reader_.emplace(input_);
  if (buffer_.status() != OB_SUCCESS) return buffer_.status();
  if (header) {
    const ReadResult result = reader_->next(row_);
    if (buffer_.status() != OB_SUCCESS) return buffer_.status();
    if (result != ReadResult::row) return malformed();
  }
  return OB_SUCCESS;
}

int FileSqlScan::next(const ObJsonTableSpec &spec, ObEvalCtx &ctx)
{
  if (FileSqlFormat::parquet == format_) {
    if (parquet_reader_ == nullptr) return OB_NOT_INIT;
  } else if (FileSqlFormat::glob == format_) {
  } else if ((FileSqlFormat::jsonl == format_ && !json_reader_) ||
             (FileSqlFormat::csv == format_ && !reader_)) {
    return OB_NOT_INIT;
  }
  if (buffer_.check_status() != OB_SUCCESS) return buffer_.status();
  int read_ret = OB_SUCCESS;
  if (FileSqlFormat::glob == format_) {
    read_ret = next_glob(row_);
  } else if (FileSqlFormat::parquet == format_) {
    read_ret = parquet_reader_->next(row_);
  } else if (FileSqlFormat::jsonl == format_) {
    NamedRow source;
    read_ret = json_reader_->next(source);
    if (read_ret == OB_SUCCESS) {
      row_.assign(spec.cols_def_.count() - 1, Field{});
      std::unordered_map<std::string_view, Field *> fields;
      for (NamedField &field : source) fields.emplace(field.name, &field.value);
      for (int64_t i = 1; i < spec.cols_def_.count(); ++i) {
        const ObString &key = spec.cols_def_.at(i)->path_;
        const auto field = fields.find(std::string_view(key.ptr(), key.length()));
        if (field != fields.end()) row_[i - 1] = std::move(*field->second);
      }
    }
  } else {
    const ReadResult result = reader_->next(row_);
    if (result == ReadResult::end) read_ret = OB_ITER_END;
    else if (result == ReadResult::io_error) read_ret = OB_IO_ERROR;
    else if (result == ReadResult::too_large) read_ret = OB_SIZE_OVERFLOW;
    else if (result != ReadResult::row) read_ret = OB_ERR_PARSE_SQL;
  }
  if (buffer_.status() != OB_SUCCESS) return buffer_.status();
  if (buffer_.failed()) return OB_IO_ERROR;
  if (read_ret == OB_ITER_END) {
    const int checked = check_identity();
    return checked == OB_SUCCESS ? OB_ITER_END : checked;
  }
  if (read_ret != OB_SUCCESS) return read_ret == OB_ERR_PARSE_SQL ? malformed(FileSqlFormat::jsonl == format_) : read_ret;
  // The first definition is the multimode table's structural root.
  if (row_.size() + 1 != static_cast<size_t>(spec.cols_def_.count())) return malformed();
  int ret = OB_SUCCESS;
  for (int64_t i = 1; OB_SUCC(ret) && i < spec.cols_def_.count(); ++i) {
    const ObJtColInfo &column = *spec.cols_def_.at(i);
    if (column.output_column_idx_ < 0) continue;
    if (column.output_column_idx_ >= spec.column_exprs_.count()) return OB_ERR_UNEXPECTED;
    ObExpr &expr = *spec.column_exprs_.at(column.output_column_idx_);
    ObDatum &datum = expr.locate_datum_for_write(ctx);
    Field &field = row_[i - 1];
    if (field.is_null()) {
      datum.set_null();
    } else {
      const char *first = field.text.data(), *last = first + field.text.size();
      const ObObjType type = expr.datum_meta_.type_;
      if (type == ObIntType) {
        int64_t value = 0;
        auto parsed = std::from_chars(first, last, value);
        if (parsed.ec != std::errc() || parsed.ptr != last) ret = malformed();
        else datum.set_int(value);
      } else if (type == ObDoubleType) {
        double value = 0;
        auto parsed = std::from_chars(first, last, value);
        if (parsed.ec != std::errc() || parsed.ptr != last) ret = malformed();
        else datum.set_double(value);
      } else if (type == ObTinyIntType) {
        datum.set_int(field.text[0] == 't' || field.text[0] == 'T' ? 1 : 0);
      } else if (type == ObDateType) {
        int32_t value = 0;
        if (OB_FAIL(ObTimeConverter::str_to_date(ObString(field.text.size(), first), value))) {} else datum.set_date(value);
      } else if (type == ObDateTimeType) {
        int64_t value = 0;
        ObTimeConvertCtx convert(nullptr, false);
        if (OB_FAIL(ObTimeConverter::str_to_datetime(ObString(field.text.size(), first), convert, value))) {} else datum.set_datetime(value);
      } else {
        datum.set_string(first, field.text.size());
      }
    }
    expr.get_eval_info(ctx).evaluated_ = true;
  }
  return ret;
}
int FileSqlScan::infer_glob(Schema &schema)
{
  schema.clear();
  Column column;
  column.name = "file";
  column.type = Type::text;
  column.nullable = false;
  column.max_bytes = 4096;
  column.source_name = "file";
  schema.push_back(std::move(column));
  return OB_SUCCESS;
}

int FileSqlScan::next_glob(Row &row)
{
  if (glob_pos_ >= glob_files_.size()) return OB_ITER_END;
  row.clear();
  Field field;
  field.text = glob_files_[glob_pos_++];
  field.quoted = true;
  row.push_back(std::move(field));
  return OB_SUCCESS;
}
} // namespace oceanbase::sql::file_sql
