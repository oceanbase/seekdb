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

#ifndef OCEANBASE_STORAGE_BLOCKSSTABLE_OB_OBJECT_RW_INFO_H_
#define OCEANBASE_STORAGE_BLOCKSSTABLE_OB_OBJECT_RW_INFO_H_

#include "lib/restore/ob_io_device.h"
#include "lib/oblog/ob_log_module.h"
#include "share/io/ob_io_define.h"
#include "storage/blocksstable/ob_macro_block_id.h"

namespace oceanbase
{
namespace blocksstable
{

struct ObStorageObjectWriteInfo final
{
public:
  ObStorageObjectWriteInfo()
    : buffer_(NULL), offset_(0), size_(0), io_timeout_ms_(DEFAULT_IO_WAIT_TIME_MS), io_desc_(),
      io_callback_(NULL), device_handle_(NULL), has_backup_device_handle_(false)
  {}
  ~ObStorageObjectWriteInfo() = default;
  OB_INLINE bool is_valid() const
  {
    bool bret = false;
    bret = io_desc_.is_valid() && NULL != buffer_ && offset_ >= 0 && size_ > 0
           && io_timeout_ms_ > 0;
    if (has_backup_device_handle_) {
      bret = bret && OB_NOT_NULL(device_handle_);
    } else {
      bret = bret && OB_ISNULL(device_handle_);
    }
    return bret;
  }
  TO_STRING_KV(KP_(buffer), K_(offset), K_(size), K_(io_timeout_ms), K_(io_desc), KP_(io_callback),
               KP_(device_handle), K_(has_backup_device_handle));
public:
  const char *buffer_;
  int64_t offset_;
  int64_t size_;
  int64_t io_timeout_ms_;
  common::ObIOFlag io_desc_;
  common::ObIOCallback *io_callback_;
  ObIODevice *device_handle_;
  bool has_backup_device_handle_;
};



struct ObStorageObjectReadInfo final
{
public:
  ObStorageObjectReadInfo()
    : macro_block_id_(), offset_(), size_(),
      io_timeout_ms_(DEFAULT_IO_WAIT_TIME_MS), io_desc_(), io_callback_(NULL), buf_(NULL)
  {}
  ~ObStorageObjectReadInfo() = default;
  OB_INLINE bool is_valid() const
  {
    return macro_block_id_.is_valid() && offset_ >= 0 && size_ > 0
           && io_desc_.is_valid() && (nullptr != io_callback_ || nullptr != buf_);
  }
  TO_STRING_KV(K_(macro_block_id), K_(offset), K_(size), K_(io_timeout_ms), K_(io_desc),
               KP_(io_callback), KP_(buf));
public:
  blocksstable::MacroBlockId macro_block_id_;
  int64_t offset_;
  int64_t size_;
  int64_t io_timeout_ms_;
  common::ObIOFlag io_desc_;
  common::ObIOCallback *io_callback_;
  char *buf_;
};

} // namespace blocksstable
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_BLOCKSSTABLE_OB_OBJECT_RW_INFO_H_
