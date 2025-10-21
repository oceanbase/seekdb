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

#pragma once

#include "storage/direct_load/ob_direct_load_i_merge_task.h"
#include "storage/direct_load/ob_direct_load_lob_meta_row_iter.h"
#include "storage/ddl/ob_tablet_slice_row_iterator.h"

namespace oceanbase
{
namespace storage
{
class ObDirectLoadTabletMergeCtx;
class ObDirectLoadOriginTable;

class ObDirectLoadPartitionDelLobTask : public ObDirectLoadIMergeTask
{
public:
  ObDirectLoadPartitionDelLobTask();
  ~ObDirectLoadPartitionDelLobTask();
  int init(ObDirectLoadTabletMergeCtx *merge_ctx, ObDirectLoadOriginTable &origin_table,
           const ObDirectLoadTableDataDesc &table_data_desc,
           const ObDirectLoadTableHandleArray &sstable_array,
           const blocksstable::ObDatumRange &range, const blocksstable::ObMacroDataSeq &data_seq,
           const int64_t parallel_idx);
  int process() override;
  void stop() override;
  int init_iterator(ObITabletSliceRowIterator *&row_iterator) override;
  ObDirectLoadTabletMergeCtx *get_merge_ctx() override { return merge_ctx_; }
  TO_STRING_KV(KP_(merge_ctx), K_(sstable_array), KPC_(range), K_(data_seq), K_(parallel_idx));

private:
  class RowIterator : public ObITabletSliceRowIterator
  {
  public:
    RowIterator();
    virtual ~RowIterator();
    int init(ObDirectLoadTabletMergeCtx *merge_ctx, ObDirectLoadOriginTable &origin_table,
             const ObDirectLoadTableDataDesc &table_data_desc,
             const ObDirectLoadTableHandleArray &sstable_array,
             const blocksstable::ObDatumRange &range, int64_t parallel_idx);
    int get_next_row(const blocksstable::ObDatumRow *&row) override;
    int get_next_batch(const blocksstable::ObBatchDatumRows *&datum_rows) override
    {
      return OB_NOT_SUPPORTED;
    }
    int64_t get_slice_idx() const override { return parallel_idx_; }
    ObTabletID get_tablet_id() const override { return tablet_id_; }

  private:
    ObTabletID tablet_id_;
    int64_t parallel_idx_;
    ObDirectLoadLobMetaRowIter lob_iter_;
    blocksstable::ObDatumRow datum_row_;
    bool is_inited_;
  };

private:
  ObDirectLoadTabletMergeCtx *merge_ctx_;
  ObDirectLoadOriginTable *origin_table_;
  ObDirectLoadTableDataDesc table_data_desc_;
  ObDirectLoadTableHandleArray sstable_array_;
  const blocksstable::ObDatumRange *range_;
  blocksstable::ObMacroDataSeq data_seq_;
  int64_t parallel_idx_;
  bool is_inited_;
};

} // namespace storage
} // namespace oceanbase
