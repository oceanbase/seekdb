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
#include "ob_sstable_row_scanner.h"
#include "ob_aggregated_store.h"
#include "storage/blocksstable/ob_micro_block_row_lock_checker.h"

namespace oceanbase
{
using namespace common;
namespace storage
{
template<typename PrefetchType>
inline ObSSTableRowScanner<PrefetchType>::~ObSSTableRowScanner()
{
  storage::ObAdvanceScanHelperFactory::destroy_advance_scan_helper(advance_scan_helper_);
  FREE_ITER_FROM_ALLOCATOR(long_life_allocator_, micro_data_scanner_, ObMicroBlockRowScanner);
  FREE_ITER_FROM_ALLOCATOR(long_life_allocator_, mv_micro_data_scanner_, ObMultiVersionMicroBlockRowScanner);
}

template<typename PrefetchType>
inline void ObSSTableRowScanner<PrefetchType>::reset()
{
  storage::ObAdvanceScanHelperFactory::destroy_advance_scan_helper(advance_scan_helper_);
  FREE_ITER_FROM_ALLOCATOR(long_life_allocator_, micro_data_scanner_, ObMicroBlockRowScanner);
  FREE_ITER_FROM_ALLOCATOR(long_life_allocator_, mv_micro_data_scanner_, ObMultiVersionMicroBlockRowScanner);
  is_opened_ = false;
  cur_range_idx_ = -1;
  sstable_ = nullptr;
  iter_param_ = nullptr;
  access_ctx_ = nullptr;
  micro_scanner_ = nullptr;
  prefetcher_.reset();
  ObStoreRowIterator::reset();
  advance_scan_state_.reset();
}

template<typename PrefetchType>
inline void ObSSTableRowScanner<PrefetchType>::reuse()
{
  storage::ObAdvanceScanHelperFactory::destroy_advance_scan_helper(advance_scan_helper_);
  ObStoreRowIterator::reuse();
  is_opened_ = false;
  cur_range_idx_ = -1;
  if (nullptr != micro_data_scanner_) {
    micro_data_scanner_->reuse();
  }
  if (nullptr != mv_micro_data_scanner_) {
    mv_micro_data_scanner_->reuse();
  }
  if (nullptr != block_row_store_) {
    block_row_store_->reuse();
  }
  micro_scanner_ = nullptr;
  sstable_ = nullptr;
  prefetcher_.reuse();
  advance_scan_state_.reset();
}

template<typename PrefetchType>
inline void ObSSTableRowScanner<PrefetchType>::reclaim()
{
  storage::ObAdvanceScanHelperFactory::destroy_advance_scan_helper(advance_scan_helper_);
  is_opened_ = false;
  cur_range_idx_ = -1;
  prefetcher_.reclaim();
  if (nullptr != micro_data_scanner_) {
    micro_data_scanner_->reuse();
  }
  if (nullptr != mv_micro_data_scanner_) {
    mv_micro_data_scanner_->reuse();
  }
  micro_scanner_ = nullptr;
  sstable_ = nullptr;
  iter_param_ = nullptr;
  access_ctx_ = nullptr;
  ObStoreRowIterator::reset();
  is_reclaimed_ = true;
  advance_scan_state_.reset();
}

template<typename PrefetchType>
int ObSSTableRowScanner<PrefetchType>::advance_scan(const blocksstable::ObDatumRange &range)
{
  int ret = OB_SUCCESS;
  if (nullptr == advance_scan_helper_) {
    if (OB_FAIL(ObAdvanceScanHelperFactory::build_advance_scan_helper(*iter_param_, *access_ctx_, &range, advance_scan_helper_))) {
      LOG_WARN("failed to build advance scan helper", K(ret));
    }
  } else if (OB_FAIL(advance_scan_helper_->advance_scan(range))) {
    STORAGE_LOG(WARN, "Failed to advance scan", K(ret));
  }
  return ret;
}

template<typename PrefetchType>
inline bool ObSSTableRowScanner<PrefetchType>::can_blockscan() const
{
  return is_scan(type_) && nullptr != micro_scanner_ && micro_scanner_->can_blockscan();
}

template<typename PrefetchType>
inline bool ObSSTableRowScanner<PrefetchType>::can_batch_scan() const
{
  return can_blockscan() &&
      !block_row_store_->is_disabled() &&
      micro_scanner_->is_filter_applied() &&
      // can batch scan when only enable_pd_aggregate, as it uses own datum buffer and only return aggregated result
      (iter_param_->vectorized_enabled_ || iter_param_->enable_pd_aggregate());
}

template<typename PrefetchType>
inline int ObSSTableRowScanner<PrefetchType>::inner_open(
    const ObTableIterParam &iter_param,
    ObTableAccessContext &access_ctx,
    ObITable *table,
    const void *query_range)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_opened_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("The ObSSTableRowScanner has been opened", K(ret));
  } else if (OB_UNLIKELY(nullptr == query_range ||
                         nullptr == table ||
                         !table->is_sstable())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument to init ObSSTableRowScanner", K(ret), KP(query_range), KP(table));
  } else {
    sstable_ = static_cast<ObSSTable *>(table);
    iter_param_ = &iter_param;
    access_ctx_ = &access_ctx;
    ObSampleFilterExecutor *sample_executor = static_cast<ObSampleFilterExecutor *>(access_ctx.get_sample_executor());
    if (!prefetcher_.is_valid()) {
      if (OB_FAIL(prefetcher_.init(
                  type_, *sstable_, iter_param, access_ctx, query_range))) {
        LOG_WARN("fail to init prefetcher, ", K(ret));
      }
    } else if (OB_FAIL(prefetcher_.switch_context(type_, *sstable_, iter_param, access_ctx, query_range))) {
      LOG_WARN("fail to switch context for prefetcher, ", K(ret));
    }
    if (OB_SUCC(ret)) {
      if (iter_param_->enable_pd_aggregate() &&
          nullptr != block_row_store_ &&
          iter_param_->enable_skip_index() &&
          !sstable_->is_multi_version_table()) {
        prefetcher_.agg_store_ = static_cast<ObAggStoreBase *>(static_cast<ObAggregatedStore *>(block_row_store_));
      }
      if (nullptr != sample_executor
          && sstable_->is_major_sstable()
          && OB_FAIL(sample_executor->build_row_id_handle(
                          prefetcher_.get_index_tree_height(),
                          prefetcher_.get_index_prefetch_depth(),
                          prefetcher_.get_micro_data_pefetch_depth()))) {
        LOG_WARN("Failed to build row id handle", K(ret), KPC(sample_executor));
      } else if (OB_UNLIKELY(iter_param.is_advance_scan() &&
                 OB_FAIL(ObAdvanceScanHelperFactory::build_advance_scan_helper(iter_param,
                                                                          access_ctx,
                                                                          static_cast<const ObDatumRange *>(query_range),
                                                                          advance_scan_helper_)))) {
        LOG_WARN("failed to build advance scan helper", K(ret));
      } else if (FALSE_IT(prefetcher_.advance_scan_helper_ = advance_scan_helper_)) {
      } else if (OB_FAIL(prefetcher_.prefetch())) {
        LOG_WARN("ObSSTableRowScanner prefetch failed", K(ret));
      } else {
        is_opened_ = true;
      }
    }
  }

  if (OB_UNLIKELY(!is_opened_)) {
    reset();
  }
  return ret;
}

template<typename PrefetchType>
inline int ObSSTableRowScanner<PrefetchType>::init_micro_scanner()
{
  int ret = OB_SUCCESS;
#define INIT_MICRO_DATA_SCANNER(ptr, type)                                                \
  do {                                                                                    \
    if (ptr == nullptr) {                                                                 \
      if (OB_ISNULL(ptr = OB_NEWx(type, long_life_allocator_, *long_life_allocator_))) {  \
        ret = OB_ALLOCATE_MEMORY_FAILED;                                                  \
        LOG_WARN("Failed to alloc memory for scanner", K(ret));                           \
      } else if (OB_FAIL(ptr->init(*iter_param_, *access_ctx_, sstable_))) {              \
        LOG_WARN("Fail to init micro scanner", K(ret));                                   \
      }                                                                                   \
    } else if (OB_LIKELY(!ptr->is_valid())) {                                             \
      if (OB_FAIL(ptr->switch_context(*iter_param_, *access_ctx_, sstable_))) {           \
        LOG_WARN("Failed to switch micro scanner", K(ret), KPC(ptr), KPC_(iter_param));   \
      }                                                                                   \
    }                                                                                     \
    if (OB_SUCC(ret)) {                                                                   \
      micro_scanner_ = ptr;                                                               \
    }                                                                                     \
  } while(0)

  if (sstable_->is_multi_version_minor_sstable()) {
    INIT_MICRO_DATA_SCANNER(mv_micro_data_scanner_, ObMultiVersionMicroBlockRowScanner);
  } else {
    INIT_MICRO_DATA_SCANNER(micro_data_scanner_, ObMicroBlockRowScanner);
  }
#undef INIT_MICRO_DATA_SCANNER

  return ret;
}

template<typename PrefetchType>
inline int ObSSTableRowScanner<PrefetchType>::open_cur_data_block(ObSSTableReadHandle &read_handle)
{
  int ret = OB_SUCCESS;
  if (prefetcher_.cur_micro_data_fetch_idx_ < read_handle.micro_begin_idx_ ||
      prefetcher_.cur_micro_data_fetch_idx_ > read_handle.micro_end_idx_) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument", K(ret), K(prefetcher_), K(read_handle));
  } else {
    blocksstable::ObMicroIndexInfo &micro_info = prefetcher_.current_micro_info();
    ObMicroBlockDataHandle &micro_handle = prefetcher_.current_micro_handle();
    if (nullptr == micro_scanner_) {
      if (OB_FAIL(init_micro_scanner())) {
        LOG_WARN("fail to init micro scanner", K(ret));
      }
    }

    if (OB_SUCC(ret)) {
      // init scanner
      if (prefetcher_.cur_micro_data_fetch_idx_ == read_handle.micro_begin_idx_ &&
          cur_range_idx_ != read_handle.range_idx_) {
        LOG_DEBUG("[INDEX BLOCK] begin to init micro block scanner", K(ret), K(read_handle),
                  K(prefetcher_.cur_micro_data_fetch_idx_), K(cur_range_idx_));
        micro_scanner_->reuse();
        if (OB_FAIL(micro_scanner_->set_range(*read_handle.range_))) {
          LOG_WARN("Fail to init micro scanner", K(ret), K(read_handle));
        } else {
          cur_range_idx_ = read_handle.range_idx_;
        }
      }
    }

    if (OB_SUCC(ret)) {
      bool can_blockscan = false;
      ObMicroBlockData block_data;
      if (OB_UNLIKELY(has_advance_scan_helper() && nullptr != micro_scanner_->get_reader()) &&
          OB_FAIL(advance_scan_helper_->filter_index_node(
              micro_info, advance_scan_state_, micro_info.advance_scan_state_))) {
          LOG_WARN("fail to skip endkey", K(ret));
      } else if (OB_FAIL(ret) || micro_info.advance_scan_state_.is_before_range()) {
      } else if (OB_FAIL(micro_handle.get_micro_block_data(&macro_block_reader_, block_data))) {
        LOG_WARN("Fail to get block data", K(ret), K(micro_handle));
      } else if (OB_FAIL(micro_scanner_->open(
                  micro_handle.macro_block_id_,
                  block_data,
                  micro_info.is_left_border(),
                  micro_info.is_right_border()))) {
        LOG_WARN("Fail to open micro_scanner", K(ret), K(micro_info), K(micro_handle), KPC(this));
      } else if (OB_FAIL(prefetcher_.check_blockscan(can_blockscan))) {
        LOG_WARN("Fail to check_blockscan", K(ret));
      } else if (can_blockscan && nullptr != block_row_store_ && !block_row_store_->is_disabled()) {
        // Apply pushdown filter and block scan
        sql::ObPushdownFilterExecutor *filter = block_row_store_->get_pd_filter();
        ObSampleFilterExecutor *sample_executor = static_cast<ObSampleFilterExecutor *>(access_ctx_->get_sample_executor());
        if (nullptr != sample_executor && sstable_->is_major_sstable()) {
          sample_executor->set_block_row_range(prefetcher_.cur_micro_data_fetch_idx_,
                                               micro_scanner_->get_current_pos(),
                                               micro_scanner_->get_last_pos(),
                                               prefetcher_.current_micro_info().get_row_count());
        }
        if (nullptr != filter) {
          micro_info.pre_process_filter(*filter);
        }
        if (OB_FAIL(micro_scanner_->apply_filter(can_blockscan))) {
          if (OB_UNLIKELY(OB_ITER_END != ret)) {
            LOG_WARN("Fail to apply filter", K(ret));
          } else {
            ret = OB_SUCCESS;
          }
        }
        if (nullptr != filter) {
          micro_info.post_process_filter(*filter);
        }
        if (nullptr != sample_executor && can_batch_scan()) {
          if (sstable_->is_major_sstable()) {
          } else if (OB_FAIL(sample_executor->increase_row_num(micro_scanner_->get_access_cnt()))) {
            LOG_WARN("Failed to increase row num in sample filter", KPC_(micro_scanner), KPC(sample_executor));
          }
        }
        ++access_ctx_->table_store_stat_.pushdown_micro_access_cnt_;
        EVENT_INC(ObStatEventIds::BLOCKSCAN_BLOCK_CNT);
        EVENT_ADD(ObStatEventIds::BLOCKSCAN_ROW_CNT, micro_scanner_->get_access_cnt());
        LOG_TRACE("[PUSHDOWN] pushdown for block scan", K(prefetcher_.cur_micro_data_fetch_idx_), K(micro_info), KPC(block_row_store_));
      }
      if (OB_SUCC(ret)) {
        if (OB_UNLIKELY(has_advance_scan_helper() && !micro_info.advance_scan_state_.is_before_range() &&
                        OB_FAIL(advance_scan_helper_->seek_to_range(*micro_scanner_, micro_info, true/*first*/)))) {
          LOG_WARN("Fail to skip rows", K(ret));
        } else {
          access_ctx_->inc_micro_access_cnt();
          REALTIME_MONITOR_ADD_SSSTORE_READ_BYTES(access_ctx_, micro_scanner_->get_data_length());
        }
        LOG_DEBUG("Success to open micro block", K(ret), K(read_handle), K(prefetcher_.cur_micro_data_fetch_idx_),
                  K(micro_info), K(micro_handle), KPC(this), K(common::lbt()));
      }
    }
  }
  return ret;
}


template<typename PrefetchType>
inline int ObSSTableRowScanner<PrefetchType>::inner_get_next_row(const ObDatumRow *&store_row)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_opened_)) {
    ret = OB_NOT_INIT;
   LOG_WARN("ObSSTableRowScanner has not been opened", K(ret), KPC(this));
  } else if (can_batch_scan()) {
    ret = OB_PUSHDOWN_STATUS_CHANGED;
  } else {
    while(OB_SUCC(ret)) {
      if (OB_FAIL(prefetcher_.prefetch())) {
        LOG_WARN("Fail to prefetch micro block", K(ret), KPC(this));
      } else if (prefetcher_.cur_range_fetch_idx_ >= prefetcher_.cur_range_prefetch_idx_) {
        if (OB_LIKELY(prefetcher_.is_prefetch_end_)) {
          ret = OB_ITER_END;
          if (nullptr != micro_scanner_) {
            micro_scanner_->reset_blockscan();
          }
        } else {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("Current fetch handle idx exceed prefetching idx", K(ret), KPC(this));
        }
      } else if (prefetcher_.read_wait()) {
        continue;
      } else if (OB_FAIL(fetch_row(prefetcher_.current_read_handle(), store_row))) {
        if (OB_LIKELY(OB_ITER_END == ret)) {
          if (prefetcher_.cur_range_fetch_idx_ < prefetcher_.prefetching_range_idx() || prefetcher_.is_prefetch_end_) {
            ++prefetcher_.cur_range_fetch_idx_;
          }
          ret = OB_SUCCESS;
        } else if (OB_UNLIKELY(OB_PUSHDOWN_STATUS_CHANGED != ret)) {
          LOG_WARN("Fail to fetch row", K(ret), KPC(this));
        }
      } else {
        break;
      }
    }
  }
  if (OB_SUCC(ret) && NULL != store_row) {
    ObDatumRow &datum_row = *const_cast<ObDatumRow *>(store_row);
    if (!store_row->row_flag_.is_not_exist() &&
      iter_param_->need_scn_ &&
      OB_FAIL(set_row_scn(access_ctx_->use_fuse_row_cache_, *iter_param_, store_row))) {
      LOG_WARN("failed to set row scn", K(ret), KPC(this));
    }
    EVENT_INC(ObStatEventIds::SSSTORE_READ_ROW_COUNT);
    if (OB_NOT_NULL(sstable_)) {
      if (sstable_->is_minor_sstable()) {
        EVENT_INC(ObStatEventIds::MINOR_SSSTORE_READ_ROW_COUNT);
      } else if (sstable_->is_major_sstable()) {
        EVENT_INC(ObStatEventIds::MAJOR_SSSTORE_READ_ROW_COUNT);
      }
    }
    LOG_DEBUG("[INDEX BLOCK] inner get next row", KPC(store_row), KPC(this));
  }
  LOG_DEBUG("chaser debug", K(ret), KPC(store_row), KPC(this));
  return ret;
}

template<typename PrefetchType>
inline int ObSSTableRowScanner<PrefetchType>::fetch_row(ObSSTableReadHandle &read_handle, const ObDatumRow *&store_row)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(read_handle.is_get_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Unexpected get in scan", K(ret), K(read_handle));
  } else if (-1 == read_handle.micro_begin_idx_) {
    // empty range
    ret = OB_ITER_END;
    LOG_DEBUG("[INDEX BLOCK] scan empty read handle", K(prefetcher_), K(read_handle));
  } else {
    bool need_open_micro = false;
    if (-1 == prefetcher_.cur_micro_data_fetch_idx_ ||
        cur_range_idx_ != read_handle.range_idx_) {
      LOG_DEBUG("[INDEX BLOCK] begin to fetch row", K(cur_range_idx_),
                K(prefetcher_.cur_micro_data_fetch_idx_), K(read_handle));
      prefetcher_.cur_micro_data_fetch_idx_ = read_handle.micro_begin_idx_;
      need_open_micro = true;
    }
    if (need_open_micro) {
      if (OB_FAIL(open_cur_data_block(read_handle))) {
        if (OB_UNLIKELY(OB_ITER_END != ret)) {
          LOG_WARN("Fail to open cur data block", K(ret), KPC(this));
        }
      } else if (can_batch_scan()) {
        ret = OB_PUSHDOWN_STATUS_CHANGED;
        LOG_TRACE("[Vectorized|Aggregate] pushdown status changed, fuse=>pushdown", K(ret),
                  K(prefetcher_.cur_micro_data_fetch_idx_));
      }
    }

    while (OB_SUCC(ret)) {
      if (has_advance_scan_helper_and_needs_seek() &&
          OB_FAIL(advance_scan_helper_->seek_to_range(*micro_scanner_, prefetcher_.current_micro_info()))) {
        LOG_WARN("Failed to seek to range", K(ret), KPC(advance_scan_helper_),
                 K(prefetcher_.current_micro_info().advance_scan_state_));
      } else if (OB_FAIL(micro_scanner_->get_next_row(store_row))) {
        if (OB_UNLIKELY(OB_ITER_END != ret)) {
          LOG_WARN("Fail to get next row", K(ret));
        } else if (prefetcher_.cur_micro_data_fetch_idx_ >= read_handle.micro_end_idx_) {
          ret = OB_ITER_END;
          if (ObStoreRowIterator::IteratorRowLockAndDuplicationCheck == type_ ||
              ObStoreRowIterator::IteratorRowLockCheck == type_) {
            ObMicroBlockRowLockChecker *checker = static_cast<ObMicroBlockRowLockChecker *>(micro_scanner_);
            checker->inc_empty_read(read_handle);
          }
          LOG_DEBUG("[INDEX BLOCK] Open data block handle iter end", K(ret),
                    K(prefetcher_.cur_micro_data_fetch_idx_), K(read_handle));
        } else if (FALSE_IT(prefetcher_.inc_cur_micro_data_fetch_idx())) {
        } else if (OB_FAIL(open_cur_data_block(read_handle))) {
          if (OB_UNLIKELY(OB_ITER_END != ret)) {
            LOG_WARN("Fail to open cur data block", K(ret), KPC(this));
          }
        } else if (can_batch_scan()) {
          ret = OB_PUSHDOWN_STATUS_CHANGED;
          LOG_TRACE("[Vectorized|Aggregate] pushdown status changed, fuse=>pushdown", K(ret),
                    K(prefetcher_.cur_micro_data_fetch_idx_));
        }
      } else {
        (const_cast<ObDatumRow*> (store_row))->scan_index_ = read_handle.range_idx_;
        break;
      }
    }
  }
  return ret;
}

template<typename PrefetchType>
inline int ObSSTableRowScanner<PrefetchType>::refresh_blockscan_checker(const blocksstable::ObDatumRowkey &rowkey)
{
  int ret = OB_SUCCESS;
  if (nullptr != block_row_store_ &&
      OB_FAIL(prefetcher_.refresh_blockscan_checker(prefetcher_.cur_micro_data_fetch_idx_ + 1, rowkey))) {
    LOG_WARN("Failed to prepare blockscan check info", K(ret), K(rowkey), KPC(this));
  }
  return ret;
}

template<typename PrefetchType>
inline int ObSSTableRowScanner<PrefetchType>::get_next_rows()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_opened_ || nullptr == block_row_store_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("The ObSSTableRowScanner has not been opened or init", K(ret), K_(is_opened), KP_(block_row_store), KPC(this));
  } else {
    while (OB_SUCC(ret) && !block_row_store_->is_end()) {
      // scan macro blocks
      if (OB_FAIL(prefetcher_.prefetch())) {
        LOG_WARN("Fail to do prefetch", K(ret), KPC(this));
      } else if (prefetcher_.cur_range_fetch_idx_ >= prefetcher_.cur_range_prefetch_idx_) {
        if (OB_LIKELY(prefetcher_.is_prefetch_end_)) {
          ret = OB_ITER_END;
          if (nullptr != micro_scanner_) {
            micro_scanner_->reset_blockscan();
          }
        } else {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("Current fetch handle idx exceed prefetching idx", K(ret), KPC(this));
        }
      } else if (prefetcher_.read_wait()) {
        continue;
      } else if (OB_FAIL(fetch_rows(prefetcher_.current_read_handle()))) {
        if (OB_ITER_END == ret) {
          if (prefetcher_.cur_range_fetch_idx_ < prefetcher_.prefetching_range_idx() || prefetcher_.is_prefetch_end_) {
            ++prefetcher_.cur_range_fetch_idx_;
          }
          ret = OB_SUCCESS;
        } else if (OB_UNLIKELY(OB_PUSHDOWN_STATUS_CHANGED != ret)) {
          LOG_WARN("Fail to fetch row", K(ret), KPC(this));
        }
      } else {
        // block scan is not effective or vector store ended
        break;
      }
    }
  }
  return ret;
}

template<typename PrefetchType>
inline int ObSSTableRowScanner<PrefetchType>::fetch_rows(ObSSTableReadHandle &read_handle)
{
  int ret = OB_SUCCESS;
  if (-1 == read_handle.micro_begin_idx_) {
    // empty range
    ret = OB_ITER_END;
    LOG_DEBUG("[INDEX BLOCK] scan empty read handle", K(prefetcher_), K(read_handle));
  } else {
    bool need_open_micro = false;
    if (-1 == prefetcher_.cur_micro_data_fetch_idx_ ||
        cur_range_idx_ != read_handle.range_idx_) {
      LOG_DEBUG("[INDEX BLOCK] begin to fetch row", K(cur_range_idx_),
                K(prefetcher_.cur_micro_data_fetch_idx_), K(read_handle));
      prefetcher_.cur_micro_data_fetch_idx_ = read_handle.micro_begin_idx_;
      need_open_micro = true;
    }
    if (need_open_micro) {
      if (OB_FAIL(open_cur_data_block(read_handle))) {
        if (OB_UNLIKELY(OB_ITER_END != ret)) {
          LOG_WARN("Fail to open cur data block", K(ret), KPC(this));
        }
      } else if (!can_batch_scan()) {
        ret = OB_PUSHDOWN_STATUS_CHANGED;
        LOG_TRACE("[Vectorized] pushdown status changed, pushdown=>fuse", K(ret),
                  K(prefetcher_.cur_micro_data_fetch_idx_));
      }
    }

    bool need_prefetch = false;
    while (OB_SUCC(ret) && !block_row_store_->is_end()) {
      if (OB_SUCCESS == micro_scanner_->end_of_block() && !can_batch_scan()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Unexpected scan status", K(ret), KPC(this));
      }  else if (has_advance_scan_helper_and_needs_seek() &&
                  OB_FAIL(advance_scan_helper_->seek_to_range(*micro_scanner_, prefetcher_.current_micro_info()))) {
        LOG_WARN("Failed to seek to range", K(ret), KPC(advance_scan_helper_),
                 K(prefetcher_.current_micro_info().advance_scan_state_));
      } else if (OB_FAIL(micro_scanner_->get_next_rows())) {
        if (OB_UNLIKELY(OB_ITER_END != ret)) {
          LOG_WARN("Fail to get next row", K(ret));
        } else if (prefetcher_.cur_micro_data_fetch_idx_ >= read_handle.micro_end_idx_) {
          ret = OB_ITER_END;
          LOG_DEBUG("[INDEX BLOCK] Open data block handle iter end", K(ret),
                    K(prefetcher_.cur_micro_data_fetch_idx_), K(read_handle));
        } else if (FALSE_IT(prefetcher_.inc_cur_micro_data_fetch_idx())) {
        } else if (OB_FAIL(open_cur_data_block(read_handle))) {
          if (OB_UNLIKELY(OB_ITER_END != ret)) {
            LOG_WARN("Fail to open cur data block", K(ret), KPC(this));
          }
        } else if (need_prefetch && OB_FAIL(prefetcher_.prefetch())) {
          LOG_WARN("Fail to do prefetch", K(ret), K_(prefetcher));
        } else if (!can_batch_scan()) {
          ret = OB_PUSHDOWN_STATUS_CHANGED;
          LOG_TRACE("[Vectorized] pushdown status changed, pushdown=>fuse", K(ret),
                    K(prefetcher_.cur_micro_data_fetch_idx_));
        } else {
          // should do prefetch as all the prefetched micros may be read
          need_prefetch = iter_param_->enable_pd_aggregate();
        }
      }
    }

  }
  return ret;
}


template<typename PrefetchType>
inline int ObSSTableRowScanner<PrefetchType>::get_next_rowkey(const bool need_set_border_rowkey,
                                                       int64_t &curr_scan_index,
                                                       blocksstable::ObDatumRowkey& rowkey,
                                                       blocksstable::ObDatumRowkey &border_rowkey,
                                                       common::ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  const ObDatumRow *row = nullptr;
  ObDatumRowkey tmp_rowkey;
  border_rowkey.reset();
  rowkey.reset();
  block_row_store_->disable();

  // get next row
  if (OB_FAIL(get_next_row(row))) {
    if (OB_UNLIKELY(OB_ITER_END != ret)) {
      LOG_WARN("Failed to get next row from iterator", K(ret), KPC(this));
    } else {
      // range_idx_ maybe -1 for empty range
      curr_scan_index = MAX(cur_range_idx_, 0);
      if (access_ctx_->query_flag_.is_reverse_scan()) {
        rowkey.set_min_rowkey();
      } else {
        rowkey.set_max_rowkey();
      }
      ret = OB_SUCCESS;
    }
  } else if (OB_FAIL(tmp_rowkey.assign(row->storage_datums_, iter_param_->get_schema_rowkey_count()))) {
    LOG_WARN("assign rowkey failed", K(ret), K(row), K(iter_param_->get_schema_rowkey_count()));
  } else if (OB_UNLIKELY(!tmp_rowkey.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tmp_rowkey is not valid", K(ret), K(tmp_rowkey));
  } else if (OB_FAIL(tmp_rowkey.deep_copy(rowkey, allocator))) {
    LOG_WARN("fail to deep copy rowkey", K(ret), K(tmp_rowkey));
  } else {
    curr_scan_index = cur_range_idx_;
  }

  if (OB_SUCC(ret) && need_set_border_rowkey) {
    border_rowkey = prefetcher_.get_border_rowkey();
  }
  return ret;
}

// Explicit instantiations.
template class ObSSTableRowScanner<ObIndexTreeMultiPassPrefetcher<32, 3>>;
template class ObSSTableRowScanner<ObIndexTreeMultiPassPrefetcher<2, 2>>;

}
}
