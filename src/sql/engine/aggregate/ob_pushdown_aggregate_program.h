/*
 * Copyright (c) 2026 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#ifndef OCEANBASE_SQL_ENGINE_AGGREGATE_OB_PUSHDOWN_AGGREGATE_PROGRAM_H_
#define OCEANBASE_SQL_ENGINE_AGGREGATE_OB_PUSHDOWN_AGGREGATE_PROGRAM_H_

#include "lib/container/ob_iarray.h"
#include "lib/container/ob_se_array.h"
#include "share/aggregate/ob_pushdown_aggregate_protocol.h"

namespace oceanbase
{
namespace common
{
class ObIAllocator;
}
namespace sql
{
class ObEvalCtx;
class ObExpr;

// Query-semantic description of one COUNT input.  The slot is the aggregate
// expression ordinal; storage owns the mapping from this ordinal to its
// physical projector.  Keeping the mapping out of the program is what lets
// the same program consume rows, decoded batches, or exact index summaries.
struct ObCountPushdownInputSpec
{
  ObCountPushdownInputSpec() : slot_(-1), exclude_null_(false) {}
  ObCountPushdownInputSpec(const int32_t slot, const bool exclude_null)
    : slot_(slot), exclude_null_(exclude_null)
  {}
  TO_STRING_KV(K_(slot), K_(exclude_null));

  share::aggregate::ObAggregateInputSlot slot_;
  bool exclude_null_;
};

// SQL-owned COUNT state machine shared by production materialization and
// protocol-level tests.  consume() performs no allocation after init() and is
// failure atomic: a segment either contributes every COUNT delta or none.
class ObCountPushdownAggregateProgramBase
  : public share::aggregate::ObIPushdownAggregateProgram
{
public:
  ObCountPushdownAggregateProgramBase();
  ~ObCountPushdownAggregateProgramBase() override = default;

  int init(const common::ObIArray<ObCountPushdownInputSpec> &inputs);
  share::aggregate::ObPushdownAggregateProgramState state() const override;
  int reset_scan() override;
  int can_consume(
      share::aggregate::ObIAggregateInputSegment &segment,
      bool &can_consume) override;
  int consume(share::aggregate::ObIAggregateInputSegment &segment) override;
  int seal() override;
  int emit(
      const int64_t max_rows,
      share::aggregate::ObAggregateEmitResult &result) override;

protected:
  virtual int materialize_counts(const common::ObIArray<int64_t> &counts) = 0;
  const common::ObIArray<int64_t> &accumulated_counts() const { return counts_; }

private:
  int validate_reduction(
      const uint32_t requested,
      const share::aggregate::ObAggregateReduction &reduction) const;

private:
  common::ObSEArray<ObCountPushdownInputSpec, 4> inputs_;
  common::ObSEArray<int64_t, 4> counts_;
  common::ObSEArray<int64_t, 4> deltas_;
  share::aggregate::ObPushdownAggregateProgramState state_;
};

// Creates the production SQL implementation for the subset supported by the
// new deep aggregate seam.  OB_NOT_SUPPORTED is a clean dispatch miss: the
// caller must keep using the legacy path and program remains null.
int create_pushdown_aggregate_program(
    ObEvalCtx &eval_ctx,
    const common::ObIArray<ObExpr *> &aggregate_exprs,
    const bool rich_format,
    common::ObIAllocator &allocator,
    share::aggregate::ObIPushdownAggregateProgram *&program);

void destroy_pushdown_aggregate_program(
    common::ObIAllocator &allocator,
    share::aggregate::ObIPushdownAggregateProgram *&program);

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_ENGINE_AGGREGATE_OB_PUSHDOWN_AGGREGATE_PROGRAM_H_
