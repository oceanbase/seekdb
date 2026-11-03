/**
 * (c) 2025 OceanBase
 * Streaming CSV reader implementation.
 */

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/basic/ob_file_csv_reader.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "lib/utility/ob_macro_utils.h"

namespace oceanbase
{
namespace sql
{

using namespace common;

namespace
{

const int64_t INITIAL_RECORD_CAPACITY = 4096;

inline bool is_space_char(const char c)
{
  return ' ' == c || '\t' == c || '\r' == c || '\n' == c;
}

} // namespace

ObFileCsvReader::ObFileCsvReader()
    : fd_(-1),
      data_pos_(0),
      buf_(NULL),
      buf_pos_(0),
      buf_len_(0),
      eof_(false),
      unget_(-1),
      record_(NULL),
      record_len_(0),
      record_cap_(0),
      record_no_(0),
      field_term_(','),
      quote_('"'),
      escape_('\\'),
      has_quote_(true),
      has_escape_(false),
      parse_header_(false),
      skip_header_lines_(0),
      skip_blank_lines_(false),
      trim_space_(false),
      empty_field_as_null_(false),
      has_null_str_(false),
      null_str_(),
      compression_(ObCSVGeneralFormat::ObCSVCompression::NONE),
      cs_type_(CHARSET_UTF8MB4),
      header_(),
      allocator_(NULL)
{
}

ObFileCsvReader::~ObFileCsvReader()
{
  close();
}

void ObFileCsvReader::close()
{
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  if (NULL != buf_) {
    free(buf_);
    buf_ = NULL;
  }
  if (NULL != record_) {
    free(record_);
    record_ = NULL;
  }
  buf_pos_ = 0;
  buf_len_ = 0;
  eof_ = false;
  unget_ = -1;
  record_len_ = 0;
  record_cap_ = 0;
  record_no_ = 0;
  data_pos_ = 0;
  header_.reset();
  allocator_ = NULL;
}

int ObFileCsvReader::open(const ObString &file_path,
                          const ObCSVGeneralFormat &format,
                          ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  char path_buf[PATH_MAX + 1];
  close();
  if (OB_UNLIKELY(file_path.empty() || file_path.length() > PATH_MAX)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid file path", K(ret), K(file_path));
  } else if (OB_UNLIKELY(ObCSVGeneralFormat::ObCSVCompression::NONE != format.compression_algorithm_)) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "csv compression");
  } else if (OB_UNLIKELY(NULL == (buf_ = static_cast<char *>(malloc(READ_BUF_SIZE))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    MEMCPY(path_buf, file_path.ptr(), file_path.length());
    path_buf[file_path.length()] = '\0';
    struct stat stat_info;
    if (0 != ::stat(path_buf, &stat_info)) {
      ret = OB_FILE_NOT_EXIST;
      LOG_USER_ERROR(OB_FILE_NOT_EXIST, file_path.length(), file_path.ptr());
    } else if (!S_ISREG(stat_info.st_mode)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("file path is not a regular file", K(ret), K(file_path));
    } else if (-1 == (fd_ = ::open(path_buf, O_RDONLY))) {
      ret = (ENOENT == errno) ? OB_FILE_NOT_EXIST : OB_IO_ERROR;
      if (OB_FILE_NOT_EXIST == ret) {
        LOG_USER_ERROR(OB_FILE_NOT_EXIST, file_path.length(), file_path.ptr());
      } else {
        LOG_WARN("failed to open file", K(ret), K(errno), K(file_path));
      }
    }
  }

  if (OB_SUCC(ret)) {
    allocator_ = &allocator;
    field_term_ = format.field_term_str_.length() > 0 ? format.field_term_str_.ptr()[0] : ',';
    has_quote_ = format.field_enclosed_char_ != INT64_MAX
                 && format.field_enclosed_char_ >= 0
                 && format.field_enclosed_char_ <= 0xff;
    quote_ = has_quote_ ? static_cast<char>(format.field_enclosed_char_) : '"';
    has_escape_ = format.field_escaped_char_ != INT64_MAX
                  && format.field_escaped_char_ >= 0
                  && format.field_escaped_char_ <= 0xff
                  && format.field_escaped_char_ != format.field_enclosed_char_;
    escape_ = has_escape_ ? static_cast<char>(format.field_escaped_char_) : '\\';
    parse_header_ = format.parse_header_;
    skip_header_lines_ = format.skip_header_lines_;
    if (parse_header_ && skip_header_lines_ <= 0) {
      skip_header_lines_ = 1;
    }
    skip_blank_lines_ = format.skip_blank_lines_;
    trim_space_ = format.trim_space_;
    empty_field_as_null_ = format.empty_field_as_null_;
    compression_ = format.compression_algorithm_;
    cs_type_ = format.cs_type_;
    if (format.null_if_.count() > 0 && !format.null_if_.at(0).empty()) {
      has_null_str_ = true;
      if (OB_FAIL(ob_write_string(allocator, format.null_if_.at(0), null_str_))) {
        LOG_WARN("failed to copy null literal", K(ret));
      }
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(read_header())) {
      LOG_WARN("failed to read csv header", K(ret));
    } else {
      // the lookahead byte pushed back by the header parser is logically not
      // consumed yet, so put it back before computing the data offset
      if (unget_ >= 0 && buf_pos_ > 0) {
        --buf_pos_;
        unget_ = -1;
      }
      // position of the first data byte: the file offset we reached minus
      // the bytes that are buffered but not consumed yet.
      off_t file_pos = ::lseek(fd_, 0, SEEK_CUR);
      if (file_pos < 0) {
        ret = OB_IO_ERROR;
        LOG_WARN("failed to get file offset", K(ret), K(errno));
      } else {
        data_pos_ = static_cast<int64_t>(file_pos) - (buf_len_ - buf_pos_);
        if (data_pos_ < 0) {
          data_pos_ = 0;
        }
      }
    }
  }
  if (OB_FAIL(ret)) {
    close();
  }
  return ret;
}

int ObFileCsvReader::read_header()
{
  int ret = OB_SUCCESS;
  if (skip_header_lines_ <= 0) {
    // nothing to skip
  } else {
    ObSEArray<ObFileCsvField, 16> fields;
    ObSEArray<ObFileCsvField, 16> last_fields;
    int64_t last_len = 0;
    char *last_record = NULL;
    const bool saved_skip_blank = skip_blank_lines_;
    skip_blank_lines_ = false;
    for (int64_t i = 0; OB_SUCC(ret) && i < skip_header_lines_; ++i) {
      if (OB_FAIL(next_record(fields))) {
        if (OB_ITER_END == ret) {
          // an empty file (or a header shorter than expected) is not fatal
          ret = OB_SUCCESS;
          break;
        } else {
          LOG_WARN("failed to read header line", K(ret), K(i));
        }
      } else {
        // keep a private copy of the last skipped line, next_record() reuses
        // the reader's record buffer
        char *new_record = static_cast<char *>(malloc(record_len_ > 0 ? record_len_ : 1));
        if (NULL == new_record) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else {
          if (record_len_ > 0) {
            MEMCPY(new_record, record_, record_len_);
          }
          if (NULL != last_record) {
            free(last_record);
          }
          last_record = new_record;
          last_len = record_len_;
          last_fields.reuse();
          for (int64_t j = 0; OB_SUCC(ret) && j < fields.count(); ++j) {
            ObFileCsvField field = fields.at(j);
            field.ptr_ = (NULL == last_record ? "" : last_record + (field.ptr_ - record_));
            if (OB_FAIL(last_fields.push_back(field))) {
              LOG_WARN("failed to copy header field", K(ret));
            }
          }
        }
      }
    }
    skip_blank_lines_ = saved_skip_blank;
    if (OB_SUCC(ret) && parse_header_) {
      for (int64_t i = 0; OB_SUCC(ret) && i < last_fields.count(); ++i) {
        const ObFileCsvField &field = last_fields.at(i);
        if (field.ptr_ < last_record || field.ptr_ + field.len_ > last_record + last_len) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid header field offset", K(ret));
        } else {
          ObString name;
          ObString value(field.len_, field.ptr_);
          if (OB_FAIL(ob_write_string(*allocator_, value, name))) {
            LOG_WARN("failed to copy column name", K(ret));
          } else if (OB_FAIL(header_.push_back(name))) {
            LOG_WARN("failed to add column name", K(ret));
          }
        }
      }
    }
    if (NULL != last_record) {
      free(last_record);
      last_record = NULL;
    }
  }
  return ret;
}

int ObFileCsvReader::reset()
{
  int ret = OB_SUCCESS;
  if (fd_ < 0) {
    ret = OB_NOT_INIT;
  } else if (0 != ::lseek(fd_, data_pos_, SEEK_SET)) {
    ret = OB_IO_ERROR;
    LOG_WARN("failed to seek to data start", K(ret), K(errno), K_(data_pos));
  } else {
    buf_pos_ = 0;
    buf_len_ = 0;
    eof_ = false;
    unget_ = -1;
    record_len_ = 0;
    record_no_ = 0;
  }
  return ret;
}

int ObFileCsvReader::refill()
{
  int ret = OB_SUCCESS;
  if (eof_) {
    buf_pos_ = 0;
    buf_len_ = 0;
  } else {
    ssize_t n = 0;
    do {
      n = ::read(fd_, buf_, READ_BUF_SIZE);
    } while (n < 0 && EINTR == errno);
    if (n < 0) {
      ret = OB_IO_ERROR;
      LOG_WARN("failed to read file", K(ret), K(errno));
    } else {
      buf_pos_ = 0;
      buf_len_ = n;
      if (0 == n) {
        eof_ = true;
      }
    }
  }
  return ret;
}

int ObFileCsvReader::next_byte(int64_t &ch)
{
  int ret = OB_SUCCESS;
  if (-1 != unget_) {
    ch = unget_;
    unget_ = -1;
  } else {
    if (buf_pos_ >= buf_len_ && OB_FAIL(refill())) {
      LOG_WARN("failed to refill buffer", K(ret));
    } else if (buf_pos_ < buf_len_) {
      ch = static_cast<unsigned char>(buf_[buf_pos_++]);
    } else {
      ch = -1;
    }
  }
  return ret;
}

void ObFileCsvReader::unget_byte(const int64_t ch)
{
  if (ch >= 0) {
    unget_ = ch;
  }
}

int ObFileCsvReader::ensure_record_capacity(const int64_t needed)
{
  int ret = OB_SUCCESS;
  if (needed > MAX_RECORD_SIZE) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("csv record is too large", K(needed), K(MAX_RECORD_SIZE), K_(record_no));
  } else if (needed > record_cap_) {
    int64_t new_cap = record_cap_ > 0 ? record_cap_ : INITIAL_RECORD_CAPACITY;
    while (new_cap < needed) {
      new_cap *= 2;
    }
    if (new_cap > MAX_RECORD_SIZE) {
      new_cap = MAX_RECORD_SIZE;
    }
    char *new_buf = static_cast<char *>(malloc(new_cap));
    if (NULL == new_buf) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      if (record_len_ > 0 && NULL != record_) {
        MEMCPY(new_buf, record_, record_len_);
      }
      if (NULL != record_) {
        free(record_);
      }
      record_ = new_buf;
      record_cap_ = new_cap;
    }
  }
  return ret;
}

int ObFileCsvReader::append_char(const char ch)
{
  int ret = OB_SUCCESS;
  if (record_len_ >= record_cap_ && OB_FAIL(ensure_record_capacity(record_len_ + 1))) {
    LOG_WARN("failed to grow record buffer", K(ret));
  } else {
    record_[record_len_++] = ch;
  }
  return ret;
}

int ObFileCsvReader::append_bytes(const char *ptr, const int64_t len)
{
  int ret = OB_SUCCESS;
  if (len <= 0) {
    // nothing to do
  } else if (record_len_ + len > record_cap_ && OB_FAIL(ensure_record_capacity(record_len_ + len))) {
    LOG_WARN("failed to grow record buffer", K(ret));
  } else {
    MEMCPY(record_ + record_len_, ptr, len);
    record_len_ += len;
  }
  return ret;
}

int ObFileCsvReader::finish_field(ObIArray<ObFileCsvField> &fields,
                                  const int64_t field_start,
                                  const bool quoted)
{
  int ret = OB_SUCCESS;
  int64_t len = record_len_ - field_start;
  const char *ptr = (NULL == record_ ? "" : record_ + field_start);
  if (trim_space_ && !quoted) {
    while (len > 0 && is_space_char(*ptr)) {
      ++ptr;
      --len;
    }
    while (len > 0 && is_space_char(ptr[len - 1])) {
      --len;
    }
  }
  bool is_null = false;
  if (!quoted) {
    if (has_null_str_) {
      if (len == null_str_.length()
          && (0 == len || 0 == memcmp(ptr, null_str_.ptr(), len))) {
        is_null = true;
      }
    } else if (empty_field_as_null_ && 0 == len) {
      is_null = true;
    }
  }
  ObFileCsvField field;
  field.ptr_ = ptr;
  field.len_ = static_cast<int32_t>(len);
  field.is_null_ = is_null;
  if (OB_FAIL(fields.push_back(field))) {
    LOG_WARN("failed to add field", K(ret));
  }
  return ret;
}

int ObFileCsvReader::next_record(ObIArray<ObFileCsvField> &fields)
{
  int ret = OB_SUCCESS;
  bool skip_current = false;
  fields.reuse();
  do {
    skip_current = false;
    record_len_ = 0;
    int64_t field_start = 0;
    bool in_quotes = false;
    bool quoted = false;
    bool any_char = false;
    bool any_quote = false;
    bool record_done = false;
    ++record_no_;
    while (OB_SUCC(ret) && !record_done) {
      int64_t ch = 0;
      if (OB_FAIL(next_byte(ch))) {
        LOG_WARN("failed to read byte", K(ret));
      } else if (ch < 0) {
        // end of file
        if (in_quotes) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unterminated quoted field at end of file", K(ret), K_(record_no));
        } else if (!any_char) {
          ret = OB_ITER_END;
        } else {
          if (OB_FAIL(finish_field(fields, field_start, quoted))) {
            LOG_WARN("failed to finish field", K(ret));
          }
          record_done = true;
        }
      } else if (in_quotes) {
        if (has_quote_ && ch == static_cast<int64_t>(static_cast<unsigned char>(quote_))) {
          int64_t next_ch = 0;
          if (OB_FAIL(next_byte(next_ch))) {
            LOG_WARN("failed to read byte", K(ret));
          } else if (next_ch == static_cast<int64_t>(static_cast<unsigned char>(quote_))) {
            // doubled quote -> literal quote
            if (OB_FAIL(append_char(quote_))) {
              LOG_WARN("failed to append char", K(ret));
            }
          } else {
            in_quotes = false;
            if (next_ch >= 0) {
              unget_byte(next_ch);
            }
          }
        } else if (has_escape_ && ch == static_cast<int64_t>(static_cast<unsigned char>(escape_))) {
          int64_t next_ch = 0;
          if (OB_FAIL(next_byte(next_ch))) {
            LOG_WARN("failed to read byte", K(ret));
          } else if (next_ch >= 0 && OB_FAIL(append_char(static_cast<char>(next_ch)))) {
            LOG_WARN("failed to append char", K(ret));
          }
        } else if ('\r' == ch) {
          int64_t next_ch = 0;
          if (OB_FAIL(next_byte(next_ch))) {
            LOG_WARN("failed to read byte", K(ret));
          } else if ('\n' == next_ch) {
            if (OB_FAIL(append_char('\n'))) {
              LOG_WARN("failed to append char", K(ret));
            }
          } else {
            if (OB_FAIL(append_char('\r'))) {
              LOG_WARN("failed to append char", K(ret));
            }
            unget_byte(next_ch);
          }
        } else if (OB_FAIL(append_char(static_cast<char>(ch)))) {
          LOG_WARN("failed to append char", K(ret));
        }
        any_char = true;
      } else {
        if (ch == static_cast<int64_t>(static_cast<unsigned char>(field_term_))) {
          if (OB_FAIL(finish_field(fields, field_start, quoted))) {
            LOG_WARN("failed to finish field", K(ret));
          } else {
            field_start = record_len_;
            quoted = false;
          }
        } else if ('\n' == ch) {
          if (OB_FAIL(finish_field(fields, field_start, quoted))) {
            LOG_WARN("failed to finish field", K(ret));
          }
          record_done = true;
        } else if ('\r' == ch) {
          int64_t next_ch = 0;
          if (OB_FAIL(next_byte(next_ch))) {
            LOG_WARN("failed to read byte", K(ret));
          } else if ('\n' != next_ch) {
            unget_byte(next_ch);
          }
          if (OB_FAIL(finish_field(fields, field_start, quoted))) {
            LOG_WARN("failed to finish field", K(ret));
          }
          record_done = true;
        } else if (has_quote_ && ch == static_cast<int64_t>(static_cast<unsigned char>(quote_))
                   && record_len_ == field_start) {
          in_quotes = true;
          quoted = true;
          any_quote = true;
        } else if (has_escape_ && ch == static_cast<int64_t>(static_cast<unsigned char>(escape_))) {
          int64_t next_ch = 0;
          if (OB_FAIL(next_byte(next_ch))) {
            LOG_WARN("failed to read byte", K(ret));
          } else if (next_ch >= 0 && OB_FAIL(append_char(static_cast<char>(next_ch)))) {
            LOG_WARN("failed to append char", K(ret));
          }
        } else if (OB_FAIL(append_char(static_cast<char>(ch)))) {
          LOG_WARN("failed to append char", K(ret));
        }
        any_char = true;
      }
    }
    if (OB_SUCC(ret) && skip_blank_lines_ && 1 == fields.count()
        && 0 == record_len_ && !any_quote) {
      // an empty line is not a record
      skip_current = true;
    }
  } while (OB_SUCC(ret) && skip_current);

  if (OB_SUCC(ret) && fields.empty()) {
    ret = OB_ITER_END;
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
