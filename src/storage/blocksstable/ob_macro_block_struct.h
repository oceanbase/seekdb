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

#include "storage/blocksstable/ob_block_sstable_struct.h"

#ifndef OB_MACRO_BLOCK_STRUCT_H_
#define OB_MACRO_BLOCK_STRUCT_H_

namespace oceanbase
{
namespace blocksstable
{
struct ObDataMacroBlockMeta;
struct ObMacroBlocksWriteCtx final
{
public:
  static const int64_t DEFAULT_READ_BLOCK_NUM = 8;
  ObMacroBlocksWriteCtx();
  ~ObMacroBlocksWriteCtx();
  void reset();
  void clear();
  int set(ObMacroBlocksWriteCtx &src);
  int deep_copy(ObMacroBlocksWriteCtx *&dst, ObIAllocator &allocator);
  int get_macro_id_array(common::ObIArray<blocksstable::MacroBlockId> &block_ids);
  int add_macro_block_id(const blocksstable::MacroBlockId &macro_block_id);
  int pop_macro_block_id(blocksstable::MacroBlockId &macro_block_id);
  OB_INLINE void increment_old_block_count() { ++use_old_macro_block_count_; }
  OB_INLINE int64_t get_macro_block_count() const { return macro_block_list_.count(); }
  OB_INLINE bool is_empty() const { return macro_block_list_.empty(); }
  OB_INLINE common::ObIArray<blocksstable::MacroBlockId> &get_macro_block_list() { return macro_block_list_; }
  TO_STRING_KV(K(macro_block_list_.count()), K_(use_old_macro_block_count));
public:
  common::ObSEArray<blocksstable::MacroBlockId, DEFAULT_READ_BLOCK_NUM> macro_block_list_;
  int64_t use_old_macro_block_count_;
  DISALLOW_COPY_AND_ASSIGN(ObMacroBlocksWriteCtx);
};

} // end namespace blocksstable
} // end namespace oceanbase

#endif /* OB_MACRO_BLOCK_STRUCT_H_ */
