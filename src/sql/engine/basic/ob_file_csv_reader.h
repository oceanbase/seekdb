/**
 * (c) 2025 OceanBase
 * This file contains a streaming CSV reader used by the file-as-database
 * table functions.  The reader never materializes the whole file: it reads
 * fixed size chunks and only keeps the record currently being parsed in
 * memory, so the memory footprint does not grow with the file size.
 */

#pragma once

#include "lib/allocator/ob_allocator.h"
#include "lib/container/ob_se_array.h"
#include "lib/string/ob_string.h"
#include "sql/engine/cmd/ob_load_data_parser.h"

namespace oceanbase
{
namespace sql
{

/// One field of a CSV record.  The bytes live in the reader's record buffer
/// and stay valid until the next call to next_record().
struct ObFileCsvField
{
  ObFileCsvField() : ptr_(NULL), len_(0), is_null_(false) {}

  const char *ptr_;
  int32_t len_;
  bool is_null_;
};

/**
 * Streaming CSV reader.
 *
 * Supported syntax:
 *   - header row (skipped or exposed through get_header())
 *   - LF and CRLF line terminators
 *   - quoted fields, doubled quote escapes ("" -> ") and backslash escapes
 *   - embedded newlines inside quoted fields
 *   - empty unquoted fields as NULL, custom null literal
 *   - blank line skipping and space trimming
 *
 * The reader is deliberately independent from ObCSVGeneralParser so that the
 * buffer boundary behavior and rescan semantics are fully controlled here.
 */
class ObFileCsvReader
{
public:
  static constexpr int64_t READ_BUF_SIZE = 1 << 20;   // 1MB io chunk
  static constexpr int64_t MAX_RECORD_SIZE = 64 << 20; // hard limit per record

  ObFileCsvReader();
  ~ObFileCsvReader();

  int open(const common::ObString &file_path,
           const ObCSVGeneralFormat &format,
           common::ObIAllocator &allocator);
  void close();

  /// Rewind to the first data record, keeping the parsed header.
  int reset();

  bool is_opened() const { return fd_ >= 0; }
  bool has_header() const { return parse_header_; }
  /// 1-based physical line number of the record returned by the last call
  int64_t get_record_no() const { return record_no_; }
  const common::ObIArray<common::ObString> &get_header() const { return header_; }

  /**
   * Read the next record.
   * @return OB_SUCCESS, OB_ITER_END at end of file, or an error code.
   */
  int next_record(common::ObIArray<ObFileCsvField> &fields);

  TO_STRING_KV(K_(fd), K_(record_no), K_(buf_pos), K_(buf_len), K_(eof));

private:
  int read_header();
  int refill();
  int next_byte(int64_t &ch);
  void unget_byte(const int64_t ch);
  int append_char(const char ch);
  int append_bytes(const char *ptr, const int64_t len);
  int finish_field(common::ObIArray<ObFileCsvField> &fields,
                   const int64_t field_start,
                   const bool quoted);
  int ensure_record_capacity(const int64_t needed);

private:
  int fd_;
  int64_t data_pos_;   // file offset of the first data byte
  char *buf_;
  int64_t buf_pos_;
  int64_t buf_len_;
  bool eof_;
  int64_t unget_;
  char *record_;
  int64_t record_len_;
  int64_t record_cap_;
  int64_t record_no_;

  char field_term_;
  char quote_;
  char escape_;
  bool has_quote_;
  bool has_escape_;
  bool parse_header_;
  int64_t skip_header_lines_;
  bool skip_blank_lines_;
  bool trim_space_;
  bool empty_field_as_null_;
  bool has_null_str_;
  common::ObString null_str_;
  ObCSVGeneralFormat::ObCSVCompression compression_;
  common::ObCharsetType cs_type_;

  common::ObSEArray<common::ObString, 16> header_;
  common::ObIAllocator *allocator_;

  DISALLOW_COPY_AND_ASSIGN(ObFileCsvReader);
};

} // namespace sql
} // namespace oceanbase
