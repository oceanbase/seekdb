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

#ifndef OB_STORAGE_CKPT_LINKED_MARCO_BLOCK_STRUCT_H_
#define OB_STORAGE_CKPT_LINKED_MARCO_BLOCK_STRUCT_H_

#include "lib/utility/ob_print_utils.h"
#include "storage/blocksstable/ob_macro_block_id.h"
#include "storage/blocksstable/ob_macro_block_handle.h"

namespace oceanbase
{
namespace blocksstable
{
  class ObSSTableMacroInfo;
}
namespace storage
{

struct ObLinkedMacroBlockHeader final
{
  static const int32_t LINKED_MACRO_BLOCK_HEADER_VERSION = 1;
  static const int32_t LINKED_MACRO_BLOCK_HEADER_MAGIC = 10000;

  ObLinkedMacroBlockHeader()
  {
    reset();
  }
  ~ObLinkedMacroBlockHeader() = default;
  const blocksstable::MacroBlockId get_previous_block_id() const
  {
    return previous_macro_block_id_;
  }
  void set_previous_block_id(const blocksstable::MacroBlockId &block_id)
  {
    previous_macro_block_id_ = block_id;
  }

  int serialize(char *buf, const int64_t buf_len, int64_t &pos) const;
  int deserialize(const char *buf, const int64_t data_len, int64_t &pos);
  int64_t get_serialize_size()
  {
    return sizeof(version_) + sizeof(magic_) + sizeof(item_count_) + sizeof(fragment_offset_)
        + previous_macro_block_id_.get_serialize_size();
  }

  void reset()
  {
    version_ = LINKED_MACRO_BLOCK_HEADER_VERSION;
    magic_ = LINKED_MACRO_BLOCK_HEADER_MAGIC;
    item_count_ = 0;
    fragment_offset_ = 0;
    previous_macro_block_id_.reset();
  }

  TO_STRING_KV(
    K_(version), K_(magic), K_(item_count), K_(fragment_offset), K_(previous_macro_block_id));

  int32_t version_;
  int32_t magic_;
  int32_t item_count_;
  int32_t fragment_offset_;
  blocksstable::MacroBlockId previous_macro_block_id_;
};

struct ObLinkedMacroBlockItemHeader final
{
  static const int32_t LINKED_MACRO_BLOCK_ITEM_HEADER_VERSION = 1;
  static const int32_t LINKED_MACRO_BLOCK_ITEM_MAGIC = 10001;

  ObLinkedMacroBlockItemHeader()
    : version_(LINKED_MACRO_BLOCK_ITEM_HEADER_VERSION), magic_(LINKED_MACRO_BLOCK_ITEM_MAGIC),
      payload_size_(0), payload_crc_(0)
  {
  }
  ~ObLinkedMacroBlockItemHeader() = default;

  bool is_valid() const
  {
    return LINKED_MACRO_BLOCK_ITEM_HEADER_VERSION == version_ &&
      LINKED_MACRO_BLOCK_ITEM_MAGIC == magic_;
  }

  TO_STRING_KV(K_(version), K_(magic), K_(payload_size), K_(payload_crc));

  int32_t version_;
  int32_t magic_;
  int32_t payload_size_;
  int32_t payload_crc_;
};

class ObMetaBlockListHandle final
{
public:
  ObMetaBlockListHandle();
  ~ObMetaBlockListHandle();
  int add_macro_blocks(const common::ObIArray<blocksstable::MacroBlockId> &block_list);
  void reset();
  const common::ObIArray<blocksstable::MacroBlockId> &get_meta_block_list() const;
private:
  void switch_handle();
  void reset_new_handle();
private:
  static const int64_t META_BLOCK_HANDLE_CNT = 2;
  blocksstable::ObStorageObjectsHandle meta_handles_[META_BLOCK_HANDLE_CNT];
  int64_t cur_handle_pos_;
};

}  // end namespace storage
}  // end namespace oceanbase

#endif  // OB_STORAGE_CKPT_LINKED_MARCO_BLOCK_STRUCT_H_
