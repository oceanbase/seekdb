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

#ifndef OB_STORAGE_OB_SSTABLE_ROW_SCANNER_H_
#define OB_STORAGE_OB_SSTABLE_ROW_SCANNER_H_

#include "storage/blocksstable/ob_micro_block_row_scanner.h"
#include "ob_index_tree_prefetcher.h"

namespace oceanbase {
using namespace blocksstable;
namespace storage {
template<typename PrefetchType = ObIndexTreeMultiPassPrefetcher<>>
class ObSSTableRowScanner : public ObStoreRowIterator
{
public:
  ObSSTableRowScanner() :
      ObStoreRowIterator(),
      is_opened_(false),
      sstable_(nullptr),
      iter_param_(nullptr),
      access_ctx_(nullptr),
      prefetcher_(),
      macro_block_reader_(),
      micro_scanner_(nullptr),
      micro_data_scanner_(nullptr),
      mv_micro_data_scanner_(nullptr),
      advance_scan_helper_(nullptr),
      advance_scan_state_(),
      cur_range_idx_(-1)
  {
    type_ = ObStoreRowIterator::IteratorScan;
  }
  virtual ~ObSSTableRowScanner();
  virtual void reset() override;
  virtual void reuse() override;
  virtual void reclaim() override;
  virtual int advance_scan(const blocksstable::ObDatumRange &range) override;
  virtual bool can_blockscan() const override;
  virtual bool can_batch_scan() const override;
  virtual int get_next_rowkey(const bool need_set_border_rowkey,
                              int64_t &curr_scan_index,
                              blocksstable::ObDatumRowkey& rowkey,
                              blocksstable::ObDatumRowkey &border_rowkey,
                              common::ObIAllocator &allocator) final;
  OB_INLINE bool is_end_of_scan() const
  {
    return prefetcher_.is_prefetch_end_ &&
        prefetcher_.cur_range_fetch_idx_ >= prefetcher_.cur_range_prefetch_idx_;
  }
  TO_STRING_KV(K_(is_opened), K_(cur_range_idx),
               KP_(micro_scanner), KP_(micro_data_scanner), KP_(mv_micro_data_scanner),
               KPC_(advance_scan_helper), K_(advance_scan_state), KP_(sstable), KP_(iter_param), KP_(access_ctx), K_(prefetcher));
protected:
  int inner_open(
      const ObTableIterParam &iter_param,
      ObTableAccessContext &access_ctx,
      ObITable *table,
      const void *query_range);
  virtual int inner_get_next_row(const ObDatumRow *&store_row) override;
  virtual int fetch_row(ObSSTableReadHandle &read_handle, const ObDatumRow *&store_row);
  virtual int refresh_blockscan_checker(const blocksstable::ObDatumRowkey &rowkey) override final;
  virtual int get_next_rows() override;
private:
  int init_micro_scanner();
  int open_cur_data_block(ObSSTableReadHandle &read_handle);
  int fetch_rows(ObSSTableReadHandle &read_handle);
  OB_INLINE bool has_advance_scan_helper() const
  {
    return nullptr != advance_scan_helper_;
  }
  OB_INLINE bool has_advance_scan_helper_and_inside_range(const ObMicroIndexInfo &index_info) const
  {
    return nullptr != advance_scan_helper_ && !index_info.advance_scan_state_.is_before_range();
  }
  OB_INLINE bool has_advance_scan_helper_and_needs_seek() const
  {
    return nullptr != advance_scan_helper_ && advance_scan_helper_->needs_range_seek();
  }

protected:
  bool is_opened_;
  ObSSTable *sstable_;
  const ObTableIterParam *iter_param_;
  ObTableAccessContext *access_ctx_;
  PrefetchType prefetcher_;
  ObMacroBlockReader macro_block_reader_;
  ObIMicroBlockRowScanner *micro_scanner_;
  ObMicroBlockRowScanner *micro_data_scanner_;
  ObMultiVersionMicroBlockRowScanner *mv_micro_data_scanner_;
  ObAdvanceScanHelper *advance_scan_helper_;
  ObAdvanceScanState advance_scan_state_;
private:
  int64_t cur_range_idx_;
};

}
}
#ifndef OCEABASE_SSTABLE_ROW_SCANNER_IPP
#define OCEABASE_SSTABLE_ROW_SCANNER_IPP
#include "ob_sstable_row_scanner.ipp"
#endif
#endif //OB_STORAGE_OB_SSTABLE_ROW_SCANNER_H_
