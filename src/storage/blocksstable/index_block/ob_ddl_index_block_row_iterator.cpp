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

#define USING_LOG_PREFIX STORAGE
#include "ob_ddl_index_block_row_iterator.h"
#include "storage/access/ob_rows_info.h"
#include "storage/ddl/ob_tablet_ddl_kv.h"

namespace oceanbase
{
namespace blocksstable
{

/******************             ObDDLIndexBlockRowIterator              **********************/
ObDDLIndexBlockRowIterator::ObDDLIndexBlockRowIterator()
  : is_iter_start_(false),
    is_iter_finish_(true),
    btree_iter_(),
    block_meta_tree_(nullptr),
    cur_tree_value_(nullptr)
{

}

ObDDLIndexBlockRowIterator::~ObDDLIndexBlockRowIterator()
{
  reset();
}

void ObDDLIndexBlockRowIterator::reset()
{
  ObIndexBlockRowIterator::reset();
  is_iter_finish_ = true;
  is_iter_start_ = false;
  btree_iter_.reset();
  block_meta_tree_ = nullptr;
  cur_tree_value_ = nullptr;
}

void ObDDLIndexBlockRowIterator::reuse()
{
  ObIndexBlockRowIterator::reuse();
  is_iter_finish_ = true;
  is_iter_start_ = false;
  btree_iter_.reset();
  block_meta_tree_ = nullptr;
  cur_tree_value_ = nullptr;
}

int ObDDLIndexBlockRowIterator::init(const ObMicroBlockData &idx_block_data,
                                     const ObStorageDatumUtils *datum_utils,
                                     ObIAllocator *allocator,
                                     const bool is_reverse_scan,
                                     const ObIndexBlockIterParam &iter_param)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(datum_utils) || !datum_utils->is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguement", K(ret), KP(allocator), KPC(datum_utils));
  } else {
    block_meta_tree_ = reinterpret_cast<const ObBlockMetaTree *>(idx_block_data.buf_);
    is_reverse_scan_ = is_reverse_scan;
    iter_step_ = is_reverse_scan_ ? -1 : 1;
    datum_utils_ = datum_utils;
    is_inited_ = true;
  }
  return ret;
}

int ObDDLIndexBlockRowIterator::set_iter_param(const ObStorageDatumUtils *datum_utils,
                                               bool is_reverse_scan,
                                               const storage::ObBlockMetaTree *block_meta_tree,
                                               const int64_t iter_step)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(datum_utils) || OB_UNLIKELY(!datum_utils->is_valid()) || OB_ISNULL(block_meta_tree)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguement", K(ret), KP(block_meta_tree), KPC(datum_utils));
  } else {
    block_meta_tree_ = block_meta_tree;
    is_reverse_scan_ = is_reverse_scan;
    iter_step_ = iter_step == INT64_MAX ? (is_reverse_scan_ ? -1 : 1) : iter_step;
    datum_utils_ = datum_utils;
    is_inited_ = true;
  }
  return ret;
}

int ObDDLIndexBlockRowIterator::locate_key(const ObDatumRowkey &rowkey)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Iter not opened yet", K(ret), KPC(this));
  } else if (OB_UNLIKELY(!rowkey.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid rowkey", K(ret), K(rowkey));
  } else {
    ObDatumRange range;
    range.set_start_key(rowkey);
    range.set_end_key(rowkey);
    range.set_left_closed();
    range.set_right_closed();
    if (OB_ISNULL(block_meta_tree_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("block meta tree is null", K(ret));
    } else if (OB_FAIL(block_meta_tree_->locate_key(range,
                                                    *datum_utils_,
                                                    btree_iter_,
                                                    cur_tree_value_))) {
      if (OB_UNLIKELY(OB_BEYOND_THE_RANGE != ret)) {
        LOG_WARN("locate rowkey failed", K(ret), K(range), K(*this));
      } else {
        is_iter_finish_ = true;
      }
    } else if (OB_ISNULL(cur_tree_value_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("cur tree value is null", K(ret), KP(cur_tree_value_));
    } else {
      is_iter_start_ = true;
      is_iter_finish_ = false;
    }
  }
  return ret;
}

int ObDDLIndexBlockRowIterator::locate_range(const ObDatumRange &range,
                                             const bool is_left_border,
                                             const bool is_right_border)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Iter not opened yet", K(ret), KPC(this));
  } else if (OB_UNLIKELY(!range.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid range", K(ret), K(range));
  } else if (OB_ISNULL(block_meta_tree_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("block meta tree is null", K(ret));
  } else if (OB_FAIL(block_meta_tree_->locate_range(range,
                                                    *datum_utils_,
                                                    is_left_border,
                                                    is_right_border,
                                                    is_reverse_scan_,
                                                    btree_iter_,
                                                    cur_tree_value_))) {
    is_iter_finish_ = true;
    LOG_WARN("block meta tree locate range failed", K(ret), K(range));
  } else if (OB_ISNULL(cur_tree_value_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("cur tree value is null", K(ret), KP(cur_tree_value_));
  } else {
    is_iter_start_ = true;
    is_iter_finish_ = false;
  }
  return ret;
}

int ObDDLIndexBlockRowIterator::locate_range()
{
  int ret = OB_SUCCESS;
  ObDatumRange range;
  range.set_start_key(ObDatumRowkey::MIN_ROWKEY);
  range.set_end_key(ObDatumRowkey::MAX_ROWKEY);
  range.set_left_open();
  range.set_right_open();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Iter not opened yet", K(ret), KPC(this));
  } else if (OB_ISNULL(block_meta_tree_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("block meta tree is null", K(ret));
  } else if (OB_FAIL(block_meta_tree_->locate_range(range,
                                                    *datum_utils_,
                                                    false, /*is_left_border*/
                                                    false, /*is_right_border*/
                                                    is_reverse_scan_,
                                                    btree_iter_,
                                                    cur_tree_value_))) {
    if (OB_BEYOND_THE_RANGE != ret) {
      LOG_WARN("block meta tree locate range failed", K(ret), K(range));
    } else {
      is_iter_finish_ = true;
      LOG_INFO("no data to locate", K(ret));
      ret = OB_SUCCESS;
    }
  } else if (OB_ISNULL(cur_tree_value_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("cur tree value is null", K(ret), KP(cur_tree_value_));
  } else {
    is_iter_start_ = true;
    is_iter_finish_ = false;
  }
  return ret;
}

int ObDDLIndexBlockRowIterator::skip_to_next_valid_position(const ObDatumRowkey &rowkey)
{
  int ret = OB_SUCCESS;
  storage::ObBlockMetaTreeValue *tmp_tree_value = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Iter not opened yet", K(ret), KPC(this));
  } else if (OB_ISNULL(block_meta_tree_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("block meta tree is null", K(ret));
  } else if (OB_FAIL(block_meta_tree_->skip_to_next_valid_position(rowkey,
                                                                   *datum_utils_,
                                                                   btree_iter_,
                                                                   tmp_tree_value))) {
    if (OB_UNLIKELY(OB_ITER_END != ret)) {
      LOG_WARN("Failed to skip to next valid position in block meta tree", K(ret), K(rowkey));
    } else {
      is_iter_finish_ = true;
    }
  } else {
    cur_tree_value_ = tmp_tree_value;
  }
  return ret;
}

int ObDDLIndexBlockRowIterator::find_rowkeys_belong_to_same_idx_row(ObMicroIndexInfo &idx_block_row, int64_t &rowkey_begin_idx, int64_t &rowkey_end_idx, const ObRowsInfo *&rows_info)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Iter not opened yet", K(ret), KPC(this));
  } else if (OB_ISNULL(rows_info)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid rows info", K(ret));
  } else {
    const ObDatumRowkey *cur_rowkey = cur_tree_value_->rowkey_;
    bool is_decided = false;
    for (; OB_SUCC(ret) && rowkey_begin_idx < rowkey_end_idx; ++rowkey_begin_idx) {
      if (rows_info->is_row_skipped(rowkey_begin_idx)) {
        continue;
      }
      const ObDatumRowkey &rowkey = rows_info->get_rowkey(rowkey_begin_idx);
      int32_t cmp_ret = 0;
      if (OB_ISNULL(cur_rowkey)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("null rowkey", K(ret), K(cur_tree_value_), KP(cur_rowkey));
      } else if (OB_FAIL(rowkey.compare(*cur_rowkey, *datum_utils_, cmp_ret, false))) {
      }

      if (OB_FAIL(ret)) {
      } else if (cmp_ret > 0) {
        idx_block_row.rowkey_end_idx_ = rowkey_begin_idx;
        is_decided = true;
        break;
      } else if (cmp_ret == 0) {
        idx_block_row.rowkey_end_idx_ = rowkey_begin_idx + 1;
        is_decided = true;
        break;
      }
    }

    if (OB_SUCC(ret) && !is_decided) {
      idx_block_row.rowkey_end_idx_ = rowkey_begin_idx;
    }
  }
  return ret;
}

int ObDDLIndexBlockRowIterator::check_blockscan(const ObDatumRowkey &rowkey, bool &can_blockscan)
{
  int ret = OB_SUCCESS;
  can_blockscan = false;
  return ret;
}

int ObDDLIndexBlockRowIterator::get_current(const ObIndexBlockRowHeader *&idx_row_header,
                                            ObCommonDatumRowkey &endkey)
{
  int ret = OB_SUCCESS;
  bool is_start_key = false;
  bool is_end_key = false;
  idx_row_header = nullptr;
  endkey.reset();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Iter not opened yet", K(ret), KPC(this));
  } else if (OB_ISNULL(cur_tree_value_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("cur tree value is null", K(ret));
  } else {
    idx_row_header = &(cur_tree_value_->header_);
    endkey.set_compact_rowkey(&(cur_tree_value_->block_meta_->end_key_));
  }
  return ret;
}

int ObDDLIndexBlockRowIterator::inner_get_current(const ObIndexBlockRowHeader *&idx_row_header,
                                                  ObCommonDatumRowkey &endkey)
{
  int ret = OB_SUCCESS;
  bool is_start_key = false;
  bool is_end_key = false;
  idx_row_header = nullptr;
  endkey.reset();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Iter not opened yet", K(ret), KPC(this));
  } else if (OB_ISNULL(cur_tree_value_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("cur tree value is null", K(ret));
  } else {
    idx_row_header = &(cur_tree_value_->header_);
    endkey.set_compact_rowkey(&(cur_tree_value_->block_meta_->end_key_));
  }
  return ret;
}

int ObDDLIndexBlockRowIterator::get_next(const ObIndexBlockRowHeader *&idx_row_header,
                                         ObCommonDatumRowkey &endkey,
                                         bool &is_scan_left_border,
                                         bool &is_scan_right_border,
                                         const ObIndexBlockRowMinorMetaInfo *&idx_minor_info,
                                         const char *&agg_row_buf,
                                         int64_t &agg_buf_size,
                                         int64_t &row_offset)
{
  int ret = OB_SUCCESS;
  idx_row_header = nullptr;
  endkey.reset();
  is_scan_left_border = false;
  is_scan_right_border = false;
  idx_minor_info = nullptr;
  agg_row_buf = nullptr;
  agg_buf_size = 0;
  row_offset = 0;
  bool is_start_key = false;
  bool is_end_key = false;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Iter not opened yet", K(ret), KPC(this));
  } else if (OB_FAIL(inner_get_current(idx_row_header, endkey))) {
  } else if (OB_UNLIKELY(nullptr == idx_row_header || !endkey.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Unexpected null index block row header/endkey", K(ret), KP(idx_row_header), K(endkey));
  } else if (OB_UNLIKELY((idx_row_header->is_data_index() && !idx_row_header->is_major_node()) ||
                         idx_row_header->is_pre_aggregated() ||
                         !idx_row_header->is_major_node())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid index row header", K(ret), KPC(idx_row_header));
  }

  if (OB_SUCC(ret)) {
    if (is_iter_start_) {
      is_start_key = true;
      is_iter_start_ = false;
    }
    storage::ObBlockMetaTreeValue *tmp_tree_value = nullptr;
    if (OB_ISNULL(block_meta_tree_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("block meta iterator is null", K(ret));
    } else if (OB_FAIL(block_meta_tree_->get_next_tree_value(btree_iter_, std::abs(iter_step_), tmp_tree_value))) {
      if (OB_ITER_END != ret) {
        LOG_WARN("get index block row header failed", K(ret), K(*this));
      } else {
        is_iter_finish_ = true;
        is_end_key = true;
        ret = OB_SUCCESS;
      }
    } else {
      cur_tree_value_ = tmp_tree_value;
    }
    if (OB_SUCC(ret)) {
      is_scan_left_border = is_reverse_scan_ ? is_end_key : is_start_key;
      is_scan_right_border = is_reverse_scan_ ? is_start_key : is_end_key;
    }
  }
  return ret;
}

int ObDDLIndexBlockRowIterator::get_next_meta(const ObDataMacroBlockMeta *&meta)
{
  int ret = OB_SUCCESS;
  meta = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Iter not opened yet", K(ret), KPC(this));
  } else if (OB_ISNULL(cur_tree_value_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("cur tree value is null", K(ret));
  } else {
    meta = cur_tree_value_->block_meta_;
    if (is_iter_start_) {
      is_iter_start_ = false;
    }
    storage::ObBlockMetaTreeValue *tmp_tree_value = nullptr;
    if (OB_ISNULL(block_meta_tree_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("block meta iterator is null", K(ret));
    } else if (OB_FAIL(block_meta_tree_->get_next_tree_value(btree_iter_, std::abs(iter_step_), tmp_tree_value))) {
      if (OB_ITER_END != ret) {
        LOG_WARN("get index block row header failed", K(ret), K(*this));
      } else {
        is_iter_finish_ = true;
        ret = OB_SUCCESS;
      }
    } else {
      cur_tree_value_ = tmp_tree_value;
    }
  }
  return ret;
}

bool ObDDLIndexBlockRowIterator::end_of_block() const
{
  return is_iter_finish_;
}

int ObDDLIndexBlockRowIterator::get_index_row_count(const ObDatumRange &range,
                                                    const bool is_left_border,
                                                    const bool is_right_border,
                                                    int64_t &index_row_count,
                                                    int64_t &data_row_count)
{
  int ret = OB_SUCCESS;
  index_row_count = 0;
  DDLBtreeIterator tmp_iter;
  ObBlockMetaTreeValue *cur_tree_value = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Iter not opened yet", K(ret), KPC(this));
  } else if (OB_UNLIKELY(!range.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguement", K(ret), K(range));
  } else if (OB_ISNULL(block_meta_tree_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("block meta tree is null", K(ret));
  } else if (OB_FAIL(block_meta_tree_->locate_range(range,
                                                    *datum_utils_,
                                                    is_left_border,
                                                    is_right_border,
                                                    is_reverse_scan_,
                                                    tmp_iter,
                                                    cur_tree_value))) {
  } else {
    if (OB_NOT_NULL(cur_tree_value)) {
      ++index_row_count; //first
    }
    while (OB_SUCC(ret)) {
      ObDatumRowkeyWrapper rowkey_wrapper;
      if (OB_FAIL(tmp_iter.get_next(rowkey_wrapper, cur_tree_value))) {
        if (OB_ITER_END != ret) {
          LOG_WARN("get next failed", K(ret));
        } else {
          ret = OB_SUCCESS;
          break;
        }
      } else {
        ++index_row_count;
      }
    }
    if (OB_FAIL(ret)) {
      index_row_count = 0;
    }
  }
  return ret;
}

} // end namespace blocksstable
} // end namespace oceanbase
