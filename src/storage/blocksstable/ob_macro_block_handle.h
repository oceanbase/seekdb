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

#ifndef OCEANBASE_BLOCKSSTABLE_OB_MACRO_BLOCK_HANDLE
#define OCEANBASE_BLOCKSSTABLE_OB_MACRO_BLOCK_HANDLE

#include "lib/container/ob_array.h"
#include "lib/utility/ob_print_utils.h"
#include "share/io/ob_io_define.h"
#include "storage/blocksstable/ob_macro_block_id.h"

namespace oceanbase
{
namespace blocksstable
{
struct ObMacroBlockReadInfo;
struct ObMacroBlockWriteInfo;

class ObMacroBlockHandle final
{
public:
  ObMacroBlockHandle() = default;
  ~ObMacroBlockHandle();
  ObMacroBlockHandle(const ObMacroBlockHandle &other);
  ObMacroBlockHandle &operator=(const ObMacroBlockHandle &other);
  void reset();
  void reuse();
  void reset_macro_id();
  bool is_valid() const { return io_handle_.is_valid(); }
  bool is_empty() const { return io_handle_.is_empty(); }
  OB_INLINE bool is_finished() const { return io_handle_.is_empty() || io_handle_.is_finished(); }
  const char *get_buffer() { return io_handle_.get_buffer(); }
  const MacroBlockId& get_macro_id() const { return macro_id_; }
  common::ObIOHandle &get_io_handle() { return io_handle_; }
  int64_t get_data_size() const { return io_handle_.get_data_size(); }
  int async_read(const ObMacroBlockReadInfo &read_info);
  int async_write(const ObMacroBlockWriteInfo &write_info);
  int set_macro_block_id(const MacroBlockId &macro_block_id);
  int wait(const int64_t wait_timeout_ms=UINT64_MAX);
  TO_STRING_KV(K_(macro_id), K_(io_handle));
private:
  int report_bad_block() const;
  static uint64_t get_tenant_id();
private:
  MacroBlockId macro_id_;
  common::ObIOHandle io_handle_;
};

class ObStorageObjectsHandle final
{
public:
  ObStorageObjectsHandle();
  ~ObStorageObjectsHandle();
  int add(const MacroBlockId &macro_id);
  int64_t count() const { return macro_id_list_.count(); }
  MacroBlockId at(const int64_t i) const { return macro_id_list_.at(i); }
  const common::ObIArray<MacroBlockId> &get_macro_id_list() const { return macro_id_list_; }
  common::ObIArray<MacroBlockId> &get_macro_id_list() { return macro_id_list_; }
  void reset();
  int reserve(const int64_t block_cnt);
  TO_STRING_KV(K_(macro_id_list));
private:
  common::ObArray<MacroBlockId> macro_id_list_;
  DISALLOW_COPY_AND_ASSIGN(ObStorageObjectsHandle);
};

} // namespace blocksstable
} // namespace oceanbase

#endif // OCEANBASE_BLOCKSSTABLE_OB_MACRO_BLOCK_HANDLE
