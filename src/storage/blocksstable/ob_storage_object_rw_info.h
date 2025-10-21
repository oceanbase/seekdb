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

#include "common/storage/ob_io_device.h"
#include "lib/oblog/ob_log_module.h"
#include "share/io/ob_io_define.h"
#include "storage/blocksstable/ob_macro_block_id.h"
#include "storage/blocksstable/ob_logic_macro_id.h"

namespace oceanbase
{
namespace blocksstable
{

struct ObStorageObjectWriteInfo final
{
public:
  ObStorageObjectWriteInfo()
    : buffer_(NULL), offset_(0), size_(0), io_timeout_ms_(DEFAULT_IO_WAIT_TIME_MS), io_desc_(),
      io_callback_(NULL), device_handle_(NULL), has_backup_device_handle_(false),
      ls_epoch_id_(0), mtl_tenant_id_(OB_INVALID_TENANT_ID), tmp_file_valid_length_(0)
  {}
  ~ObStorageObjectWriteInfo() = default;
  OB_INLINE bool is_valid() const
  {
    bool bret = false;
    bret = io_desc_.is_valid() && NULL != buffer_ && offset_ >= 0 && size_ > 0
           && io_timeout_ms_ > 0 && ls_epoch_id_ >= 0 && is_valid_tenant_id(mtl_tenant_id_)
           && tmp_file_valid_length_ >= 0;
    if (has_backup_device_handle_) {
      bret = bret && OB_NOT_NULL(device_handle_);
    } else {
      bret = bret && OB_ISNULL(device_handle_);
    }
    return bret;
  }
  int fill_io_info_for_backup(const blocksstable::MacroBlockId &macro_id, ObIOInfo &io_info) const;
  TO_STRING_KV(KP_(buffer), K_(offset), K_(size), K_(io_timeout_ms), K_(io_desc), KP_(io_callback),
               KP_(device_handle), K_(has_backup_device_handle), K_(ls_epoch_id), K_(mtl_tenant_id),
               K_(tmp_file_valid_length));
public:
  const char *buffer_;
  int64_t offset_;
  int64_t size_;
  int64_t io_timeout_ms_;
  common::ObIOFlag io_desc_;
  common::ObIOCallback *io_callback_;
  ObIODevice *device_handle_;
  bool has_backup_device_handle_;
  int64_t ls_epoch_id_; // for share storage file path
  uint64_t mtl_tenant_id_;
  int64_t tmp_file_valid_length_; // for shared storage tmp file path
};



struct ObStorageObjectReadInfo final
{
public:
  ObStorageObjectReadInfo()
    : macro_block_id_(), offset_(), size_(), logic_micro_id_(), micro_crc_(0),
      io_timeout_ms_(DEFAULT_IO_WAIT_TIME_MS), io_desc_(), io_callback_(NULL), buf_(NULL),
      ls_epoch_id_(0), mtl_tenant_id_(OB_INVALID_TENANT_ID), bypass_micro_cache_(false),
      is_major_macro_preread_(false)
  {}
  ~ObStorageObjectReadInfo() = default;
  OB_INLINE bool is_valid() const
  {
    return macro_block_id_.is_valid() && offset_ >= 0 && size_ > 0
           && io_desc_.is_valid() && (nullptr != io_callback_ || nullptr != buf_)
           && ls_epoch_id_ >= 0
           && (macro_block_id_.is_id_mode_local() || is_valid_tenant_id(mtl_tenant_id_));
  }
  TO_STRING_KV(K_(macro_block_id), K_(offset), K_(size), K_(logic_micro_id), K_(micro_crc),
               K_(io_timeout_ms), K_(io_desc), KP_(io_callback), KP_(buf), K_(ls_epoch_id),
               K_(mtl_tenant_id), K_(bypass_micro_cache), K_(is_major_macro_preread));
public:
  blocksstable::MacroBlockId macro_block_id_;
  int64_t offset_;
  int64_t size_;
  // @logic_micro_id_ and @micro_crc_ are components of ObSSMicroBlockCacheKey, which are used
  // for interacting with disk cache of shared storage
  ObLogicMicroBlockId logic_micro_id_;
  int64_t micro_crc_;
  int64_t io_timeout_ms_;
  common::ObIOFlag io_desc_;
  common::ObIOCallback *io_callback_;
  char *buf_;
  int64_t ls_epoch_id_; // for share storage file path
  uint64_t mtl_tenant_id_;
  bool bypass_micro_cache_;
  bool is_major_macro_preread_; // for shared storage, if need to preread major macro to local cache from object storage.
};

} // namespace blocksstable
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_BLOCKSSTABLE_OB_OBJECT_RW_INFO_H_
