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

#ifndef OB_STORAGE_OB_AGGREGATED_STORE_H_
#define OB_STORAGE_OB_AGGREGATED_STORE_H_

#include "query/engine/expr/ob_expr.h"
#include "ob_block_batched_row_store.h"
#include "storage/blocksstable/ob_datum_row.h"
#include "storage/access/ob_pushdown_aggregate.h"
#include "storage/blocksstable/index_block/ob_index_block_row_struct.h"
#include "share/aggregate/ob_pushdown_aggregate_protocol.h"

namespace oceanbase
{
namespace blocksstable
{
class ObMicroBlockDecoder;
struct ObMicroIndexInfo;
}
namespace storage
{

static const int64_t AGG_ROW_MODE_COUNT_THRESHOLD = 3;
static const double AGG_ROW_MODE_RATIO_THRESHOLD = 0.5;

class ObAggRow
{
public:
  ObAggRow(common::ObIAllocator &allocator);
  ~ObAggRow();
  void reset();
  void reuse();
  int init(const ObTableAccessParam &param, const ObTableAccessContext &context, const int64_t batch_size);
  OB_INLINE int64_t get_agg_count() const { return agg_cells_.count(); }
  OB_INLINE int64_t get_dummy_agg_count() const { return dummy_agg_cells_.count(); }
  OB_INLINE bool has_lob_column_out() const { return has_lob_column_out_; }
  bool check_need_access_data();
  OB_INLINE ObAggCell* at(int64_t idx) { return agg_cells_.at(idx); }
  OB_INLINE ObAggCell* at_dummy(int64_t idx) { return dummy_agg_cells_.at(idx); }
  OB_INLINE common::ObIArray<ObAggCell*>& get_agg_cells() { return agg_cells_; }
  TO_STRING_KV(K_(agg_cells), K_(dummy_agg_cells), K_(can_use_index_info), K_(need_access_data), K_(has_lob_column_out));
private:
  common::ObFixedArray<ObAggCell *, common::ObIAllocator> agg_cells_;
  // TODO(yht146439) remove this after DAS eliminate unused output
  common::ObFixedArray<ObAggCell *, common::ObIAllocator> dummy_agg_cells_;
  bool can_use_index_info_;
  bool need_access_data_;
  bool has_lob_column_out_;
  common::ObIAllocator &allocator_;
  ObPDAggFactory agg_cell_factory_;
};

class ObAggregatedStore : public ObBlockBatchedRowStore, public ObAggStoreBase
{
public:
  ObAggregatedStore(
      const int64_t batch_size,
      sql::ObEvalCtx &eval_ctx,
      ObTableAccessContext &context);
  virtual ~ObAggregatedStore();
  virtual void reset() override;
  virtual void reuse() override;
  int reuse_capacity(const int64_t capacity) override;
  virtual int init(const ObTableAccessParam &param, common::hash::ObHashSet<int32_t> *agg_col_mask = nullptr) override;
  int fill_index_info(const blocksstable::ObMicroIndexInfo &index_info) override;
  virtual int fill_rows(
      const int64_t group_idx,
      blocksstable::ObIMicroBlockRowScanner &scanner,
      int64_t &begin_index,
      const int64_t end_index,
      const ObFilterResult &res) override;
  virtual int fill_rows(const int64_t group_idx, const int64_t row_count) override;
  virtual int fill_row(blocksstable::ObDatumRow &out_row) override;
  int collect_aggregated_result() override;
  int get_agg_cell(const sql::ObExpr *expr, ObAggCell *&agg_cell);
  int can_use_index_info(const blocksstable::ObMicroIndexInfo &index_info, bool &can_agg) override;
  ObAggStoreBase *get_agg_store() override { return this; }
  // OB_INLINE void set_end() override { iter_end_flag_ = IterEndState::ITER_END; }
  int check_agg_in_row_mode(const ObTableIterParam &iter_param);
  bool has_data();
  INHERIT_TO_STRING_KV("ObBlockBatchedRowStore", ObBlockBatchedRowStore,
                       K_(agg_row), K_(agg_flat_row_mode), KP_(aggregate_program));

protected:
  int on_scan_start() override;

private:
  ObAggRow agg_row_;
  bool agg_flat_row_mode_;
  blocksstable::ObDatumRow row_buf_;
  share::aggregate::ObIPushdownAggregateProgram *aggregate_program_;
};

} /* namespace storage */
} /* namespace oceanbase */

#endif /* OB_STORAGE_OB_AGGREGATED_STORE_H_ */
