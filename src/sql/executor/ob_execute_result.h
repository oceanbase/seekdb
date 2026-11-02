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

#ifndef OCEANBASE_SQL_EXECUTOR_OB_EXECUTE_RESULT_
#define OCEANBASE_SQL_EXECUTOR_OB_EXECUTE_RESULT_

#include "common/row/ob_row.h"
#include "common/datum/datum_value.h"
#include "common/datum/datum.h"
#include "common/datum/sql_type.h"
#include "sql/engine/ob_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExecContext;
class ObPhyOperator;
class ObOperator;
class ObOpSpec;

// Borrowed until the next advance/close. Values stay in an expression frame or
// explicitly identified inline slots. Row-producing callers can
// supply an ObNewRow without changing their execution or cursor semantics.
class ResultRowView
{
public:
  ResultRowView() : legacy_(nullptr), exprs_(nullptr), ctx_(nullptr), count_(0), idx_(0),
    batch_(false), legacy_scratch_(nullptr), cell_metas_(nullptr) {}
  void set_legacy(const common::ObNewRow *row) { *this = ResultRowView(); legacy_ = row; }
  void set_datums(const ExprFixedArray &exprs, ObEvalCtx &ctx, int64_t idx, bool batch,
                  char *legacy_scratch, const common::SqlTypeMeta *cell_metas = nullptr)
  {
    *this = ResultRowView(); exprs_ = exprs.count() ? &exprs.at(0) : nullptr;
    ctx_ = &ctx; count_ = exprs.count(); idx_ = idx; batch_ = batch;
    legacy_scratch_ = legacy_scratch;
    cell_metas_ = cell_metas;
  }
  // Every execution row is native. The full type belongs to the corresponding
  // Expr, including Decimal precision, collation and complex-domain metadata.
  bool is_inline_datum_row() const { return is_datum_row(); }
  bool inline_row_valid() const { return is_datum_row(); }
  // Values are copied into caller storage; references still borrow the row owner.
  // Both outputs remain unchanged on failure. No Datum projection is requested.
  int get_inline_value(int64_t i, common::Datum &value, const common::SqlType *&type) const
  {
    int ret = OB_SUCCESS;
    if (!is_datum_row()) { ret = OB_STATE_NOT_MATCH; }
    else if (i < 0 || i >= count_) { ret = OB_SIZE_OVERFLOW; }
    else {
      common::Datum candidate;
      if (OB_SUCC(ret = expr(i).read_value(*ctx_, idx_, candidate))) {
        value = candidate;
        type = &expr(i).get_sql_type();
      }
    }
    return ret;
  }
  int get_legacy_value(int64_t i, common::ObDatum &value) const;
  // Project expression metadata without allocating or constructing a value object.
  // previous_meta is the per-column metadata left by the preceding result row.
  static int project_value(const common::ObDatum &datum, const common::SqlTypeMeta &meta,
                           common::SqlValueDatumMapType mapping,
                           const common::SqlTypeMeta &previous_meta,
                           common::DatumValue &value, bool keep_null_collation = false);
  int get_value(int64_t i, common::DatumValue &value) const;
  bool is_datum_row() const { return nullptr != ctx_; }
  bool has_legacy_datums() const { return is_datum_row() && legacy_scratch_ != nullptr; }
  const common::ObNewRow *legacy_row() const { return legacy_; }
  int64_t count() const { return is_datum_row() ? count_ : (legacy_ ? legacy_->get_count() : 0); }
  const ObExpr &expr(int64_t i) const { return *exprs_[i]; }
private:
  const common::ObNewRow *legacy_;
  ObExpr *const *exprs_;
  ObEvalCtx *ctx_;
  int64_t count_;
  int64_t idx_;
  bool batch_;
  // A compatibility descriptor may borrow these stable per-column scalar
  // bytes until the next row advance. Reference values keep the Expr lifetime.
  char *legacy_scratch_;
  // Per-column projection metadata belongs to the executor, independently of
  // the caller's output object. NULL retains the preceding row's scale.
  const common::SqlTypeMeta *cell_metas_;
};

class ObIExecuteResult
{
public:
  virtual ~ObIExecuteResult() {}

  virtual int open(ObExecContext &ctx) = 0;
  virtual int get_next_row(ObExecContext &ctx, const common::ObNewRow *&row) = 0;
  virtual int get_next_result_row(ObExecContext &ctx, ResultRowView &view)
  {
    const common::ObNewRow *row = nullptr;
    int ret = get_next_row(ctx, row);
    view.set_legacy(row);
    return ret;
  }
  // Row-producing callers forward their existing native cells; the static engine
  // overrides this entry and never eagerly materializes an ObNewRow.
  virtual int get_next_native_result_row(ObExecContext &ctx, ResultRowView &view)
  { return get_next_result_row(ctx, view); }
  virtual int close(ObExecContext &ctx) = 0;
};

class ObExecuteResult : public ObIExecuteResult
{
  friend class ObExecutor;
public:
  ObExecuteResult()
    : err_code_(OB_ERR_UNEXPECTED),
      static_engine_root_(NULL),
      legacy_cell_scratch_(nullptr),
      native_cell_metas_(nullptr),
      native_cell_count_(0),
      row_iteration_started_(false) {}
  virtual ~ObExecuteResult() {}

  virtual int open(ObExecContext &ctx) override;
  virtual int get_next_row(ObExecContext &ctx, const common::ObNewRow *&row) override;
  virtual int get_next_result_row(ObExecContext &ctx, ResultRowView &view) override;
  virtual int get_next_native_result_row(ObExecContext &ctx, ResultRowView &view) override;
  virtual int close(ObExecContext &ctx) override;

  inline int get_err_code() { return err_code_; }

  // interface for static typing engine
  int open() const;
  int get_next_row() const;
  int close() const;
  const ObOperator *get_static_engine_root() const { return static_engine_root_; }
  void set_static_engine_root(ObOperator *op)
  {
    static_engine_root_ = op;
    br_it_.set_operator(op);
    br_it_.rescan();
    row_.reset();
    legacy_cell_scratch_ = nullptr;
    native_cell_metas_ = nullptr;
    native_cell_count_ = 0;
    row_iteration_started_ = false;
  }

private:
  int err_code_;
  ObOperator *static_engine_root_;
  char *legacy_cell_scratch_;
  common::SqlTypeMeta *native_cell_metas_;
  int64_t native_cell_count_;
  mutable bool row_iteration_started_;
  // Reusable native descriptors and scalar slots for the row iterator interface.
  mutable common::ObNewRow row_;
  mutable ObBatchRowIter br_it_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObExecuteResult);
};

}
}
#endif /* OCEANBASE_SQL_EXECUTOR_OB_EXECUTE_RESULT_ */
