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

#ifndef SRC_SQL_ENGINE_BASIC_OB_EXTERNAL_FILE_WRITER_H_
#define SRC_SQL_ENGINE_BASIC_OB_EXTERNAL_FILE_WRITER_H_


#include "sql/engine/ob_operator.h"
#include "lib/file/ob_file.h"
#include "share/io/ob_backup_storage_info.h"
#include "sql/engine/cmd/ob_load_data_parser.h"
#include "ob_select_into_basic.h"
#include "sql/resolver/dml/ob_select_stmt.h"

namespace oceanbase
{
namespace sql
{
class ObExternalFileWriter
{
public:
  ObExternalFileWriter(const share::ObBackupStorageInfo &access_info,
                       const IntoFileLocation &file_location):
    write_bytes_(0),
    is_file_opened_(false),
    file_appender_(),
    storage_appender_(),
    split_file_id_(0),
    url_(),
    access_info_(access_info),
    file_location_(file_location)
  {}

  virtual ~ObExternalFileWriter() {
    file_appender_.~ObFileAppender();
    storage_appender_.reset();
  }

  int open_file();
  virtual int close_file();
  int close_data_writer();
  virtual int write_file() = 0;
  virtual int64_t get_file_size() = 0;
  int64_t get_write_bytes() { return write_bytes_; }
  void set_write_bytes(int64_t write_bytes) { write_bytes_ = write_bytes; }
protected:
  int64_t write_bytes_;
public:
  bool is_file_opened_;
  ObFileAppender file_appender_;
  ObStorageAppender storage_appender_;
  int64_t split_file_id_;
  ObString url_;
  const share::ObBackupStorageInfo &access_info_;
  const IntoFileLocation &file_location_;
};

class ObCsvFileWriter : public ObExternalFileWriter
{
public:
  ObCsvFileWriter(const share::ObBackupStorageInfo &access_info,
                  const IntoFileLocation &file_location,
                  bool &use_shared_buf,
                  const bool &has_compress,
                  const bool &has_lob,
                  int64_t &write_offset):
    ObExternalFileWriter(access_info, file_location),
    buf_(NULL),
    buf_len_(0),
    curr_pos_(0),
    last_line_pos_(0),
    curr_line_len_(0),
    compress_stream_writer_(NULL),
    use_shared_buf_(use_shared_buf),
    has_compress_(has_compress),
    has_lob_(has_lob),
    write_offset_(write_offset)
  {}

  virtual ~ObCsvFileWriter()
  {
    if (OB_NOT_NULL(compress_stream_writer_)) {
      compress_stream_writer_->~ObCompressStreamWriter();
      compress_stream_writer_ = NULL;
    }
  }
  int alloc_buf(common::ObIAllocator &allocator, int64_t buf_len);
  int init_compress_writer(common::ObIAllocator &allocator,
                           const ObCSVGeneralFormat::ObCSVCompression &compression_algorithm,
                           const int64_t &buffer_size);
  char *get_buf() { return buf_; }
  int64_t get_buf_len() { return buf_len_; }
  int64_t get_curr_pos() { return curr_pos_; }
  int64_t get_last_line_pos() { return last_line_pos_; }
  int64_t get_curr_line_len() { return curr_line_len_; }
  ObCompressStreamWriter *get_compress_stream_writer() { return compress_stream_writer_; }
  void set_curr_pos(int64_t curr_pos) { curr_pos_ = curr_pos; }
  void update_last_line_pos() { last_line_pos_ = curr_pos_; }
  void reset_curr_line_len() { curr_line_len_ = 0; }
  void increase_curr_line_len() { curr_line_len_ += (curr_pos_ - last_line_pos_); }
  int flush_buf();
  int flush_shared_buf(const char *shared_buf, bool continue_use_shared_buf = false);
  int flush_data(const char * data, int64_t data_len);
  int flush_to_compress_stream(const char *data, int64_t data_len);
  int flush_to_storage(const char *data, int64_t data_len);
  virtual int write_file() override;
  virtual int close_file() override;
  virtual int64_t get_file_size() override;
  int64_t get_curr_bytes_exclude_curr_line();
private:
  char *buf_;
  int64_t buf_len_;
  int64_t curr_pos_;
  int64_t last_line_pos_;
  int64_t curr_line_len_;
  ObCompressStreamWriter *compress_stream_writer_;
  bool &use_shared_buf_;
  const bool &has_compress_;
  const bool &has_lob_;
  int64_t &write_offset_;
};

}
}
#endif /* SRC_SQL_ENGINE_BASIC_OB_EXTERNAL_FILE_WRITER_H_ */
