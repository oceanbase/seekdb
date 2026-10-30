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
#include "sql/engine/ob_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExecContext;
class ObPhyOperator;
class ObOperator;
class ObOpSpec;

// Borrowed until the next advance/close. Values stay in the expression frame;
// type information stays in the output expressions. Row-producing callers can
// supply an ObNewRow without changing their execution or cursor semantics.
class ResultRowView
{
public:
  ResultRowView() : legacy_(nullptr), exprs_(nullptr), ctx_(nullptr), count_(0), idx_(0), batch_(false), cell_metas_(nullptr) {}
  void set_legacy(const common::ObNewRow *row) { *this = ResultRowView(); legacy_ = row; }
  void set_datums(const ExprFixedArray &exprs, ObEvalCtx &ctx, int64_t idx, bool batch,
                  const common::SqlTypeMeta *cell_metas = nullptr)
  { legacy_ = nullptr; exprs_ = exprs.count() ? &exprs.at(0) : nullptr; ctx_ = &ctx;
    count_ = exprs.count(); idx_ = idx; batch_ = batch; cell_metas_ = cell_metas; }
  // Project expression metadata without allocating or constructing a value object.
  // previous_meta is the per-column metadata left by the preceding result row.
  static int project_value(const common::ObDatum &datum, const common::SqlTypeMeta &meta,
                           common::SqlValueDatumMapType mapping,
                           const common::SqlTypeMeta &previous_meta,
                           common::DatumValue &value, bool keep_null_collation = false);
  int get_value(int64_t i, common::DatumValue &value) const;
  bool is_datum_row() const { return nullptr != ctx_; }
  const common::ObNewRow *legacy_row() const { return legacy_; }
  int64_t count() const { return is_datum_row() ? count_ : (legacy_ ? legacy_->get_count() : 0); }
  const ObExpr &expr(int64_t i) const { return *exprs_[i]; }
  const common::ObDatum &datum(int64_t i) const
  {
    const ObExpr &e = expr(i);
    return batch_ ? e.locate_batch_datums(*ctx_)[e.is_batch_result() ? idx_ : 0]
                  : e.locate_expr_datum(*ctx_);
  }
private:
  const common::ObNewRow *legacy_;
  ObExpr *const *exprs_;
  ObEvalCtx *ctx_;
  int64_t count_;
  int64_t idx_;
  bool batch_;
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
      native_cell_metas_(nullptr),
      native_cell_count_(0) {}
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
    row_.reset();
    native_cell_metas_ = nullptr;
    native_cell_count_ = 0;
  }

private:
  int err_code_;
  ObOperator *static_engine_root_;
  common::SqlTypeMeta *native_cell_metas_;
  int64_t native_cell_count_;
  // Reusable native descriptors and scalar slots for the row iterator interface.
  mutable common::ObNewRow row_;
  mutable ObBatchRowIter br_it_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObExecuteResult);
};

}
}
#endif /* OCEANBASE_SQL_EXECUTOR_OB_EXECUTE_RESULT_ */
