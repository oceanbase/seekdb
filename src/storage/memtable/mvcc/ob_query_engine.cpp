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

#include "storage/memtable/mvcc/ob_query_engine.h"
#include "storage/memtable/mvcc/ob_btree_iter_cache.h"
#include "lib/allocator/ob_malloc.h"
#include "common/ob_store_range.h"
#include "storage/blocksstable/ob_row_reader.h"

namespace oceanbase
{
namespace memtable
{
using namespace common;

// modify buf size in ob_keybtree.h together, otherwise there may be memory waste or overflow.
STATIC_ASSERT(sizeof(ObQueryEngine::Iterator<keybtree::BtreeIterator<ObStoreRowkeyWrapper, ObMvccRow *>>) <= 5120, "Iterator size exceeded");
#if !defined(_WIN32)
STATIC_ASSERT(sizeof(keybtree::Iterator<ObStoreRowkeyWrapper, ObMvccRow *>) == 368, "Iterator size changed");
#endif

void ObQueryEngine::check_cleanout(bool &is_all_cleanout,
                                   bool &is_all_delay_cleanout,
                                   int64_t &count)
{
  int ret = OB_SUCCESS;
  Iterator<keybtree::BtreeIterator<ObStoreRowkeyWrapper, ObMvccRow *>> iter;
  ObStoreRowkeyWrapper scan_start_key_wrapper(&ObStoreRowkey::MIN_STORE_ROWKEY);
  ObStoreRowkeyWrapper scan_end_key_wrapper(&ObStoreRowkey::MAX_STORE_ROWKEY);
  iter.reset();

  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "not init", "this", this);
  } else if (OB_FAIL(keybtree_.set_key_range(iter.get_read_handle(),
                                             scan_start_key_wrapper,
                                             true, /*start_exclusive*/
                                             scan_end_key_wrapper,
                                             true  /*end_exclusive*/))) {
  } else {
    blocksstable::ObRowReader row_reader;
    blocksstable::ObDatumRow datum_row;
    is_all_cleanout = true;
    is_all_delay_cleanout = true;
    count = 0;
    for (int64_t row_idx = 0; OB_SUCC(ret) && OB_SUCC(iter.next_internal()); row_idx++) {
      const ObMemtableKey *key = iter.get_key();
      ObMvccRow *row = iter.get_value();
      for (ObMvccTransNode *node = row->get_list_head(); OB_SUCC(ret) && OB_NOT_NULL(node); node = node->prev_) {
        if (node->is_delayed_cleanout()) {
          is_all_cleanout = false;
        } else {
          is_all_delay_cleanout = false;
        }
        count++;
      }
    }
  }
}

void ObQueryEngine::dump2text(FILE* fd)
{
  int ret = OB_SUCCESS;
  Iterator<BtreeIterator> iter;
  ObStoreRowkeyWrapper scan_start_key_wrapper(&ObStoreRowkey::MIN_STORE_ROWKEY);
  ObStoreRowkeyWrapper scan_end_key_wrapper(&ObStoreRowkey::MAX_STORE_ROWKEY);
  iter.reset();

  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "not init", "this", this);
  } else if (OB_FAIL(keybtree_.set_key_range(iter.get_read_handle(),
                                             scan_start_key_wrapper,
                                             true, /*start_exclusive*/
                                             scan_end_key_wrapper,
                                             true  /*end_exclusive*/))) {
  } else {
    blocksstable::ObRowReader row_reader;
    blocksstable::ObDatumRow datum_row;
    SMART_VAR(ObCStringHelper, helper) {
      for (int64_t row_idx = 0; OB_SUCC(ret) && OB_SUCC(iter.next_internal()); row_idx++) {
        const ObMemtableKey *key = iter.get_key();
        ObMvccRow *row = iter.get_value();
        fprintf(fd, "row_idx=%ld %s %s\n", row_idx, helper.convert(*key), helper.convert(*row));
        for (ObMvccTransNode *node = row->get_list_head(); OB_SUCC(ret) && OB_NOT_NULL(node); node = node->prev_) {
          const ObMemtableDataHeader *mtd = reinterpret_cast<const ObMemtableDataHeader *>(node->buf_);
          helper.reset();
          fprintf(fd, "\t%s dml=%d size=%ld\n", helper.convert(*node), mtd->dml_flag_, mtd->buf_len_);
          if (OB_FAIL(row_reader.read_row(mtd->buf_, mtd->buf_len_, nullptr, datum_row))) {
          } else {
            for (int64_t i = 0; OB_SUCC(ret) && i < datum_row.get_column_count(); i++) {
              blocksstable::ObStorageDatum &datum = datum_row.storage_datums_[i];
              helper.reset();
              fprintf(fd, "\tcidx=%ld val=%s\n", i, helper.convert(datum));
            }
          }
        }
      }
    }
    keybtree_.dump(fd);
    fprintf(fd, "--------------------------\n");
  }
}

int64_t ObQueryEngine::btree_size() const
{
  int64_t obj_cnt = keybtree_.size();
  return obj_cnt;
}

int64_t ObQueryEngine::btree_alloc_memory() const
{
  int64_t alloc_mem = sizeof(KeyBtree) + btree_allocator_.get_allocated();

  return alloc_mem;
}

int ObQueryEngine::init()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    TRANS_LOG(WARN, "init twice", K(this));
    ret = OB_INIT_TWICE;
  } else if (OB_FAIL(keybtree_.init())) {
  } else {
    is_inited_ = true;
  }
  if (OB_FAIL(ret)) {
    destroy();
  }
  return ret;
}

void ObQueryEngine::destroy()
{
  keybtree_.destroy(true /*is_batch_destroy*/);
  btree_allocator_.reset();
  is_inited_ = false;
}

void ObQueryEngine::pre_batch_destroy_keybtree()
{
  (void)keybtree_.pre_batch_destroy();
}

int ObQueryEngine::get(const ObMemtableKey *parameter_key,
                                  ObMvccRow *&row,
                                  ObMemtableKey *returned_key)
{
  int ret = OB_SUCCESS;
  row = nullptr;

  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "not init", K(this));
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(parameter_key)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid param", KP(parameter_key));
  } else {
    const ObStoreRowkeyWrapper parameter_key_wrapper(parameter_key->get_rowkey());

    if (OB_FAIL(keybtree_.get(parameter_key_wrapper, row))) {
      if (OB_ENTRY_NOT_EXIST != ret) {
        TRANS_LOG(WARN, "get from keybtree fail", KR(ret), KPC(parameter_key));
      }
      row = nullptr;
    } else if (OB_ISNULL(row)) {
      ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(ERROR, "get NULL value from keybtree", KR(ret), KPC(parameter_key));
    }
  }
  return ret;
}

int ObQueryEngine::create_btree_kv(const ObMemtableKey *parameter_key,
                                   const ObMvccRowCreator &row_creator,
                                   ObMvccRow *&row)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "not init", K(this));
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(parameter_key)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid param", KP(parameter_key));
  } else if (OB_UNLIKELY(!row_creator.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid row creator", K(row_creator));
  } else {
    const ObStoreRowkeyWrapper parameter_key_wrapper(parameter_key->get_rowkey());
    if (OB_FAIL(keybtree_.insert_or_get(parameter_key_wrapper, row_creator, row))) {
    }
  }

  return ret;
}

int ObQueryEngine::scan(const ObMemtableKey *start_key,
                        const bool start_exclude,
                        const ObMemtableKey *end_key,
                        const bool end_exclude,
                        ObIQueryEngineIterator *&ret_iter)
{
  int ret = OB_SUCCESS;
  Iterator<BtreeIterator> *iter = nullptr;

  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "not init", "this", this);
    ret = OB_NOT_INIT;
  } else {
    void *buf = btree_iter_alloc(sizeof(Iterator<BtreeIterator>));
    if (OB_ISNULL(buf)) {
      TRANS_LOG(WARN, "alloc iter fail");
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      iter = new (buf) Iterator<BtreeIterator>();
    }
  }
  if (OB_SUCC(ret)) {
    ObStoreRowkeyWrapper scan_start_key_wrapper(start_key->get_rowkey());
    ObStoreRowkeyWrapper scan_end_key_wrapper(end_key->get_rowkey());
    iter->reset();
    if (OB_FAIL(keybtree_.set_key_range(iter->get_read_handle(),
                                        scan_start_key_wrapper, start_exclude,
                                        scan_end_key_wrapper, end_exclude))) {
      ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(ERROR, "set key range to btree scan handle fail", KR(ret));
    }
  }

  if (OB_FAIL(ret)) {
    TRANS_LOG(WARN, "query_engine scan fail", KR(ret),
              K(start_key), "start_exclude", STR_BOOL(start_exclude),
              K(end_key), "end_exclude", STR_BOOL(end_exclude),
              KP(iter));
    revert_iter(iter);
  } else {
    ret_iter = iter;
  }

  return ret;
}

void ObQueryEngine::revert_iter(ObIQueryEngineIterator *iter)
{
  if (OB_NOT_NULL(iter)) {
    auto *typed = static_cast<Iterator<BtreeIterator> *>(iter);
    typed->~Iterator();
    btree_iter_free(typed);
  }
}

int ObQueryEngine::sample_rows(Iterator<BtreeRawIterator> *iter,
                               const ObMemtableKey *start_key,
                               const int start_exclude,
                               const ObMemtableKey *end_key,
                               const int end_exclude,
                               const transaction::ObTransID &tx_id,
                               int64_t &logical_row_count,
                               int64_t &physical_row_count,
                               double &ratio)
{
  int ret = OB_SUCCESS;
  ObMvccRow *value = nullptr;
  int64_t sample_row_count = 0;
  int64_t empty_delete_row_count = 0;
  int64_t delete_row_count = 0;
  logical_row_count = 0;
  physical_row_count = 0;
  ratio = 1.5;
  ObStoreRowkeyWrapper scan_start_key_wrapper(start_key->get_rowkey());
  ObStoreRowkeyWrapper scan_end_key_wrapper(end_key->get_rowkey());
  iter->reset();

  if (OB_FAIL(keybtree_.set_key_range(iter->get_read_handle(),
                                      scan_start_key_wrapper,
                                      start_exclude,
                                      scan_end_key_wrapper,
                                      end_exclude))) {
  } else {
    while (OB_SUCC(ret)) {
      if (OB_FAIL(iter->next())) {
        if (OB_ITER_END != ret) {
          TRANS_LOG(WARN, "query engine iter next fail", KR(ret));
        }
      } else if (OB_ISNULL(value = iter->get_value())) {
        TRANS_LOG(ERROR, "unexpected value null pointer");
        ret = OB_ERR_UNEXPECTED;
      } else {
        ++sample_row_count;
        ++physical_row_count;
        if (blocksstable::ObDmlFlag::DF_NOT_EXIST == value->first_dml_flag_ &&
            blocksstable::ObDmlFlag::DF_NOT_EXIST == value->last_dml_flag_) {
          ObMvccTransNode *iter = value->get_list_head();
          if (nullptr != iter && iter->get_tx_id() == tx_id) {
            // Case1: uncommited row set by myself in the memtable
            ++logical_row_count;
          }
        } else if (blocksstable::ObDmlFlag::DF_INSERT == value->first_dml_flag_
            && blocksstable::ObDmlFlag::DF_DELETE != value->last_dml_flag_) {
          // Case2: insert new row in the memtable
          ++logical_row_count;
        } else if (blocksstable::ObDmlFlag::DF_DELETE == value->last_dml_flag_) {
          if (blocksstable::ObDmlFlag::DF_INSERT != value->first_dml_flag_) {
            // Case3: delete an existent row in the memtable
            --logical_row_count;
            ++delete_row_count;
          } else {
            // Case4: insert and then delete a new row in the memtable
            ++empty_delete_row_count;
          }
        } else {
          // Case5: existent row, not change estimation total row count
        }

        if (sample_row_count >= MAX_SAMPLE_ROW_COUNT) {
          break;
        }
      }
    }
  }

  const int64_t total_delete_row_count = delete_row_count + empty_delete_row_count;
  if (0 < total_delete_row_count) {
    ratio = 1.0 + static_cast<double>(delete_row_count) / static_cast<double>(total_delete_row_count);
  }

  return ret;
}

int ObQueryEngine::init_raw_iter_for_estimate(Iterator<BtreeRawIterator>*& iter,
                                              const ObMemtableKey *start_key,
                                              const ObMemtableKey *end_key)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "not init", "this", this);
    ret = OB_NOT_INIT;
  } else {
    void *buf = ob_malloc(sizeof(Iterator<BtreeRawIterator>), ObMemAttr("BtreeRawIter"));
    if (OB_ISNULL(buf)) {
      TRANS_LOG(WARN, "alloc raw iter fail");
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      iter = new (buf) Iterator<BtreeRawIterator>();
    }
  }
  if (OB_SUCC(ret)) {
    ObStoreRowkeyWrapper start_key_wrapper(start_key->get_rowkey());
    ObStoreRowkeyWrapper end_key_wrapper(end_key->get_rowkey());
    iter->reset();
    if (OB_FAIL(keybtree_.set_key_range(iter->get_read_handle(),
                                        start_key_wrapper,
                                        true, /*start_exclusive*/
                                        end_key_wrapper,
                                        true  /*start_exclusive*/))) {
    }
  }

  return ret;
}

int ObQueryEngine::estimate_size(const ObMemtableKey *start_key,
                                 const ObMemtableKey *end_key,
                                 int64_t &total_bytes,
                                 int64_t &total_rows)
{
  int ret = OB_SUCCESS;
  int64_t unused_top_level = 0;
  int64_t unused_btree_node_count = 0;
  if (OB_FAIL(inner_loop_find_level_(
          start_key, end_key, ESTIMATE_CHILD_COUNT_THRESHOLD, unused_top_level, unused_btree_node_count, total_rows))) 
  {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
      total_bytes = 0;
      total_rows = 0;
    } else {
      TRANS_LOG(WARN, "esitmate size failed", KR(ret), KPC(start_key), KPC(end_key));
    }
  } else {
    int64_t per_row_size = 100;
    total_bytes = total_rows * per_row_size;
  }
  return ret;
}

int ObQueryEngine::split_range(const ObMemtableKey *start_key,
                               const ObMemtableKey *end_key,
                               int64_t range_count,
                               ObIArray<ObStoreRange> &range_array)
{
  int ret = OB_SUCCESS;
  Iterator<BtreeRawIterator> *iter = nullptr;
  int64_t top_level = 0;
  int64_t btree_node_count = 0;

  if (range_count < 1 || range_count > MAX_RANGE_SPLIT_COUNT) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "range count should be greater than 1 if you try to split range", KR(ret), K(range_count));
  } else {
    bool need_retry = false;
    do {
      need_retry = false;
      // Here we can not use ESTIMATE_CHILD_COUNT_THRESHOLD to init SEArray due to the stack size limit
      ObSEArray<ObStoreRowkeyWrapper, ESTIMATE_CHILD_COUNT_THRESHOLD / 2> key_array;
      if (OB_FAIL(find_split_range_level_(start_key, end_key, range_count, top_level, btree_node_count)) &&
          OB_ENTRY_NOT_EXIST != ret) {
        TRANS_LOG(WARN, "estimate size fail", K(ret), K(*start_key), K(*end_key));
      } else if (OB_ENTRY_NOT_EXIST == ret) {
        TRANS_LOG(
            WARN, "range too small, not enough rows ro split", K(ret), K(*start_key), K(*end_key), K(range_count));
      } else if (btree_node_count < range_count) {
        ret = OB_ENTRY_NOT_EXIST;
        TRANS_LOG(WARN, "branch fan out less than range count", K(btree_node_count), K(range_count));
      } else if (OB_FAIL(init_raw_iter_for_estimate(iter, start_key, end_key))) {
      } else if (NULL == iter) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(iter->get_read_handle().split_range(top_level, btree_node_count, range_count, key_array))) {
        TRANS_LOG(WARN,
                  "split range fail",
                  K(ret),
                  K(*start_key),
                  K(*end_key),
                  K(range_count),
                  K(top_level),
                  K(btree_node_count),
                  K(range_count));
        if (OB_EAGAIN == ret) {
          need_retry = true;
          ret = OB_SUCCESS;
        }
      } else if (OB_FAIL(convert_keys_to_store_ranges_(start_key, end_key, range_count, key_array, range_array))) {
      } else {
        // split range succeed
      }

      if (OB_NOT_NULL(iter)) {
        iter->~Iterator();
        ob_free(iter);
        iter = NULL;
      }
    } while (OB_SUCC(ret) && need_retry);
  }
  return ret;
}

int ObQueryEngine::find_split_range_level_(const ObMemtableKey *start_key,
                                           const ObMemtableKey *end_key,
                                           const int64_t range_count,
                                           int64_t &top_level,
                                           int64_t &btree_node_count)
{
  int64_t unused_total_rows = 0;
  const int64_t node_threshold = MAX(ESTIMATE_CHILD_COUNT_THRESHOLD, range_count);
  return inner_loop_find_level_(start_key, end_key, node_threshold, top_level, btree_node_count, unused_total_rows);
}

int ObQueryEngine::inner_loop_find_level_(const ObMemtableKey *start_key,
                                           const ObMemtableKey *end_key,
                                           const int64_t level_node_threshold,
                                           int64_t &top_level,
                                           int64_t &btree_node_count,
                                           int64_t &total_rows)
{
  int ret = OB_SUCCESS;
  Iterator<BtreeRawIterator> *iter = nullptr;

  // directly loop on the third level because in theory, the second level of BTree can have a maximum of 15 * 15 = 225
  // nodes, which does not meet the THRESHOLD requirement.
  btree_node_count = 0;
  top_level = 2;
  while (OB_SUCC(ret) && btree_node_count < level_node_threshold) {
    top_level++;
    btree_node_count = 0;
    if (OB_FAIL(init_raw_iter_for_estimate(iter, start_key, end_key))) {
    } else if (OB_ISNULL(iter)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(iter->get_read_handle().estimate_key_count(top_level, btree_node_count, total_rows))) {
      if (OB_ENTRY_NOT_EXIST != ret) {
        TRANS_LOG(WARN, "find split range level failed", K(ret), K(*start_key), K(*end_key));
      }
    }
    if (OB_NOT_NULL(iter)) {
      iter->~Iterator();
      ob_free(iter);
      iter = NULL;
    }
  }

  if (OB_ENTRY_NOT_EXIST != ret) {
    STORAGE_LOG(INFO, "finish find split level", KR(ret), K(top_level), K(btree_node_count), K(total_rows));
  }

  return ret;
}

int ObQueryEngine::convert_keys_to_store_ranges_(const ObMemtableKey *start_key,
                                                 const ObMemtableKey *end_key,
                                                 const int64_t range_count,
                                                 const common::ObIArray<ObStoreRowkeyWrapper> &key_array,
                                                 ObIArray<ObStoreRange> &range_array)
{
  int ret = OB_SUCCESS;

  ObStoreRange merge_range;
  for (int64_t i = 0; OB_SUCC(ret) && i < range_count; i++) {
    const ObStoreRowkey *rowkey = nullptr;
    // start key
    if (0 == i) {
      if (OB_ISNULL(rowkey = start_key->get_rowkey())) {
        ret = OB_ERR_UNEXPECTED;
        TRANS_LOG(WARN, "Unexcepted null store rowkey", K(ret), KPC(start_key));
      } else {
        merge_range.set_start_key(*rowkey);
      }
    } else {
      merge_range.set_start_key(*key_array.at(i - 1).get_rowkey());
    }

    // endkey
    if (OB_FAIL(ret)) {
    } else if (i == range_count - 1) {
      if (OB_ISNULL(rowkey = end_key->get_rowkey())) {
        ret = OB_ERR_UNEXPECTED;
        TRANS_LOG(WARN, "Unexcepted null store rowkey", K(ret), KPC(start_key));
      } else {
        merge_range.set_end_key(*rowkey);
      }
    } else if (OB_ISNULL(rowkey = key_array.at(i).get_rowkey())) {
      ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(WARN, "Unexpected null store rowkey", K(ret), K(i));
    } else {
      merge_range.set_end_key(*rowkey);
    }
    if (OB_SUCC(ret)) {
      merge_range.set_left_open();
      merge_range.set_right_closed();
      if (OB_FAIL(range_array.push_back(merge_range))) {
      }
    }
  }
  return ret;
}

int ObQueryEngine::estimate_row_count(const transaction::ObTransID &tx_id,
                                      const ObMemtableKey *start_key, const int start_exclude,
                                      const ObMemtableKey *end_key, const int end_exclude,
                                      int64_t &logical_row_count,
                                      int64_t &physical_row_count)
{
  int ret = OB_SUCCESS;
  Iterator<BtreeRawIterator> *iter = nullptr;
  int64_t remaining_row_count = 0;
  int64_t element_count = 0;
  int64_t phy_row_count1 = 0;
  int64_t phy_row_count2 = 0;
  int64_t log_row_count1 = 0;
  int64_t log_row_count2 = 0;
  double ratio1 = 1.5;
  double ratio2 = 1.5;
  double ratio = 1.5;
  logical_row_count = 0;
  physical_row_count = 0;

  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "not init", "this", this);
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(start_key) || OB_ISNULL(end_key)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid param", KR(ret));
  } else {
    void *buf = ob_malloc(sizeof(Iterator<BtreeRawIterator>), ObMemAttr("BtreeRawIter"));
    if (OB_ISNULL(buf)) {
      TRANS_LOG(WARN, "alloc raw iter fail");
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      iter = new (buf) Iterator<BtreeRawIterator>();
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(sample_rows(iter, end_key, end_exclude, start_key, start_exclude, tx_id,
      log_row_count1, phy_row_count1, ratio1))) {
    if (OB_ITER_END != ret) {
      TRANS_LOG(WARN, "failed to sample rows reverse", KR(ret), K(*start_key), K(*end_key));
    }
  } else if (OB_FAIL(sample_rows(iter, start_key, start_exclude, end_key, end_exclude, tx_id,
      log_row_count2, phy_row_count2, ratio2))) {
    if (OB_ITER_END != ret) {
      TRANS_LOG(WARN, "failed to sample rows", KR(ret), K(*start_key), K(*end_key));
    }
  }

  logical_row_count = log_row_count1 + log_row_count2;
  // since remaining_row_count actually includes phy_row_count1, so we don't
  // want to add it twice here
  physical_row_count = phy_row_count1 + phy_row_count2;
  ratio = (ratio1 + ratio2) / 2;

  if (OB_SUCC(ret)) {
    // fast caculate the remaining row count
    if (OB_FAIL(iter->get_read_handle().estimate_element_count(remaining_row_count, element_count))) {
      if (OB_ITER_END != ret) {
        TRANS_LOG(WARN, "estimate row count fail", KR(ret));
      } else {
        // logical row count should be calculated with btree element count
        logical_row_count = static_cast<int64_t>(static_cast<double>(logical_row_count)
            * (static_cast<double>(element_count + MAX_SAMPLE_ROW_COUNT)
                / static_cast<double>(MAX_SAMPLE_ROW_COUNT * 2)));
        physical_row_count += remaining_row_count - phy_row_count1;
        ret = OB_SUCCESS;
      }
    }
  }
  if (OB_NOT_NULL(iter)) {
    iter->~Iterator();
    ob_free(iter);
    iter = NULL;
  }
  ret = OB_ITER_END == ret ? OB_SUCCESS : ret;
  return ret;
}

} // namespace memtable
} // namespace oceanbase
