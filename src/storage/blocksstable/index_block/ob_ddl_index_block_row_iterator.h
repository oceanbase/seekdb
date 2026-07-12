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

#ifndef OCEANBASE_STORAGE_BLOCKSSTABLE_OB_DDL_INDEX_BLOCK_ROW_ITERATOR_H
#define OCEANBASE_STORAGE_BLOCKSSTABLE_OB_DDL_INDEX_BLOCK_ROW_ITERATOR_H

#include "storage/blocksstable/index_block/ob_index_block_row_scanner.h"
#include "storage/blocksstable/index_block/ob_index_block_macro_iterator.h"

namespace oceanbase
{

namespace storage
{
class ObDDLMemtable;
}
namespace blocksstable
{
typedef keybtree::BtreeIterator<blocksstable::ObDatumRowkeyWrapper, storage::ObBlockMetaTreeValue *> DDLBtreeIterator;
class ObDDLIndexBlockRowIterator : public ObIndexBlockRowIterator
{
public:
  ObDDLIndexBlockRowIterator();
  virtual ~ObDDLIndexBlockRowIterator();
  virtual int init(const ObMicroBlockData &idx_block_data,
                   const ObStorageDatumUtils *datum_utils,
                   ObIAllocator *allocator,
                   const bool is_reverse_scan,
                   const ObIndexBlockIterParam &iter_param) override;
  virtual int get_current(const ObIndexBlockRowHeader *&idx_row_header,
                          ObCommonDatumRowkey &endkey) override;
  virtual int get_next(const ObIndexBlockRowHeader *&idx_row_header,
                       ObCommonDatumRowkey &endkey,
                       bool &is_scan_left_border,
                       bool &is_scan_right_border,
                       const ObIndexBlockRowMinorMetaInfo *&idx_minor_info,
                       const char *&agg_row_buf,
                       int64_t &agg_buf_size,
                       int64_t &row_offset) override;
  virtual int locate_key(const ObDatumRowkey &rowkey) override;
  virtual int locate_range(const ObDatumRange &range,
                           const bool is_left_border,
                           const bool is_right_border) override;
  virtual int locate_range() override;
  virtual int skip_to_next_valid_position(const ObDatumRowkey &rowkey) override;
  virtual int find_rowkeys_belong_to_same_idx_row(ObMicroIndexInfo &idx_block_row, int64_t &rowkey_begin_idx, int64_t &rowkey_end_idx, const ObRowsInfo *&rows_info) override;
  virtual int check_blockscan(const ObDatumRowkey &rowkey, bool &can_blockscan) override;
  virtual bool end_of_block() const override;
  virtual int get_index_row_count(const ObDatumRange &range,
                                  const bool is_left_border,
                                  const bool is_right_border,
                                  int64_t &index_row_count,
                                  int64_t &data_row_count) override;
  virtual void reset() override;
  virtual void reuse() override;
  virtual void set_iter_end() override { is_iter_finish_ = true; }
  INHERIT_TO_STRING_KV("base iterator:", ObIndexBlockRowIterator, "format:", "ObDDLIndexBlockRowIterator",
                       K_(is_iter_start), K_(is_iter_finish), KP(cur_tree_value_), KP(block_meta_tree_));
public:
  int set_iter_param(const ObStorageDatumUtils *datum_utils,
                     bool is_reverse_scan,
                     const storage::ObBlockMetaTree *block_meta_tree,
                     const int64_t iter_step = INT64_MAX);
  bool is_valid() { return OB_NOT_NULL(block_meta_tree_); }
  int get_next_meta(const ObDataMacroBlockMeta *&meta);
private:
  int inner_get_current(const ObIndexBlockRowHeader *&idx_row_header,
                        ObCommonDatumRowkey &endkey);
private:
  bool is_iter_start_;
  bool is_iter_finish_;
  DDLBtreeIterator btree_iter_;
  const storage::ObBlockMetaTree *block_meta_tree_;
  storage::ObBlockMetaTreeValue *cur_tree_value_;
};

} // end namespace blocksstable
} // end namespace oceanbase
#endif
