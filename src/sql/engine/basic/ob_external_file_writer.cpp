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

#include "ob_external_file_writer.h"
#include "ob_select_into_basic.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

int ObExternalFileWriter::open_file()
{
  int ret = OB_SUCCESS;
  if (IntoFileLocation::SERVER_DISK != file_location_) {//OSS,COS,S3
    bool is_exist = false;
    ObBackupIoAdapter adapter;
    ObStorageAccessType access_type = OB_STORAGE_ACCESS_APPENDER;
    // for S3, should use OB_STORAGE_ACCESS_BUFFERED_MULTIPART_WRITER
    // OSS,COS can also use multipart writer. need to check performance
    if (IntoFileLocation::REMOTE_S3 == file_location_
        || IntoFileLocation::REMOTE_COS == file_location_) {
      access_type = OB_STORAGE_ACCESS_BUFFERED_MULTIPART_WRITER;
    }
    if (OB_FAIL(adapter.is_exist(url_, &access_info_, is_exist))) {
      LOG_WARN("fail to check file exist", KR(ret), K(url_), K(access_info_));
    } else if (is_exist) {
      ret = OB_FILE_ALREADY_EXIST;
      LOG_WARN("file already exist", KR(ret), K(url_), K(access_info_));
    } else if (OB_FAIL(storage_appender_.open(&access_info_, url_, access_type))) {
      LOG_WARN("fail to open file", KR(ret), K(url_), K(access_info_));
    } else {
      is_file_opened_ = true;
    }
  } else {//SERVER DISK
    if (OB_FAIL(file_appender_.create(url_, true))) {
      LOG_WARN("failed to create file", K(ret), K(url_));
    } else {
      is_file_opened_ = true;
    }
  }
  return ret;
}

int ObExternalFileWriter::close_file()
{
  int ret = OB_SUCCESS;
  if (IntoFileLocation::SERVER_DISK == file_location_) {
    if (file_appender_.is_opened() && OB_FAIL(file_appender_.fsync())) {
      LOG_WARN("failed to do fsync", K(ret));
    } else {
      file_appender_.close();
    }
  } else if (OB_FAIL(storage_appender_.close())) {
    LOG_WARN("fail to close storage appender", K(ret), K(url_), K(access_info_));
  }
  if (OB_SUCC(ret)) {
    is_file_opened_ = false;
  }
  return ret;
}

int ObExternalFileWriter::close_data_writer()
{
  int ret = OB_SUCCESS;
  OZ(write_file());
  OZ(close_file());
  return ret;
}

int ObCsvFileWriter::alloc_buf(common::ObIAllocator &allocator, int64_t buf_len)
{
  int ret = OB_SUCCESS;
  char *buf = NULL;
  if (OB_ISNULL(buf = static_cast<char*>(allocator.alloc(buf_len)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate buffer", K(ret), K(buf_len));
  } else {
    buf_ = buf;
    buf_len_ = buf_len;
  }
  return ret;
}

int ObCsvFileWriter::init_compress_writer(ObIAllocator &allocator,
                                          const ObCSVGeneralFormat::ObCSVCompression &compression_algorithm,
                                          const int64_t &buffer_size)
{
  int ret = OB_SUCCESS;
  void *ptr = NULL;
  if (OB_ISNULL(ptr = allocator.alloc(sizeof(ObCompressStreamWriter)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate stream writer", K(ret), K(sizeof(ObCompressStreamWriter)));
  } else {
    compress_stream_writer_ = new(ptr) ObCompressStreamWriter();
  }
  if (OB_SUCC(ret)
      && OB_FAIL(compress_stream_writer_->init(&file_appender_,
                                               &storage_appender_,
                                               file_location_,
                                               compression_algorithm,
                                               allocator,
                                               buffer_size))) {
    LOG_WARN("failed to init compress stream writer", K(ret));
  }
  return ret;
}

int ObCsvFileWriter::flush_buf()
{
  int ret = OB_SUCCESS;
  if (use_shared_buf_) {
    // do nothing
  } else if (last_line_pos_ > 0 && OB_NOT_NULL(buf_)) {
    if (OB_FAIL(flush_data(buf_, last_line_pos_))) {
      LOG_WARN("failed to flush data", K(ret));
    } else {
      MEMMOVE(buf_, buf_ + last_line_pos_, curr_pos_ - last_line_pos_);
      curr_pos_ = curr_pos_ - last_line_pos_;
      last_line_pos_ = 0;
    }
  }
  return ret;
}

int ObCsvFileWriter::flush_shared_buf(const char *shared_buf, bool continue_use_shared_buf) {
  int ret = common::OB_SUCCESS;
  if (get_curr_pos() > 0 && use_shared_buf_) {
    if (OB_FAIL(flush_data(shared_buf, get_curr_pos()))) {
    } else {
      if (has_lob_) {
        increase_curr_line_len();
      }
      set_curr_pos(0);
      update_last_line_pos();
      use_shared_buf_ = continue_use_shared_buf;
    }
  }
  return ret;
}

int ObCsvFileWriter::flush_data(const char * data, int64_t data_len)
{
  int ret = OB_SUCCESS;
  if (has_compress_) {
    if (OB_FAIL(flush_to_compress_stream(data, data_len))) {
      LOG_WARN("failed to flush to compress stream", K(ret));
    }
  } else {
    if (OB_FAIL(flush_to_storage(data, data_len))) {
      LOG_WARN("failed to flush to storage", K(ret));
    }
  }
  return ret;
}

int ObCsvFileWriter::flush_to_compress_stream(const char *data, int64_t data_len)
{
  int ret = OB_SUCCESS;
  if (data == NULL || data_len == 0) {
  } else if (!has_compress_ || OB_ISNULL(compress_stream_writer_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null compress stream writer", K(ret));
  } else if (!is_file_opened_ && OB_FAIL(open_file())) {
    LOG_WARN("failed to open file", K(ret), K(url_));
  } else if (OB_FAIL(compress_stream_writer_->write(data, data_len))) {
    LOG_WARN("failed to write to compress stream writer", K(ret), K(url_));
  }
  return ret;
}

int ObCsvFileWriter::flush_to_storage(const char *data, int64_t data_len)
{
  int ret = OB_SUCCESS;
  if (data == NULL || data_len == 0) {
  } else if (!is_file_opened_ && OB_FAIL(open_file())) {
    LOG_WARN("failed to open file", K(ret), K(url_));
  } else {
    if (file_location_ == IntoFileLocation::SERVER_DISK) {
      if (OB_FAIL(file_appender_.append(data, data_len, false))) {
        LOG_WARN("failed to append file", K(ret), K(data_len));
      }
    } else {
      int64_t write_size = 0;
      int64_t begin_ts = ObTimeUtility::current_time();
      if (OB_FAIL(storage_appender_.append(data, data_len, write_size))) {
        LOG_WARN("fail to append data", KR(ret), KP(data), K(data_len), K(url_), K(access_info_));
      } else {
        write_offset_ += write_size;
        int64_t end_ts = ObTimeUtility::current_time();
        int64_t cost_time = end_ts - begin_ts;
        long double speed = (cost_time <= 0) ? 0 :
                        (long double) write_size * 1000.0 * 1000.0 / 1024.0 / 1024.0 / cost_time;
        long double total_write = (long double) write_offset_ / 1024.0 / 1024.0;
        _OB_LOG(TRACE, "write oss stat, time:%ld write_size:%ld speed:%.2Lf MB/s total_write:%.2Lf MB",
                cost_time, write_size, speed, total_write);
      }
    }
  }
  return ret;
}

int ObCsvFileWriter::write_file()
{
  return flush_buf();
}

int ObCsvFileWriter::close_file()
{
  int ret = OB_SUCCESS;
  if (has_compress_ && OB_NOT_NULL(compress_stream_writer_) && is_file_opened_
      && OB_FAIL(compress_stream_writer_->finish_file_compress())) {
    LOG_WARN("failed to flush compress buffer", K(ret));
  } else if (OB_FAIL(ObExternalFileWriter::close_file())) {
    LOG_WARN("failed to close file", K(ret));
  }
  return ret;
}

int64_t ObCsvFileWriter::get_file_size()
{
  int64_t curr_line_len = 0;
  if (!has_lob_ || get_curr_line_len() == 0) {
    curr_line_len = get_curr_pos() - get_last_line_pos();
  } else {
    curr_line_len = get_curr_pos() + get_curr_line_len();
  }
  return get_curr_bytes_exclude_curr_line() + curr_line_len;
}

int64_t ObCsvFileWriter::get_curr_bytes_exclude_curr_line()
{
  int64_t curr_bytes_exclude_curr_line = 0;
  if (has_compress_) {
    if (compress_stream_writer_ == NULL) {
      // do nothing
    } else {
      // If export to compressed file, curr_bytes is estimated.
      // The compression algorithm has internal buffer,
      // so in order to avoid the estimated compressed bytes exceeding MAX_OSS_FILE_SIZE,
      // the size of the internal buffer needs to be taken into account.
      // zstd: 128 KB, gzip: 64KB. use the maximum buffer size here.
      const int64_t COMPRESSION_INTERNAL_BUFFER_SIZE = 128 * 1024; // 128KB
      curr_bytes_exclude_curr_line = get_compress_stream_writer()->get_write_bytes();
    }
  } else {
    curr_bytes_exclude_curr_line = get_write_bytes();
  }
  return curr_bytes_exclude_curr_line;
}


}
}
