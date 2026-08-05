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

#ifndef OCEANBASE_STORAGE_ACCESS_OB_PUSHDOWN_AGGREGATE_INPUT_H_
#define OCEANBASE_STORAGE_ACCESS_OB_PUSHDOWN_AGGREGATE_INPUT_H_

#include "common/datum/ob_datum.h"
#include "lib/allocator/page_arena.h"
#include "share/aggregate/ob_pushdown_aggregate_protocol.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObColumnParam;
}
}
namespace blocksstable
{
class ObDatumRow;
class ObIMicroBlockReader;
struct ObMicroIndexInfo;
}
namespace storage
{
struct ObTableIterParam;

// Storage-owned adapter for one exact physical input segment.  Canonical input
// slots are mapped through group_by_cols_project_ followed by agg_cols_project_
// here, at the edge; the query-owned program never receives storage projectors
// or schema objects.
class ObPushdownAggregateInput final
  : public share::aggregate::ObIAggregateInputSegment
{
public:
  ObPushdownAggregateInput(
      const ObTableIterParam &iter_param,
      blocksstable::ObIMicroBlockReader *reader,
      const int32_t *row_ids,
      const int64_t row_count,
      const bool is_padding_mode);
  ObPushdownAggregateInput(
      const ObTableIterParam &iter_param,
      const blocksstable::ObDatumRow &row,
      const bool is_padding_mode);
  ObPushdownAggregateInput(
      const ObTableIterParam &iter_param,
      const blocksstable::ObMicroIndexInfo &index_info,
      const bool is_padding_mode);

  const share::aggregate::ObAggregateSelectionView &selection() const override
  { return selection_; }
  int can_read_values(
      const share::aggregate::ObAggregateInputSlot slot,
      bool &can_read) const override;
  int try_reduce(
      const share::aggregate::ObAggregateInputSlot slot,
      const uint32_t requested,
      share::aggregate::ObAggregateReduction &reduction) override;
  int read_values(
      const share::aggregate::ObAggregateInputSlot slot,
      share::aggregate::ObAggregateValueBatchView &values) override;
  int try_dictionary(
      const share::aggregate::ObAggregateInputSlot slot,
      share::aggregate::ObAggregateDictionaryView &dictionary) override;

private:
  enum InputKind : uint8_t
  {
    INPUT_READER = 0,
    INPUT_ROW,
    INPUT_INDEX
  };

  int get_input_column(
      const share::aggregate::ObAggregateInputSlot slot,
      int32_t &col_offset,
      int32_t &col_index,
      const share::schema::ObColumnParam *&col_param) const;
  int get_null_count(
      const int32_t col_offset,
      const int32_t col_index,
      const share::schema::ObColumnParam &col_param,
      int64_t &null_count) const;
  int read_index_extreme(
      const int32_t col_offset,
      const int32_t col_index,
      const share::schema::ObColumnParam &col_param,
      const bool read_min,
      common::ObDatum &datum,
      bool &is_prefix);
  int normalize_value(
      const share::schema::ObColumnParam &col_param,
      common::ObDatum &datum);
  int prepare_value_buffer(const int64_t value_count);

private:
  const ObTableIterParam &iter_param_;
  InputKind kind_;
  blocksstable::ObIMicroBlockReader *reader_;
  const int32_t *row_ids_;
  const blocksstable::ObDatumRow *row_;
  const blocksstable::ObMicroIndexInfo *index_info_;
  share::aggregate::ObAggregateSelectionView selection_;
  bool is_padding_mode_;
  // Every value view is backed by adapter-owned scratch.  Reusing this arena
  // on the next non-const call implements the protocol's explicit lifetime.
  common::ObArenaAllocator value_allocator_;
  common::ObDatum *value_datums_;
  // Index reductions borrow payload bytes from index_info_, but the view
  // objects themselves must outlive try_reduce().
  common::ObDatum min_datum_;
  common::ObDatum max_datum_;
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_ACCESS_OB_PUSHDOWN_AGGREGATE_INPUT_H_
