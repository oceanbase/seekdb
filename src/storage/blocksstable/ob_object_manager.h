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

#ifndef OCEANBASE_BLOCKSSTABLE_OB_OBJECT_MANAGER_H_
#define OCEANBASE_BLOCKSSTABLE_OB_OBJECT_MANAGER_H_

#include "storage/blocksstable/ob_storage_object_rw_info.h"
#include "storage/blocksstable/ob_storage_object_handle.h"
#include "storage/blocksstable/ob_super_block_buffer_holder.h"
#include "storage/ob_super_block_struct.h"

namespace oceanbase
{
namespace blocksstable
{
enum class ObStorageObjectType : uint8_t
{
  DATA_MACRO = 0,
  META_MACRO,
  MAX
};

// ================================= ObStorageObjectOpt ====================================//

class ObStorageObjectOpt final
{
public:
  ObStorageObjectOpt()
    : object_type_(ObStorageObjectType::DATA_MACRO) {}

  ~ObStorageObjectOpt() {}
  void set_data_macro_object_opt()
  {
    object_type_ = ObStorageObjectType::DATA_MACRO;
  }
  void set_meta_macro_object_opt()
  {
    object_type_ = ObStorageObjectType::META_MACRO;
  }

  int64_t to_string(char *buf, const int64_t buf_len) const;

public:
  ObStorageObjectType object_type_;
};

// ================================== ObObjectManager =====================================//
class ObObjectManager final
{
public:
  int init(const int64_t macro_object_size);
  int start(const int64_t reserved_size);
  void stop();
  void wait();
  void destroy();

  int alloc_object(
      const ObStorageObjectOpt &opt,
      ObStorageObjectHandle &object_handle);
  static int async_read_object(
      const ObStorageObjectReadInfo &read_info,
      ObStorageObjectHandle &object_handle);
  static int async_write_object(
      const ObStorageObjectOpt &opt,
      const ObStorageObjectWriteInfo &write_info,
      ObStorageObjectHandle &object_handle);
  static int read_object(
      const ObStorageObjectReadInfo &read_info,
      ObStorageObjectHandle &object_handle);
  static int write_object(
      const ObStorageObjectOpt &opt,
      const ObStorageObjectWriteInfo &write_info,
      ObStorageObjectHandle &object_handle);
  int get_object_size(
      const MacroBlockId &object_id,
      const int64_t ls_epoch,
      int64_t &object_size) const;
  int64_t get_macro_object_size() const { return macro_object_size_; }
  int inc_ref(const MacroBlockId &object_id) const;
  int dec_ref(const MacroBlockId &object_id) const;
  int resize_local_device(
      const int64_t expected_current_size,
      const int64_t new_device_size,
      const int64_t new_device_disk_percentage,
      const int64_t reserved_size);
  int check_disk_space_available();
  int update_super_block(const common::ObLogCursor &replay_start_point,
                         const blocksstable::MacroBlockId &runtime_meta_entry);

  static ObObjectManager &get_instance();

  const storage::ObServerSuperBlock &get_server_super_block() const
  {
    return super_block_;
  }

  void get_server_super_block_by_copy(storage::ObServerSuperBlock &other) const
  {
    SpinRLockGuard guard(lock_);
    other = super_block_;
  }

  int64_t get_macro_block_size() const
  {
    return super_block_.get_macro_block_size();
  }
  int64_t get_max_macro_block_count(int64_t reserved_size) const;
  int64_t get_used_macro_block_count() const;
  int64_t get_free_macro_block_count() const;
  int64_t get_total_macro_block_count() const
  {
    return super_block_.get_total_macro_block_count();
  }
  int read_or_format_super_block_(const bool need_format);


private:
  ObObjectManager();
  ~ObObjectManager();

private:
  bool is_inited_;
  int64_t macro_object_size_;

  common::SpinRWLock lock_;
  storage::ObServerSuperBlock super_block_; // read only memory cache
  ObSuperBlockBufferHolder super_block_buf_holder_;
  lib::ObMutex resize_file_lock_;

  DISALLOW_COPY_AND_ASSIGN(ObObjectManager);
};

#define OB_STORAGE_OBJECT_MGR (oceanbase::blocksstable::ObObjectManager::get_instance())

}  // namespace blocksstable
}  // namespace oceanbase

#endif // OCEANBASE_BLOCKSSTABLE_OB_OBJECT_MANAGER_H_
