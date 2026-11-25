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

#ifndef OCEANBASE_STORAGE_BLOCKSSTABLE_OB_OBJECT_HANDLE_H_
#define OCEANBASE_STORAGE_BLOCKSSTABLE_OB_OBJECT_HANDLE_H_

#include "share/io_request.h"
#include "storage/blocksstable/ob_macro_block_id.h"
namespace oceanbase
{
namespace storage
{
class ObBloomFilterBuildTask;
}
namespace blocksstable
{
struct ObStorageObjectReadInfo;
struct ObStorageObjectWriteInfo;
class ObMacroBlockWriter;

class ObStorageObjectHandle final
{
  // for call set_macro_block_id
  friend class ObObjectManager; // in ObObjectManager::ss_get_object_id
  friend class ObBlockManager; // in ObBlockManager::alloc_object
  friend class storage::ObBloomFilterBuildTask; // keeps the macro block alive while the task is queued
public:
  ObStorageObjectHandle() = default;
  ~ObStorageObjectHandle();
  ObStorageObjectHandle(const ObStorageObjectHandle &other);
  ObStorageObjectHandle &operator=(const ObStorageObjectHandle &other);
  void reset();
  void reuse();
  void reset_macro_id();
  bool is_valid() const { return io_handle_.is_valid(); }
  bool is_empty() const { return io_handle_.is_empty(); }
  OB_INLINE bool is_finished() const { return io_handle_.is_empty() || io_handle_.is_finished(); }
  const char *get_buffer() { return io_handle_.get_buffer(); }
  const ObMicroBlockCacheValue *get_micro_block() const
  {
    const auto *callback = io_handle_.get_callback_output();
    return callback != nullptr ? callback->micro_block() : nullptr;
  }
  const ObMultiBlockIOResult *get_micro_blocks() const
  {
    const auto *callback = io_handle_.get_callback_output();
    return callback != nullptr ? callback->micro_blocks() : nullptr;
  }
  const MacroBlockId& get_macro_id() const { return macro_id_; }
  common::ObIOHandle &get_io_handle() { return io_handle_; }
  int64_t get_data_size() const { return io_handle_.get_data_size(); }
  int async_read(const ObStorageObjectReadInfo &read_info,
      std::unique_ptr<storage_io::CallbackAdapter> callback = nullptr);
  int async_write(const ObStorageObjectWriteInfo &write_info);
  int wait();
  int get_io_time_us(int64_t &io_time_us) const;
  TO_STRING_KV(K_(macro_id), K_(io_handle));

private:
  int set_macro_block_id(const MacroBlockId &macro_block_id);
  int report_bad_block() const;
  
  int sn_async_read(const ObStorageObjectReadInfo &read_info,
      std::unique_ptr<storage_io::CallbackAdapter> callback);
  int sn_async_write(const ObStorageObjectWriteInfo &write_info);

private:
  MacroBlockId macro_id_;
  common::ObIOHandle io_handle_;
};

} // namespace blocksstable
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_BLOCKSSTABLE_OB_OBJECT_HANDLE_H_
