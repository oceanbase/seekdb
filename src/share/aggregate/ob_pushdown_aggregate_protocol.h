/*
 * Copyright (c) 2026 OceanBase.
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

#ifndef OCEANBASE_SHARE_AGGREGATE_OB_PUSHDOWN_AGGREGATE_PROTOCOL_H_
#define OCEANBASE_SHARE_AGGREGATE_OB_PUSHDOWN_AGGREGATE_PROTOCOL_H_

#include <stdint.h>

namespace oceanbase
{
namespace common
{
class ObIAllocator;
struct ObDatum;
}
namespace share
{
namespace aggregate
{

// A slot is an ordinal in one canonical input list:
//
//   [ grouping inputs in declaration order,
//     aggregate inputs in declaration order ]
//
// It is not a schema column id and deliberately carries no SQL expression or
// physical decoder information.  Query programs compile against this order;
// the storage adapter owns the mapping from an ordinal to a physical column.
typedef int32_t ObAggregateInputSlot;

enum ObAggregateSelectionKind : uint8_t
{
  AGG_SELECT_DENSE = 0,
  AGG_SELECT_ROW_IDS,
  AGG_SELECT_BITMAP
};

// All pointers in protocol views are borrowed from the input segment.  They
// remain valid only until the next non-const call on that segment, or until
// consume() returns, whichever happens first.  A program that keeps a value
// after the current segment call must deep-copy it before making any other
// call on the segment.
struct ObAggregateSelectionView
{
  ObAggregateSelectionView()
    : kind_(AGG_SELECT_DENSE),
      begin_(0),
      count_(0),
      row_ids_(nullptr),
      bitmap_words_(nullptr),
      bitmap_word_count_(0),
      reverse_(false)
  {}

  ObAggregateSelectionKind kind_;
  int64_t begin_;
  int64_t count_;
  const int32_t *row_ids_;
  const uint64_t *bitmap_words_;
  int64_t bitmap_word_count_;
  bool reverse_;
};

enum ObAggregateReductionField : uint32_t
{
  AGG_REDUCE_NONE = 0,
  AGG_REDUCE_ROW_COUNT = 1U << 0,
  AGG_REDUCE_NULL_COUNT = 1U << 1,
  AGG_REDUCE_MIN = 1U << 2,
  AGG_REDUCE_MAX = 1U << 3,
  AGG_REDUCE_SUM = 1U << 4,
  AGG_REDUCE_LOGICAL_BYTES = 1U << 5
};

// A reduction contains physical, exact facts about the selected rows.  It is
// not a SQL aggregate result.  In particular, SQL NULL/type/overflow semantics
// are interpreted by the query-owned program.
struct ObAggregateReduction
{
  ObAggregateReduction()
    : present_(AGG_REDUCE_NONE),
      row_count_(0),
      null_count_(0),
      logical_bytes_(0),
      min_(nullptr),
      max_(nullptr),
      sum_(nullptr),
      min_is_prefix_(false),
      max_is_prefix_(false)
  {}

  uint32_t present_;
  int64_t row_count_;
  int64_t null_count_;
  int64_t logical_bytes_;
  const common::ObDatum *min_;
  const common::ObDatum *max_;
  const common::ObDatum *sum_;
  bool min_is_prefix_;
  bool max_is_prefix_;
};

struct ObAggregateValueBatchView
{
  ObAggregateValueBatchView() : datums_(nullptr), count_(0) {}
  const common::ObDatum *datums_;
  int64_t count_;
};

struct ObAggregateDictionaryView
{
  ObAggregateDictionaryView()
    : keys_(), refs_(nullptr), row_count_(0), null_key_index_(-1)
  {}
  ObAggregateValueBatchView keys_;
  const uint32_t *refs_;
  int64_t row_count_;
  int32_t null_key_index_;
};

// Storage implements this interface for one physical segment (row, decoded
// batch, encoded micro block, or exact index summary).  OB_NOT_SUPPORTED from
// try_reduce()/try_dictionary() means that representation is unavailable and
// must not mutate the program or the output view.
class ObIAggregateInputSegment
{
public:
  virtual ~ObIAggregateInputSegment() = default;
  virtual const ObAggregateSelectionView &selection() const = 0;
  // A side-effect-free capability probe.  It must not decode values, allocate
  // scratch memory, or invalidate a previously returned protocol view.
  virtual int can_read_values(
      const ObAggregateInputSlot slot,
      bool &can_read) const = 0;
  virtual int try_reduce(
      const ObAggregateInputSlot slot,
      const uint32_t requested,
      ObAggregateReduction &reduction) = 0;
  virtual int read_values(
      const ObAggregateInputSlot slot,
      ObAggregateValueBatchView &values) = 0;
  virtual int try_dictionary(
      const ObAggregateInputSlot slot,
      ObAggregateDictionaryView &dictionary) = 0;
};

enum ObPushdownAggregateProgramState : uint8_t
{
  AGG_PROGRAM_NEW = 0,
  AGG_PROGRAM_CONSUMING,
  AGG_PROGRAM_SEALED,
  AGG_PROGRAM_EMITTING,
  AGG_PROGRAM_END,
  AGG_PROGRAM_FAILED
};

struct ObAggregateEmitResult
{
  ObAggregateEmitResult() : row_count_(0), end_(false) {}
  int64_t row_count_;
  bool end_;
};

// Query owns the implementation and all SQL semantics/materialization.  The
// program is non-copyable, thread-confined, and must outlive every segment
// passed to consume().  A hard consume/seal/emit error poisons the program;
// callers may only reset_scan() or destroy it afterwards.
class ObIPushdownAggregateProgram
{
public:
  virtual ~ObIPushdownAggregateProgram() = default;
  // Implementations are allocator-owned and may have different concrete
  // types.  Query ownership invokes this virtual hook; storage never does.
  virtual void destroy(common::ObIAllocator &allocator) = 0;
  virtual ObPushdownAggregateProgramState state() const = 0;
  virtual int reset_scan() = 0;
  // Probe whether the segment can provide every exact physical fact required
  // by this program.  The program state and aggregate values must not change.
  virtual int can_consume(ObIAggregateInputSegment &segment, bool &can_consume) = 0;
  virtual int consume(ObIAggregateInputSegment &segment) = 0;
  virtual int seal() = 0;
  virtual int emit(const int64_t max_rows, ObAggregateEmitResult &result) = 0;
};

} // namespace aggregate
} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_AGGREGATE_OB_PUSHDOWN_AGGREGATE_PROTOCOL_H_
