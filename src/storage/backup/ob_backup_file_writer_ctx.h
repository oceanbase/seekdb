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

#ifndef STORAGE_LOG_STREAM_BACKUP_FILE_WRITER_CTX_H_
#define STORAGE_LOG_STREAM_BACKUP_FILE_WRITER_CTX_H_
#include "common/storage/ob_io_device.h"
#include "storage/blocksstable/ob_data_buffer.h"
#include "lib/utility/utility.h"
namespace oceanbase {

namespace backup {

struct ObBackupFileWriteCtx {
public:
  ObBackupFileWriteCtx();
  virtual ~ObBackupFileWriteCtx();
  int open(const int64_t max_file_size, const common::ObIOFd &io_fd, common::ObIODevice &device_handle,
      common::ObInOutBandwidthThrottle &bandwidth_throttle);
  bool is_opened() const
  {
    return is_inited_;
  }
  int append_buffer(const blocksstable::ObBufferReader &buffer, const bool is_last_part = false);
  int64_t get_file_size() const
  {
    return file_size_;
  }
  int close();

private:
  int write_buffer_(const char *buf, const int64_t len, const bool is_last_part);
  bool check_can_flush_(const bool is_last_part) const;
  int flush_buffer_(const bool is_last_part);
  int commit_file_();

private:
  bool is_inited_;
  int64_t file_size_;
  int64_t max_file_size_;
  common::ObIOFd io_fd_;
  common::ObIODevice *dev_handle_;
  blocksstable::ObSelfBufferWriter data_buffer_;
  common::ObInOutBandwidthThrottle *bandwidth_throttle_;
  int64_t last_active_time_;
  DISALLOW_COPY_AND_ASSIGN(ObBackupFileWriteCtx);
};

}  // namespace backup
}  // namespace oceanbase

#endif
