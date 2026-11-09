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

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/basic/ob_file_scan_reader.h"

#include "common/json_type/ob_json_base.h"
#include "common/json_type/ob_json_parse.h"
#include "common/json_type/ob_json_tree.h"
#include "lib/allocator/page_arena.h"
#include "lib/container/ob_array.h"
#include "lib/file/ob_file.h"
#include "lib/string/ob_string.h"
#include "share/ob_errno.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <new>
#include <string>
#include <vector>

namespace oceanbase
{
using namespace common;
namespace sql
{

// OB_CSV_READER_CORE_BEGIN
/**
 * @brief The core of the tokenizer of the csv records.
 *
 * The core only uses the standard library, it does not depend on the rest of the
 * observer, so that it can be tested in isolation. The reader of the csv files
 * and the inference of the schema share the same tokenizer, so that the values
 * are always parsed the same way.
 */
namespace ob_csv_reader_core
{

/// The state of the tokenizer.
enum ObCsvParserState
{
  OB_CSV_STATE_FIELD_START = 0,  // the beginning of a field
  OB_CSV_STATE_UNQUOTED,         // inside an unquoted field
  OB_CSV_STATE_QUOTED,           // inside an enclosed field
  OB_CSV_STATE_AFTER_QUOTE,      // after the closing quote of an enclosed field
};

/// The result of the parsing of a piece of the file.
enum ObCsvParseResult
{
  OB_CSV_PARSE_CONTINUE = 0,   // the parser made progress
  OB_CSV_PARSE_RECORD = 1,     // a record is complete
  OB_CSV_PARSE_MORE_DATA = 2,  // more data is needed
  OB_CSV_PARSE_ERROR = -1,     // the file is malformed
};

/// One field of a record.
struct ObCsvField
{
  const char *data_;
  int64_t len_;
  bool is_null_;
  bool is_quoted_;
  ObCsvField() : data_(NULL), len_(0), is_null_(false), is_quoted_(false) {}
};

/// The description of a field while the record is parsed.
struct ObCsvFieldInfo
{
  int64_t offset_;
  int64_t len_;
  bool is_null_;
  bool is_quoted_;
  ObCsvFieldInfo() : offset_(0), len_(0), is_null_(false), is_quoted_(false) {}
};

/**
 * @brief The tokenizer of the csv records.
 *
 * The tokenizer is fed with the pieces of the file, it holds the pieces of a
 * record that is not complete yet, so that a record can span several pieces.
 */
class ObCsvReaderTokenizer
{
public:
  ObCsvReaderTokenizer()
    : delim_(","), delim_len_(1), quote_("\""), quote_len_(1), escape_("\""), escape_len_(1),
      state_(OB_CSV_STATE_FIELD_START), pos_(0), field_start_(0), field_content_start_(0),
      in_record_(false), too_large_(false), error_()
  {}
  ~ObCsvReaderTokenizer() {}

  void set_delimiter(const char *data, const int64_t len)
  {
    delim_.assign(data, static_cast<size_t>(len > 0 ? len : 0));
    delim_len_ = len;
  }

  void set_quote(const char *data, const int64_t len)
  {
    quote_.assign(data, static_cast<size_t>(len > 0 ? len : 0));
    quote_len_ = len;
  }

  void set_escape(const char *data, const int64_t len)
  {
    escape_.assign(data, static_cast<size_t>(len > 0 ? len : 0));
    escape_len_ = len;
  }

  /// Append a piece of the file to the data to parse.
  void feed(const char *data, const int64_t len)
  {
    if (NULL != data && len > 0) {
      if (static_cast<int64_t>(pending_.size()) + len > OB_FILE_TABLE_MAX_RECORD_SIZE) {
        // the record is too large, the memory that one record may use is bounded
        too_large_ = true;
        error_ = "the size of a record of the csv file is too large";
        return;
      }
      pending_.insert(pending_.end(), data, data + len);
    }
  }

  /**
   * @brief Parse the next record of the file.
   *
   * @param eof whether the data holds the end of the file
   * @return OB_CSV_PARSE_RECORD when a record is ready, OB_CSV_PARSE_MORE_DATA when
   *         more data is needed, OB_CSV_PARSE_ERROR when the file is malformed
   */
  int parse_next_record(const bool eof)
  {
    if (too_large_) {
      return OB_CSV_PARSE_ERROR;
    }
    if (!in_record_) {
      out_buf_.clear();
      field_infos_.clear();
      in_record_ = true;
      state_ = OB_CSV_STATE_FIELD_START;
      field_start_ = pos_;
    }
    for (;;) {
      const int64_t avail = static_cast<int64_t>(pending_.size()) - pos_;
      if (avail <= 0) {
        if (!eof) {
          compact();
          return OB_CSV_PARSE_MORE_DATA;
        }
        return finish_at_eof();
      }
      int ret = OB_CSV_PARSE_CONTINUE;
      switch (state_) {
        case OB_CSV_STATE_FIELD_START: ret = parse_field_start(eof); break;
        case OB_CSV_STATE_UNQUOTED: ret = parse_unquoted(eof); break;
        case OB_CSV_STATE_QUOTED: ret = parse_quoted(eof); break;
        case OB_CSV_STATE_AFTER_QUOTE: ret = parse_after_quote(eof); break;
        default:
          ret = OB_CSV_PARSE_ERROR;
          error_ = "the csv tokenizer is in an unknown state";
          break;
      }
      if (OB_CSV_PARSE_CONTINUE == ret) {
        continue;
      }
      if (OB_CSV_PARSE_MORE_DATA == ret) {
        if (!eof) {
          compact();
          return OB_CSV_PARSE_MORE_DATA;
        }
        return finish_at_eof();
      }
      return ret;
    }
  }

  /// The fields of the last parsed record.
  const std::vector<ObCsvField> &get_fields() const { return fields_; }
  /// The description of the error of the last parsed record.
  const std::string &get_error() const { return error_; }

private:
  int parse_field_start(const bool eof)
  {
    const int match = match_seq(pos_, quote_, quote_len_, eof);
    if (OB_CSV_PARSE_MORE_DATA == match) {
      return OB_CSV_PARSE_MORE_DATA;
    }
    if (1 == match) {
      // the quote opens an enclosed field
      pos_ += quote_len_;
      field_start_ = pos_;
      field_content_start_ = static_cast<int64_t>(out_buf_.size());
      state_ = OB_CSV_STATE_QUOTED;
      return OB_CSV_PARSE_CONTINUE;
    }
    const int delim_match = match_seq(pos_, delim_, delim_len_, eof);
    if (OB_CSV_PARSE_MORE_DATA == delim_match) {
      return OB_CSV_PARSE_MORE_DATA;
    }
    if (1 == delim_match) {
      // an empty unquoted field is a NULL
      add_field(static_cast<int64_t>(out_buf_.size()), 0, true, false);
      pos_ += delim_len_;
      field_start_ = pos_;
      return OB_CSV_PARSE_CONTINUE;
    }
    const int64_t term_len = get_terminator_len(eof);
    if (term_len < 0) {
      return OB_CSV_PARSE_MORE_DATA;
    }
    if (term_len > 0) {
      if (field_infos_.empty()) {
        // an empty line is skipped
        pos_ += term_len;
        in_record_ = false;
        state_ = OB_CSV_STATE_FIELD_START;
        field_start_ = pos_;
        return OB_CSV_PARSE_CONTINUE;
      }
      // the record ends with an empty field
      add_field(static_cast<int64_t>(out_buf_.size()), 0, true, false);
      pos_ += term_len;
      end_record();
      return OB_CSV_PARSE_RECORD;
    }
    // an unquoted field
    state_ = OB_CSV_STATE_UNQUOTED;
    field_start_ = pos_;
    return OB_CSV_PARSE_CONTINUE;
  }

  int parse_unquoted(const bool eof)
  {
    const int64_t size = static_cast<int64_t>(pending_.size());
    while (pos_ < size) {
      const int match = match_seq(pos_, delim_, delim_len_, eof);
      if (OB_CSV_PARSE_MORE_DATA == match) {
        return OB_CSV_PARSE_MORE_DATA;
      }
      if (1 == match) {
        add_unquoted_field();
        pos_ += delim_len_;
        state_ = OB_CSV_STATE_FIELD_START;
        field_start_ = pos_;
        return OB_CSV_PARSE_CONTINUE;
      }
      const char c = pending_[pos_];
      if ('\n' == c) {
        add_unquoted_field();
        ++pos_;
        end_record();
        return OB_CSV_PARSE_RECORD;
      }
      if ('\r' == c) {
        if (pos_ + 1 >= size) {
          if (!eof) {
            return OB_CSV_PARSE_MORE_DATA;
          }
          // a lone '\r' at the end of the file is a regular character
          ++pos_;
          continue;
        }
        if ('\n' == pending_[pos_ + 1]) {
          add_unquoted_field();
          pos_ += 2;
          end_record();
          return OB_CSV_PARSE_RECORD;
        }
        // a lone '\r' is a regular character
        ++pos_;
        continue;
      }
      ++pos_;
    }
    return OB_CSV_PARSE_MORE_DATA;
  }

  int parse_quoted(const bool eof)
  {
    const int64_t size = static_cast<int64_t>(pending_.size());
    while (pos_ < size) {
      if (escape_len_ > 0 && escape_ != quote_) {
        const int match = match_seq(pos_, escape_, escape_len_, eof);
        if (OB_CSV_PARSE_MORE_DATA == match) {
          return OB_CSV_PARSE_MORE_DATA;
        }
        if (1 == match) {
          if (pos_ + escape_len_ >= size) {
            if (!eof) {
              return OB_CSV_PARSE_MORE_DATA;
            }
            error_ = "the escape character is the last character of the file";
            return OB_CSV_PARSE_ERROR;
          }
          // the escaped character is taken as it is
          out_buf_.push_back(pending_[pos_ + escape_len_]);
          pos_ += escape_len_ + 1;
          continue;
        }
      }
      const int match = match_seq(pos_, quote_, quote_len_, eof);
      if (OB_CSV_PARSE_MORE_DATA == match) {
        return OB_CSV_PARSE_MORE_DATA;
      }
      if (1 == match) {
        if (pos_ + 2 * quote_len_ > size) {
          // it cannot be told whether the quote is doubled, more data is needed
          if (!eof) {
            return OB_CSV_PARSE_MORE_DATA;
          }
          // the quote closes the field
          pos_ += quote_len_;
          field_start_ = pos_;
          add_quoted_field();
          state_ = OB_CSV_STATE_AFTER_QUOTE;
          return OB_CSV_PARSE_CONTINUE;
        }
        if (0 == memcmp(&pending_[pos_ + quote_len_], quote_.data(), static_cast<size_t>(quote_len_))) {
          // a doubled quote is a literal quote
          out_buf_.insert(out_buf_.end(), quote_.begin(), quote_.begin() + quote_len_);
          pos_ += 2 * quote_len_;
          continue;
        }
        // the closing quote
        pos_ += quote_len_;
        field_start_ = pos_;
        add_quoted_field();
        state_ = OB_CSV_STATE_AFTER_QUOTE;
        return OB_CSV_PARSE_CONTINUE;
      }
      const char c = pending_[pos_];
      if ('\r' == c) {
        if (pos_ + 1 >= size) {
          if (!eof) {
            return OB_CSV_PARSE_MORE_DATA;
          }
          out_buf_.push_back('\r');
          ++pos_;
          continue;
        }
        if ('\n' == pending_[pos_ + 1]) {
          // the end of the line inside an enclosure is normalized
          out_buf_.push_back('\n');
          pos_ += 2;
          continue;
        }
        out_buf_.push_back('\r');
        ++pos_;
        continue;
      }
      out_buf_.push_back(c);
      ++pos_;
    }
    if (!eof) {
      return OB_CSV_PARSE_MORE_DATA;
    }
    error_ = "the enclosed field is not terminated";
    return OB_CSV_PARSE_ERROR;
  }

  int parse_after_quote(const bool eof)
  {
    const int64_t size = static_cast<int64_t>(pending_.size());
    if (pos_ >= size) {
      if (!eof) {
        return OB_CSV_PARSE_MORE_DATA;
      }
      end_record();
      return OB_CSV_PARSE_RECORD;
    }
    const int match = match_seq(pos_, delim_, delim_len_, eof);
    if (OB_CSV_PARSE_MORE_DATA == match) {
      return OB_CSV_PARSE_MORE_DATA;
    }
    if (1 == match) {
      pos_ += delim_len_;
      state_ = OB_CSV_STATE_FIELD_START;
      field_start_ = pos_;
      return OB_CSV_PARSE_CONTINUE;
    }
    const int64_t term_len = get_terminator_len(eof);
    if (term_len < 0) {
      return OB_CSV_PARSE_MORE_DATA;
    }
    if (term_len > 0) {
      pos_ += term_len;
      end_record();
      return OB_CSV_PARSE_RECORD;
    }
    error_ = "an unexpected character follows the closing quote of an enclosed field";
    return OB_CSV_PARSE_ERROR;
  }

  int finish_at_eof()
  {
    switch (state_) {
      case OB_CSV_STATE_FIELD_START: {
        if (field_infos_.empty()) {
          // there is no more record
          in_record_ = false;
          return OB_CSV_PARSE_MORE_DATA;
        }
        // the record ends with an empty field
        add_field(static_cast<int64_t>(out_buf_.size()), 0, true, false);
        end_record();
        return OB_CSV_PARSE_RECORD;
      }
      case OB_CSV_STATE_UNQUOTED: {
        add_unquoted_field();
        end_record();
        return OB_CSV_PARSE_RECORD;
      }
      case OB_CSV_STATE_QUOTED: {
        error_ = "the enclosed field is not terminated";
        return OB_CSV_PARSE_ERROR;
      }
      case OB_CSV_STATE_AFTER_QUOTE: {
        end_record();
        return OB_CSV_PARSE_RECORD;
      }
      default: {
        error_ = "the csv tokenizer is in an unknown state";
        return OB_CSV_PARSE_ERROR;
      }
    }
  }

  /// The length of the record terminator at the current position, 0 when there is
  /// none, -1 when the next byte is needed to know it.
  int64_t get_terminator_len(const bool eof) const
  {
    const int64_t size = static_cast<int64_t>(pending_.size());
    const char c = pending_[pos_];
    if ('\n' == c) {
      return 1;
    }
    if ('\r' == c) {
      if (pos_ + 1 < size) {
        return ('\n' == pending_[pos_ + 1]) ? 2 : 0;
      }
      return eof ? 0 : -1;
    }
    return 0;
  }

  /**
   * @brief Whether the sequence is at the current position.
   *
   * @return 1 when the sequence matches, 0 when it does not match,
   *         OB_CSV_PARSE_MORE_DATA when more data is needed
   */
  int match_seq(const int64_t pos, const std::string &seq, const int64_t seq_len, const bool eof) const
  {
    if (seq_len <= 0) {
      return 0;
    }
    const int64_t size = static_cast<int64_t>(pending_.size());
    const int64_t avail = size - pos;
    if (avail >= seq_len) {
      return (0 == memcmp(&pending_[pos], seq.data(), static_cast<size_t>(seq_len))) ? 1 : 0;
    }
    if (avail > 0 && 0 != memcmp(&pending_[pos], seq.data(), static_cast<size_t>(avail))) {
      // the sequence cannot match
      return 0;
    }
    return eof ? 0 : OB_CSV_PARSE_MORE_DATA;
  }

  void add_unquoted_field()
  {
    const int64_t len = pos_ - field_start_;
    const int64_t offset = static_cast<int64_t>(out_buf_.size());
    if (len > 0) {
      out_buf_.insert(out_buf_.end(), pending_.begin() + field_start_, pending_.begin() + pos_);
    }
    // an empty unquoted field is a NULL
    add_field(offset, len, len <= 0, false);
  }

  void add_quoted_field()
  {
    // an enclosed field is never a NULL, an empty enclosed field is an empty string
    add_field(field_content_start_, static_cast<int64_t>(out_buf_.size()) - field_content_start_, false, true);
  }

  void add_field(const int64_t offset, const int64_t len, const bool is_null, const bool is_quoted)
  {
    ObCsvFieldInfo info;
    info.offset_ = offset;
    info.len_ = len;
    info.is_null_ = is_null;
    info.is_quoted_ = is_quoted;
    field_infos_.push_back(info);
  }

  void end_record()
  {
    fields_.clear();
    fields_.reserve(field_infos_.size());
    for (size_t i = 0; i < field_infos_.size(); ++i) {
      const ObCsvFieldInfo &info = field_infos_[i];
      ObCsvField field;
      field.data_ = (info.len_ > 0) ? &out_buf_[static_cast<size_t>(info.offset_)] : "";
      field.len_ = info.len_;
      field.is_null_ = info.is_null_;
      field.is_quoted_ = info.is_quoted_;
      fields_.push_back(field);
    }
    in_record_ = false;
    state_ = OB_CSV_STATE_FIELD_START;
    field_start_ = pos_;
  }

  /// Drop the data of the fields that are already parsed.
  void compact()
  {
    if (field_start_ > 0) {
      pending_.erase(pending_.begin(), pending_.begin() + field_start_);
      pos_ -= field_start_;
      field_start_ = 0;
    }
  }

private:
  std::string delim_;
  int64_t delim_len_;
  std::string quote_;
  int64_t quote_len_;
  std::string escape_;
  int64_t escape_len_;
  std::vector<char> pending_;
  std::vector<char> out_buf_;
  std::vector<ObCsvFieldInfo> field_infos_;
  std::vector<ObCsvField> fields_;
  ObCsvParserState state_;
  int64_t pos_;
  int64_t field_start_;
  int64_t field_content_start_;
  bool in_record_;
  bool too_large_;
  std::string error_;
};

} // end namespace ob_csv_reader_core
// OB_CSV_READER_CORE_END

/**
 * @brief The core of the values of the newline delimited json files.
 *
 * The core only uses the standard library, it does not depend on the rest of the
 * observer, so that it can be tested in isolation.
 */
namespace ob_jsonl_reader_core
{

/// The kind of a json value.
enum ObJsonlValueKind
{
  OB_JSONL_VALUE_NULL = 0,
  OB_JSONL_VALUE_INT,
  OB_JSONL_VALUE_UINT,
  OB_JSONL_VALUE_DOUBLE,
  OB_JSONL_VALUE_BOOL,
  OB_JSONL_VALUE_STRING,
};

/// A json value of a flat object.
struct ObJsonlValue
{
  ObJsonlValueKind kind_;
  int64_t int_val_;
  uint64_t uint_val_;
  double dbl_val_;
  bool bool_val_;
  const char *str_;
  int64_t str_len_;
  ObJsonlValue()
    : kind_(OB_JSONL_VALUE_NULL), int_val_(0), uint_val_(0), dbl_val_(0.0), bool_val_(false),
      str_(NULL), str_len_(0)
  {}
};

/**
 * @brief The text of a json value.
 *
 * The strings are taken as they are, the other values are formatted into the buffer.
 *
 * @param value    the value
 * @param buf      the buffer used to format the value
 * @param buf_len  the size of the buffer
 * @param text_len the length of the text
 * @return the text of the value
 */
const char *format_value(const ObJsonlValue &value,
                          char *buf,
                          const int64_t buf_len,
                          int64_t &text_len)
{
  const char *ret = "";
  text_len = 0;
  switch (value.kind_) {
    case OB_JSONL_VALUE_NULL: {
      ret = "";
      text_len = 0;
      break;
    }
    case OB_JSONL_VALUE_INT: {
      text_len = snprintf(buf, static_cast<size_t>(buf_len), "%ld", static_cast<long>(value.int_val_));
      ret = buf;
      break;
    }
    case OB_JSONL_VALUE_UINT: {
      text_len = snprintf(buf, static_cast<size_t>(buf_len), "%lu",
                          static_cast<unsigned long>(value.uint_val_));
      ret = buf;
      break;
    }
    case OB_JSONL_VALUE_DOUBLE: {
      const double dbl = value.dbl_val_;
      text_len = snprintf(buf, static_cast<size_t>(buf_len), "%.15g", dbl);
      if (strtod(buf, NULL) != dbl) {
        // the value cannot be read back exactly, use a longer representation
        text_len = snprintf(buf, static_cast<size_t>(buf_len), "%.17g", dbl);
      }
      ret = buf;
      break;
    }
    case OB_JSONL_VALUE_BOOL: {
      ret = value.bool_val_ ? "true" : "false";
      text_len = value.bool_val_ ? 4 : 5;
      break;
    }
    case OB_JSONL_VALUE_STRING: {
      ret = value.str_;
      text_len = value.str_len_;
      break;
    }
    default: {
      break;
    }
  }
  return ret;
}

} // end namespace ob_jsonl_reader_core

namespace
{

/// The size of the buffer used to read the file.
const int64_t OB_FILE_SCAN_READER_BUF_SIZE = 64 * 1024;

/// Copy a text into the allocator, the copy is terminated by a '\0'.
int ob_file_scan_reader_copy_text(common::ObIAllocator &allocator,
                                   const char *data,
                                   const int64_t len,
                                   common::ObString &dst)
{
  int ret = OB_SUCCESS;
  char *buf = NULL;
  if (OB_ISNULL(data) || len <= 0) {
    dst.reset();
  } else if (OB_ISNULL(buf = static_cast<char *>(allocator.alloc(len + 1)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    MEMCPY(buf, data, len);
    buf[len] = '\0';
    dst.assign_ptr(buf, len);
  }
  return ret;
}

/// Make the name of a column that has no name, i.e. "column0".
int ob_file_scan_reader_make_generated_name(common::ObIAllocator &allocator,
                                            const int64_t index,
                                            common::ObString &name)
{
  int ret = OB_SUCCESS;
  char buf[32];
  const int len = snprintf(buf, sizeof(buf), "column%ld", static_cast<long>(index));
  if (len <= 0 || len >= static_cast<int>(sizeof(buf))) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ret = ob_file_scan_reader_copy_text(allocator, buf, len, name);
  }
  return ret;
}

/// Read a piece of the file into the buffer.
int ob_file_scan_reader_read_chunk(common::ObFileReader &file_reader,
                                   char *read_buf,
                                   const int64_t read_buf_size,
                                   int64_t &file_offset,
                                   int64_t &read_size)
{
  int ret = OB_SUCCESS;
  read_size = 0;
  if (OB_FAIL(file_reader.pread(read_buf, read_buf_size, file_offset, read_size))) {
    LOG_WARN("cannot read the file", K(ret), K(file_offset));
  } else {
    file_offset += read_size;
  }
  return ret;
}

/// Open the file of a file table.
int ob_file_scan_reader_open_file(common::ObFileReader &file_reader, const common::ObString &path)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(path.ptr()) || path.length() <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("the path of the file is empty", K(ret));
  } else if (OB_FAIL(ret = file_reader.open(path, false))) {
    LOG_WARN("cannot open the file", K(ret), K(path));
  }
  return ret;
}

/// Parse one line of a newline delimited json file, the line must hold a json object.
int ob_jsonl_reader_parse_line(common::ObIAllocator &allocator,
                               const char *data,
                               const int64_t len,
                               common::ObJsonNode *&tree)
{
  int ret = OB_SUCCESS;
  tree = NULL;
  common::ObString text;
  text.assign_ptr(data, len);
  if (OB_FAIL(ret = common::ObJsonParser::get_tree(&allocator, text, tree))) {
    LOG_WARN("cannot parse the json text", K(ret), K(len));
  } else if (OB_ISNULL(tree) || common::ObJsonNodeType::J_OBJECT != tree->json_type()) {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

/// Extract the value of a member of a json object.
int ob_jsonl_reader_get_value(common::ObJsonNode *node, ob_jsonl_reader_core::ObJsonlValue &value)
{
  int ret = OB_SUCCESS;
  switch (node->json_type()) {
    case common::ObJsonNodeType::J_NULL: {
      value.kind_ = ob_jsonl_reader_core::OB_JSONL_VALUE_NULL;
      break;
    }
    case common::ObJsonNodeType::J_INT: {
      value.kind_ = ob_jsonl_reader_core::OB_JSONL_VALUE_INT;
      value.int_val_ = node->get_int();
      break;
    }
    case common::ObJsonNodeType::J_UINT: {
      value.kind_ = ob_jsonl_reader_core::OB_JSONL_VALUE_UINT;
      value.uint_val_ = node->get_uint();
      break;
    }
    case common::ObJsonNodeType::J_DOUBLE: {
      value.kind_ = ob_jsonl_reader_core::OB_JSONL_VALUE_DOUBLE;
      value.dbl_val_ = node->get_double();
      break;
    }
    case common::ObJsonNodeType::J_BOOLEAN: {
      value.kind_ = ob_jsonl_reader_core::OB_JSONL_VALUE_BOOL;
      value.bool_val_ = node->get_boolean();
      break;
    }
    case common::ObJsonNodeType::J_STRING: {
      value.kind_ = ob_jsonl_reader_core::OB_JSONL_VALUE_STRING;
      value.str_ = node->get_data();
      value.str_len_ = static_cast<int64_t>(node->get_data_length());
      break;
    }
    default: {
      // the nested objects and the arrays are not supported
      ret = OB_NOT_SUPPORTED;
      break;
    }
  }
  return ret;
}

/// The type of the value of a json object.
common::ObObjType ob_jsonl_reader_value_type(const ob_jsonl_reader_core::ObJsonlValue &value)
{
  common::ObObjType type = common::ObNullType;
  switch (value.kind_) {
    case ob_jsonl_reader_core::OB_JSONL_VALUE_INT:
    case ob_jsonl_reader_core::OB_JSONL_VALUE_UINT: {
      type = common::ObIntType;
      break;
    }
    case ob_jsonl_reader_core::OB_JSONL_VALUE_DOUBLE: {
      type = common::ObDoubleType;
      break;
    }
    case ob_jsonl_reader_core::OB_JSONL_VALUE_BOOL: {
      type = common::ObTinyIntType;
      break;
    }
    case ob_jsonl_reader_core::OB_JSONL_VALUE_STRING: {
      bool is_null = false;
      type = ObFileScanUtils::infer_scalar_type(value.str_, value.str_len_, is_null);
      break;
    }
    default: {
      type = common::ObNullType;
      break;
    }
  }
  return type;
}

} // end anonymous namespace

ObCsvFileScanReader::ObCsvFileScanReader(const ObFileScanOptions &options,
                                         const common::ObIArray<ObFileColumnDef> &columns)
  : options_(),
    columns_(),
    perm_alloc_(),
    row_alloc_(),
    file_reader_(),
    tokenizer_(NULL),
    read_buf_(NULL),
    read_buf_size_(0),
    file_offset_(0),
    record_no_(0),
    is_opened_(false),
    is_header_read_(false),
    init_ret_(OB_SUCCESS)
{
  init_ret_ = init_options_(options);
  if (OB_SUCCESS == init_ret_) {
    init_ret_ = init_columns_(columns);
  }
}

ObCsvFileScanReader::~ObCsvFileScanReader()
{
  close();
}

int ObCsvFileScanReader::init_options_(const ObFileScanOptions &options)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ret = ob_file_scan_reader_copy_text(perm_alloc_, options.path_.ptr(), options.path_.length(),
                                                   options_.path_))) {
  } else if (OB_FAIL(ret = ob_file_scan_reader_copy_text(perm_alloc_, options.delimiter_.ptr(),
                                                         options.delimiter_.length(), options_.delimiter_))) {
  } else if (OB_FAIL(ret = ob_file_scan_reader_copy_text(perm_alloc_, options.quote_.ptr(),
                                                         options.quote_.length(), options_.quote_))) {
  } else if (OB_FAIL(ret = ob_file_scan_reader_copy_text(perm_alloc_, options.escape_.ptr(),
                                                         options.escape_.length(), options_.escape_))) {
  } else if (OB_FAIL(ret = ob_file_scan_reader_copy_text(perm_alloc_, options.null_str_.ptr(),
                                                         options.null_str_.length(), options_.null_str_))) {
  } else {
    options_.format_ = options.format_;
    options_.header_ = options.header_;
    options_.sample_size_ = options.sample_size_;
    options_.json_format_ = options.json_format_;
    options_.union_by_name_ = options.union_by_name_;
    options_.file_size_ = options.file_size_;
    options_.file_mtime_ = options.file_mtime_;
  }
  return ret;
}

int ObCsvFileScanReader::init_columns_(const common::ObIArray<ObFileColumnDef> &columns)
{
  int ret = OB_SUCCESS;
  columns_.reset();
  if (columns.count() > 0) {
    ret = columns_.prepare_allocate(columns.count());
    for (int64_t i = 0; OB_SUCC(ret) && i < columns.count(); ++i) {
      columns_.at(i) = columns.at(i);
      ret = ob_file_scan_reader_copy_text(perm_alloc_, columns.at(i).name_.ptr(), columns.at(i).name_.length(),
                                          columns_.at(i).name_);
    }
  }
  return ret;
}

int ObCsvFileScanReader::open()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(init_ret_)) {
    ret = init_ret_;
  } else if (is_opened_) {
    ret = OB_SUCCESS;
  } else {
    void *tokenizer_buf = perm_alloc_.alloc(sizeof(ob_csv_reader_core::ObCsvReaderTokenizer));
    read_buf_ = static_cast<char *>(perm_alloc_.alloc(OB_FILE_SCAN_READER_BUF_SIZE));
    if (OB_ISNULL(tokenizer_buf) || OB_ISNULL(read_buf_)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      tokenizer_ = new (tokenizer_buf) ob_csv_reader_core::ObCsvReaderTokenizer();
      tokenizer_->set_delimiter(options_.delimiter_.ptr(), options_.delimiter_.length());
      tokenizer_->set_quote(options_.quote_.ptr(), options_.quote_.length());
      tokenizer_->set_escape(options_.escape_.ptr(), options_.escape_.length());
      read_buf_size_ = OB_FILE_SCAN_READER_BUF_SIZE;
      file_offset_ = 0;
      record_no_ = 0;
      is_header_read_ = false;
      row_values_.reset();
      if (OB_FAIL(ret = row_values_.prepare_allocate(columns_.count()))) {
      } else if (OB_FAIL(ret = ob_file_scan_reader_open_file(file_reader_, options_.path_))) {
      } else {
        is_opened_ = true;
      }
    }
  }
  return ret;
}

int ObCsvFileScanReader::close()
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(tokenizer_)) {
    tokenizer_->~ObCsvReaderTokenizer();
    tokenizer_ = NULL;
  }
  if (file_reader_.is_opened()) {
    file_reader_.close();
  }
  is_opened_ = false;
  is_header_read_ = false;
  read_buf_ = NULL;
  read_buf_size_ = 0;
  file_offset_ = 0;
  record_no_ = 0;
  row_values_.reset();
  return ret;
}

int ObCsvFileScanReader::next_row()
{
  int ret = OB_SUCCESS;
  if (!is_opened_) {
    ret = OB_NOT_INIT;
    LOG_WARN("the csv reader is not open", K(ret));
  } else {
    row_alloc_.reuse();
    if (OB_FAIL(ret = next_data_record_())) {
    } else {
      ret = build_row_values_();
    }
  }
  return ret;
}

int ObCsvFileScanReader::next_record_()
{
  int ret = OB_SUCCESS;
  bool eof = false;
  for (;;) {
    const int parse_ret = tokenizer_->parse_next_record(eof);
    if (ob_csv_reader_core::OB_CSV_PARSE_RECORD == parse_ret) {
      ret = OB_SUCCESS;
      break;
    } else if (ob_csv_reader_core::OB_CSV_PARSE_ERROR == parse_ret) {
      char msg[256];
      snprintf(msg, sizeof(msg), "the csv file is malformed: %s", tokenizer_->get_error().c_str());
      LOG_USER_ERROR(OB_ERR_UNEXPECTED, msg);
      ret = OB_ERR_UNEXPECTED;
      break;
    } else if (eof) {
      ret = OB_ITER_END;
      break;
    } else {
      int64_t read_size = 0;
      if (OB_FAIL(ret = ob_file_scan_reader_read_chunk(file_reader_, read_buf_, read_buf_size_, file_offset_,
                                                        read_size))) {
        break;
      }
      if (read_size <= 0) {
        eof = true;
      } else {
        tokenizer_->feed(read_buf_, read_size);
      }
    }
  }
  return ret;
}

int ObCsvFileScanReader::next_data_record_()
{
  int ret = OB_SUCCESS;
  for (;;) {
    if (OB_FAIL(ret = next_record_())) {
      break;
    }
    if (!is_header_read_ && options_.header_) {
      // the first record holds the names of the columns
      is_header_read_ = true;
      const int64_t field_cnt = static_cast<int64_t>(tokenizer_->get_fields().size());
      if (field_cnt != columns_.count()) {
        char msg[256];
        snprintf(msg, sizeof(msg), "the header of the csv file has %ld fields, the expected number of columns is %ld",
                 static_cast<long>(field_cnt), static_cast<long>(columns_.count()));
        LOG_USER_ERROR(OB_ERR_UNEXPECTED, msg);
        ret = OB_ERR_UNEXPECTED;
        break;
      }
      continue;
    }
    ++record_no_;
    break;
  }
  return ret;
}

int ObCsvFileScanReader::build_row_values_()
{
  int ret = OB_SUCCESS;
  const std::vector<ob_csv_reader_core::ObCsvField> &fields = tokenizer_->get_fields();
  if (static_cast<int64_t>(fields.size()) != columns_.count()) {
    char msg[256];
    snprintf(msg, sizeof(msg), "the record %ld of the csv file has %ld fields, the expected number of fields is %ld",
             static_cast<long>(record_no_), static_cast<long>(fields.size()),
             static_cast<long>(columns_.count()));
    LOG_USER_ERROR(OB_ERR_UNEXPECTED, msg);
    ret = OB_ERR_UNEXPECTED;
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < columns_.count(); ++i) {
      const ob_csv_reader_core::ObCsvField &field = fields[i];
      common::ObObj &obj = row_values_.at(i);
      const bool is_null_str = (options_.null_str_.length() > 0 && field.len_ == options_.null_str_.length()
                                && 0 == memcmp(field.data_, options_.null_str_.ptr(),
                                              static_cast<size_t>(field.len_)));
      if (field.is_null_ || is_null_str) {
        obj.set_null();
      } else {
        char *text = static_cast<char *>(row_alloc_.alloc(field.len_ > 0 ? field.len_ : 1));
        if (OB_ISNULL(text)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else {
          if (field.len_ > 0) {
            MEMCPY(text, field.data_, field.len_);
          }
          ret = ObFileScanUtils::set_obj_from_text(obj, columns_.at(i).type_, text, field.len_);
          if (OB_FAIL(ret)) {
            char msg[256];
            snprintf(msg, sizeof(msg), "cannot convert the value of the column %s of the record %ld of the csv file",
                     columns_.at(i).name_.ptr(), static_cast<long>(record_no_));
            LOG_USER_ERROR(OB_ERR_UNEXPECTED, msg);
            ret = OB_ERR_UNEXPECTED;
          }
        }
      }
    }
  }
  return ret;
}

int ObCsvFileScanReader::infer_schema(common::ObIAllocator &allocator,
                                       common::ObIArray<ObFileColumnDef> &columns)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(init_ret_)) {
    ret = init_ret_;
  } else {
    ret = infer_csv_schema_(allocator, columns);
  }
  return ret;
}

int ObCsvFileScanReader::infer_csv_schema_(common::ObIAllocator &allocator,
                                           common::ObIArray<ObFileColumnDef> &columns)
{
  int ret = OB_SUCCESS;
  ob_csv_reader_core::ObCsvReaderTokenizer tokenizer;
  common::ObArray<ObFileColumnDef, common::ModulePageAllocator, true> inferred;
  char *read_buf = NULL;
  int64_t file_offset = 0;
  int64_t expected_cnt = -1;
  int64_t record_no = 0;
  bool eof = false;
  bool header_done = false;

  tokenizer.set_delimiter(options_.delimiter_.ptr(), options_.delimiter_.length());
  tokenizer.set_quote(options_.quote_.ptr(), options_.quote_.length());
  tokenizer.set_escape(options_.escape_.ptr(), options_.escape_.length());
  if (OB_ISNULL(read_buf = static_cast<char *>(perm_alloc_.alloc(OB_FILE_SCAN_READER_BUF_SIZE)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(ret = ob_file_scan_reader_open_file(file_reader_, options_.path_))) {
  } else {
    for (;;) {
      const int parse_ret = tokenizer.parse_next_record(eof);
      if (ob_csv_reader_core::OB_CSV_PARSE_RECORD == parse_ret) {
        const std::vector<ob_csv_reader_core::ObCsvField> &fields = tokenizer.get_fields();
        if (!header_done && options_.header_) {
          // the first record holds the names of the columns
          header_done = true;
          expected_cnt = static_cast<int64_t>(fields.size());
          if (expected_cnt > OB_FILE_TABLE_MAX_COLUMN_CNT) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "the csv file has %ld columns, the greatest number of the columns of a file is %ld",
                     static_cast<long>(expected_cnt),
                     static_cast<long>(OB_FILE_TABLE_MAX_COLUMN_CNT));
            LOG_USER_ERROR(OB_NOT_SUPPORTED, msg);
            ret = OB_NOT_SUPPORTED;
            break;
          }
          for (int64_t i = 0; OB_SUCC(ret) && i < expected_cnt; ++i) {
            ObFileColumnDef column;
            column.type_ = common::ObNullType;
            column.nullable_ = false;
            if (fields[i].len_ > 0) {
              ret = ob_file_scan_reader_copy_text(allocator, fields[i].data_, fields[i].len_, column.name_);
            } else {
              ret = ob_file_scan_reader_make_generated_name(allocator, i, column.name_);
            }
            if (OB_SUCC(ret)) {
              ret = inferred.push_back(column);
            }
          }
        } else {
          ++record_no;
          if (expected_cnt < 0) {
            // the file has no header, the names are generated and the number of
            // the columns is the number of the fields of the first record
            expected_cnt = static_cast<int64_t>(fields.size());
            for (int64_t i = 0; OB_SUCC(ret) && i < expected_cnt; ++i) {
              ObFileColumnDef column;
              column.type_ = common::ObNullType;
              column.nullable_ = false;
              ret = ob_file_scan_reader_make_generated_name(allocator, i, column.name_);
              if (OB_SUCC(ret)) {
                ret = inferred.push_back(column);
              }
            }
          }
          if (OB_SUCC(ret) && static_cast<int64_t>(fields.size()) != expected_cnt) {
            char msg[256];
            snprintf(msg, sizeof(msg), "the record %ld of the csv file has %ld fields, the expected number of fields is %ld",
                     static_cast<long>(record_no), static_cast<long>(fields.size()),
                     static_cast<long>(expected_cnt));
            LOG_USER_ERROR(OB_ERR_UNEXPECTED, msg);
            ret = OB_ERR_UNEXPECTED;
          }
          for (int64_t i = 0; OB_SUCC(ret) && i < expected_cnt && i < static_cast<int64_t>(fields.size()); ++i) {
            const ob_csv_reader_core::ObCsvField &field = fields[i];
            ObFileColumnDef &column = inferred.at(i);
            if (field.is_null_) {
              // an empty unquoted field is a NULL
              column.nullable_ = true;
            } else {
              bool is_null = false;
              const common::ObObjType type = ObFileScanUtils::infer_scalar_type(field.data_, field.len_, is_null);
              if (is_null) {
                column.nullable_ = true;
              } else {
                column.type_ = ObFileScanUtils::merge_type(column.type_, type);
                if (field.len_ > column.max_len_) {
                  // the length of the column is the length of its longest value
                  column.max_len_ = field.len_;
                }
                if (common::ObDateTimeType == type) {
                  // the scale of a datetime column is the greatest number of
                  // the fractional digits of its values
                  const int64_t scale = ObFileScanUtils::datetime_scale(field.data_, field.len_);
                  if (scale > column.scale_) {
                    column.scale_ = scale;
                  }
                }
              }
            }
          }
        }
      } else if (ob_csv_reader_core::OB_CSV_PARSE_ERROR == parse_ret) {
        char msg[256];
        snprintf(msg, sizeof(msg), "the csv file is malformed: %s", tokenizer.get_error().c_str());
        LOG_USER_ERROR(OB_ERR_UNEXPECTED, msg);
        ret = OB_ERR_UNEXPECTED;
        break;
      } else if (eof) {
        break;
      } else {
        int64_t read_size = 0;
        if (OB_FAIL(ret = ob_file_scan_reader_read_chunk(file_reader_, read_buf, OB_FILE_SCAN_READER_BUF_SIZE,
                                                          file_offset, read_size))) {
          break;
        }
        if (read_size <= 0) {
          eof = true;
        } else {
          tokenizer.feed(read_buf, read_size);
        }
      }
    }
    if (file_reader_.is_opened()) {
      file_reader_.close();
    }
  }
  if (OB_SUCC(ret)) {
    for (int64_t i = 0; OB_SUCC(ret) && i < inferred.count(); ++i) {
      ret = columns.push_back(inferred.at(i));
    }
    if (OB_SUCC(ret) && columns.count() > 0) {
      // the duplicated names are suffixed, the way DuckDB names them
      ret = ObFileScanUtils::dedup_column_names(allocator, columns);
    }
    if (OB_SUCC(ret)) {
      // the number of the data records is known, the optimizer uses it as the
      // estimated cardinality of the file table
      row_cnt_ = record_no;
    }
  }
  return ret;
}

ObJsonlFileScanReader::ObJsonlFileScanReader(const ObFileScanOptions &options,
                                             const common::ObIArray<ObFileColumnDef> &columns)
  : options_(),
    columns_(),
    perm_alloc_(),
    row_alloc_(),
    file_reader_(),
    line_buf_(),
    read_buf_(NULL),
    read_buf_size_(0),
    file_offset_(0),
    line_no_(0),
    line_len_(0),
    line_consumed_(0),
    line_scan_pos_(0),
    is_opened_(false),
    init_ret_(OB_SUCCESS)
{
  init_ret_ = init_options_(options);
  if (OB_SUCCESS == init_ret_) {
    init_ret_ = init_columns_(columns);
  }
}

ObJsonlFileScanReader::~ObJsonlFileScanReader()
{
  close();
}

int ObJsonlFileScanReader::init_options_(const ObFileScanOptions &options)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ret = ob_file_scan_reader_copy_text(perm_alloc_, options.path_.ptr(), options.path_.length(),
                                                   options_.path_))) {
  } else if (OB_FAIL(ret = ob_file_scan_reader_copy_text(perm_alloc_, options.null_str_.ptr(),
                                                         options.null_str_.length(), options_.null_str_))) {
  } else {
    options_.format_ = options.format_;
    options_.header_ = options.header_;
    options_.sample_size_ = options.sample_size_;
    options_.delimiter_ = options.delimiter_;
    options_.quote_ = options.quote_;
    options_.escape_ = options.escape_;
    options_.json_format_ = options.json_format_;
    options_.union_by_name_ = options.union_by_name_;
    options_.file_size_ = options.file_size_;
    options_.file_mtime_ = options.file_mtime_;
  }
  return ret;
}

int ObJsonlFileScanReader::init_columns_(const common::ObIArray<ObFileColumnDef> &columns)
{
  int ret = OB_SUCCESS;
  columns_.reset();
  if (columns.count() > 0) {
    ret = columns_.prepare_allocate(columns.count());
    for (int64_t i = 0; OB_SUCC(ret) && i < columns.count(); ++i) {
      columns_.at(i) = columns.at(i);
      ret = ob_file_scan_reader_copy_text(perm_alloc_, columns.at(i).name_.ptr(), columns.at(i).name_.length(),
                                          columns_.at(i).name_);
    }
  }
  return ret;
}

int ObJsonlFileScanReader::open()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(init_ret_)) {
    ret = init_ret_;
  } else if (is_opened_) {
    ret = OB_SUCCESS;
  } else {
    read_buf_ = static_cast<char *>(perm_alloc_.alloc(OB_FILE_SCAN_READER_BUF_SIZE));
    if (OB_ISNULL(read_buf_)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      read_buf_size_ = OB_FILE_SCAN_READER_BUF_SIZE;
      file_offset_ = 0;
      line_no_ = 0;
      line_len_ = 0;
      line_consumed_ = 0;
      line_scan_pos_ = 0;
      line_buf_.clear();
      row_values_.reset();
      if (OB_FAIL(ret = row_values_.prepare_allocate(columns_.count()))) {
      } else if (OB_FAIL(ret = ob_file_scan_reader_open_file(file_reader_, options_.path_))) {
      } else {
        is_opened_ = true;
      }
    }
  }
  return ret;
}

int ObJsonlFileScanReader::close()
{
  int ret = OB_SUCCESS;
  if (file_reader_.is_opened()) {
    file_reader_.close();
  }
  is_opened_ = false;
  read_buf_ = NULL;
  read_buf_size_ = 0;
  file_offset_ = 0;
  line_no_ = 0;
  line_len_ = 0;
  line_consumed_ = 0;
  line_scan_pos_ = 0;
  line_buf_.clear();
  row_values_.reset();
  return ret;
}

int ObJsonlFileScanReader::next_row()
{
  int ret = OB_SUCCESS;
  if (!is_opened_) {
    ret = OB_NOT_INIT;
    LOG_WARN("the jsonl reader is not open", K(ret));
  } else {
    row_alloc_.reuse();
    if (OB_FAIL(ret = next_line_())) {
    } else {
      ret = build_row_values_();
    }
  }
  return ret;
}

int ObJsonlFileScanReader::next_line_()
{
  int ret = OB_SUCCESS;
  bool eof = false;
  for (;;) {
    const int find_ret = find_line_(eof);
    if (find_ret < 0) {
      ret = OB_ERR_UNEXPECTED;
      break;
    } else if (1 == find_ret) {
      if (is_blank_line_()) {
        // the blank lines are skipped
        consume_line_();
        continue;
      }
      ret = OB_SUCCESS;
      break;
    } else if (eof) {
      ret = OB_ITER_END;
      break;
    } else {
      int64_t read_size = 0;
      if (OB_FAIL(ret = ob_file_scan_reader_read_chunk(file_reader_, read_buf_, read_buf_size_, file_offset_,
                                                        read_size))) {
        break;
      }
      if (read_size <= 0) {
        eof = true;
      } else {
        line_buf_.insert(line_buf_.end(), read_buf_, read_buf_ + read_size);
        if (static_cast<int64_t>(line_buf_.size()) > OB_FILE_TABLE_MAX_RECORD_SIZE) {
          // the line is too large, the memory that one record may use is bounded
          char msg[256];
          snprintf(msg, sizeof(msg),
                   "the line %ld of the json file is larger than %ld bytes",
                   static_cast<long>(line_no_ + 1),
                   static_cast<long>(OB_FILE_TABLE_MAX_RECORD_SIZE));
          LOG_USER_ERROR(OB_NOT_SUPPORTED, msg);
          ret = OB_NOT_SUPPORTED;
          break;
        }
      }
    }
  }
  return ret;
}

int ObJsonlFileScanReader::find_line_(const bool eof)
{
  if (line_consumed_ > 0) {
    // the line that was read before is dropped
    consume_line_();
  }
  const int64_t size = static_cast<int64_t>(line_buf_.size());
  while (line_scan_pos_ < size) {
    if ('\n' == line_buf_[line_scan_pos_]) {
      line_consumed_ = line_scan_pos_ + 1;
      line_len_ = line_scan_pos_;
      ++line_no_;
      // the '\r' of a CRLF line is not a part of the line
      if (line_len_ > 0 && '\r' == line_buf_[static_cast<size_t>(line_len_ - 1)]) {
        --line_len_;
      }
      return 1;
    }
    ++line_scan_pos_;
  }
  if (eof) {
    if (line_buf_.empty()) {
      return 0;
    }
    line_consumed_ = size;
    line_len_ = size;
    ++line_no_;
    if (line_len_ > 0 && '\r' == line_buf_[static_cast<size_t>(line_len_ - 1)]) {
      --line_len_;
    }
    return 1;
  }
  return 0;
}

void ObJsonlFileScanReader::consume_line_()
{
  if (line_consumed_ > 0 && line_consumed_ <= static_cast<int64_t>(line_buf_.size())) {
    line_buf_.erase(line_buf_.begin(), line_buf_.begin() + line_consumed_);
  } else {
    line_buf_.clear();
  }
  line_consumed_ = 0;
  line_scan_pos_ = 0;
}

bool ObJsonlFileScanReader::is_blank_line_() const
{
  for (int64_t i = 0; i < line_len_; ++i) {
    const char c = line_buf_[static_cast<size_t>(i)];
    if (' ' != c && '\t' != c && '\r' != c) {
      return false;
    }
  }
  return true;
}

int ObJsonlFileScanReader::build_row_values_()
{
  int ret = OB_SUCCESS;
  common::ObJsonNode *tree = NULL;
  if (OB_FAIL(ret = ob_jsonl_reader_parse_line(row_alloc_, line_buf_.data(), line_len_, tree))) {
    char msg[256];
    snprintf(msg, sizeof(msg), "the line %ld of the json file is not a valid json object",
             static_cast<long>(line_no_));
    LOG_USER_ERROR(OB_ERR_UNEXPECTED, msg);
    ret = OB_ERR_UNEXPECTED;
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < columns_.count(); ++i) {
      const ObFileColumnDef &column = columns_.at(i);
      common::ObObj &obj = row_values_.at(i);
      common::ObIJsonBase *value_node = NULL;
      // a record that does not hold the column has a NULL value, the lookup
      // failure is not an error of the query
      if (OB_SUCCESS != tree->get_object_value(column.name_, value_node)) {
        obj.set_null();
      } else {
        common::ObJsonNode *node = static_cast<common::ObJsonNode *>(value_node);
        if (common::ObJsonNodeType::J_OBJECT == node->json_type()
            || common::ObJsonNodeType::J_ARRAY == node->json_type()) {
          char msg[256];
          snprintf(msg, sizeof(msg),
                   "the line %ld of the json file holds a nested object or an array, "
                   "nested objects and arrays are not supported",
                   static_cast<long>(line_no_));
          LOG_USER_ERROR(OB_ERR_UNEXPECTED, msg);
          ret = OB_ERR_UNEXPECTED;
          break;
        }
        ob_jsonl_reader_core::ObJsonlValue value;
        if (OB_FAIL(ret = ob_jsonl_reader_get_value(node, value))) {
          char msg[256];
          snprintf(msg, sizeof(msg), "the value of the column %s of the line %ld of the json file is not supported",
                   column.name_.ptr(), static_cast<long>(line_no_));
          LOG_USER_ERROR(OB_ERR_UNEXPECTED, msg);
          ret = OB_ERR_UNEXPECTED;
          break;
        }
        if (ob_jsonl_reader_core::OB_JSONL_VALUE_NULL == value.kind_) {
          obj.set_null();
        } else {
          char buf[64];
          int64_t text_len = 0;
          const char *text = ob_jsonl_reader_core::format_value(value, buf, sizeof(buf), text_len);
          char *text_copy = static_cast<char *>(row_alloc_.alloc(text_len > 0 ? text_len : 1));
          if (OB_ISNULL(text_copy)) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            break;
          }
          if (text_len > 0) {
            MEMCPY(text_copy, text, text_len);
          }
          ret = ObFileScanUtils::set_obj_from_text(obj, column.type_, text_copy, text_len);
          if (OB_FAIL(ret)) {
            char msg[256];
            snprintf(msg, sizeof(msg), "cannot convert the value of the column %s of the line %ld of the json file",
                     column.name_.ptr(), static_cast<long>(line_no_));
            LOG_USER_ERROR(OB_ERR_UNEXPECTED, msg);
            ret = OB_ERR_UNEXPECTED;
            break;
          }
        }
      }
    }
  }
  return ret;
}

int ObJsonlFileScanReader::infer_schema(common::ObIAllocator &allocator,
                                         common::ObIArray<ObFileColumnDef> &columns)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(init_ret_)) {
    ret = init_ret_;
  } else {
    ret = infer_jsonl_schema_(allocator, columns);
  }
  return ret;
}

int ObJsonlFileScanReader::infer_jsonl_schema_(common::ObIAllocator &allocator,
                                               common::ObIArray<ObFileColumnDef> &columns)
{
  int ret = OB_SUCCESS;
  common::ObArray<ObFileColumnDef, common::ModulePageAllocator, true> inferred;
  int64_t record_cnt = 0;

  if (OB_ISNULL(read_buf_)) {
    read_buf_ = static_cast<char *>(perm_alloc_.alloc(OB_FILE_SCAN_READER_BUF_SIZE));
    if (OB_ISNULL(read_buf_)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      read_buf_size_ = OB_FILE_SCAN_READER_BUF_SIZE;
    }
  }
  if (OB_SUCC(ret)) {
    file_offset_ = 0;
    line_no_ = 0;
    line_len_ = 0;
    line_consumed_ = 0;
    line_scan_pos_ = 0;
    line_buf_.clear();
    if (OB_FAIL(ret = ob_file_scan_reader_open_file(file_reader_, options_.path_))) {
    } else {
      for (;;) {
        if (OB_FAIL(ret)) {
          // the error of the previous record stops the inference
          break;
        }
        const int line_ret = next_line_();
        if (OB_ITER_END == line_ret) {
          break;
        } else if (OB_FAIL(line_ret)) {
          ret = line_ret;
          break;
        }
        common::ObJsonNode *tree = NULL;
        row_alloc_.reuse();
        if (OB_FAIL(ret = ob_jsonl_reader_parse_line(row_alloc_, line_buf_.data(), line_len_, tree))) {
          char msg[256];
          snprintf(msg, sizeof(msg), "the line %ld of the json file is not a valid json object",
                   static_cast<long>(line_no_));
          LOG_USER_ERROR(OB_ERR_UNEXPECTED, msg);
          ret = OB_ERR_UNEXPECTED;
          break;
        }
        const uint64_t member_cnt = tree->element_count();
        ++record_cnt;
        for (uint64_t i = 0; OB_SUCC(ret) && i < member_cnt; ++i) {
          common::ObString key;
          common::ObIJsonBase *value_node = NULL;
          if (OB_FAIL(ret = tree->get_object_value(i, key, value_node))) {
            break;
          }
          common::ObJsonNode *node = static_cast<common::ObJsonNode *>(value_node);
          if (common::ObJsonNodeType::J_OBJECT == node->json_type()
              || common::ObJsonNodeType::J_ARRAY == node->json_type()) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "the line %ld of the json file holds a nested object or an array, "
                     "nested objects and arrays are not supported",
                     static_cast<long>(line_no_));
            LOG_USER_ERROR(OB_ERR_UNEXPECTED, msg);
            ret = OB_ERR_UNEXPECTED;
            break;
          }
          ob_jsonl_reader_core::ObJsonlValue value;
          if (OB_FAIL(ret = ob_jsonl_reader_get_value(node, value))) {
            break;
          }
          const common::ObObjType type = ob_jsonl_reader_value_type(value);
          // find the column of the key
          int64_t column_idx = -1;
          for (int64_t j = 0; j < inferred.count(); ++j) {
            if (0 == inferred.at(j).name_.case_compare(key)) {
              column_idx = j;
              break;
            }
          }
          if (column_idx < 0) {
            // a new column
            if (inferred.count() >= OB_FILE_TABLE_MAX_COLUMN_CNT) {
              char msg[256];
              snprintf(msg, sizeof(msg),
                       "the json file has more than %ld columns, "
                       "the greatest number of the columns of a file is %ld",
                       static_cast<long>(OB_FILE_TABLE_MAX_COLUMN_CNT),
                       static_cast<long>(OB_FILE_TABLE_MAX_COLUMN_CNT));
              LOG_USER_ERROR(OB_NOT_SUPPORTED, msg);
              ret = OB_NOT_SUPPORTED;
              break;
            }
            ObFileColumnDef column;
            column.type_ = type;
            column.nullable_ = (common::ObNullType == type);
            column.max_len_ = value.str_len_;
            if (common::ObDateTimeType == type) {
              column.scale_ = ObFileScanUtils::datetime_scale(value.str_, value.str_len_);
            }
            ret = ob_file_scan_reader_copy_text(allocator, key.ptr(), key.length(), column.name_);
            if (OB_SUCC(ret)) {
              ret = inferred.push_back(column);
            }
          } else {
            ObFileColumnDef &column = inferred.at(column_idx);
            if (common::ObNullType == type) {
              column.nullable_ = true;
            } else {
              column.type_ = ObFileScanUtils::merge_type(column.type_, type);
              if (value.str_len_ > column.max_len_) {
                // the length of the column is the length of its longest value
                column.max_len_ = value.str_len_;
              }
              if (common::ObDateTimeType == type) {
                // the scale of a datetime column is the greatest number of
                // the fractional digits of its values
                const int64_t scale = ObFileScanUtils::datetime_scale(value.str_, value.str_len_);
                if (scale > column.scale_) {
                  column.scale_ = scale;
                }
              }
            }
          }
        }
        // the columns that the record does not hold are nullable
        for (int64_t j = 0; OB_SUCC(ret) && j < inferred.count(); ++j) {
          common::ObIJsonBase *value_node = NULL;
          // a key that the record does not hold is not an error, the column is
          // nullable then
          if (OB_SUCCESS != tree->get_object_value(inferred.at(j).name_, value_node)) {
            inferred.at(j).nullable_ = true;
          }
        }
      }
      if (file_reader_.is_opened()) {
        file_reader_.close();
      }
    }
  }
  if (OB_SUCC(ret)) {
    for (int64_t i = 0; OB_SUCC(ret) && i < inferred.count(); ++i) {
      ret = columns.push_back(inferred.at(i));
    }
    if (OB_SUCC(ret)) {
      // the number of the records is known, the optimizer uses it as the
      // estimated cardinality of the file table
      row_cnt_ = record_cnt;
    }
  }
  return ret;
}

} // end namespace sql
} // end namespace oceanbase
