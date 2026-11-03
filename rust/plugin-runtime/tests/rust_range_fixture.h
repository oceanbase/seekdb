// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Real sample sorting/splitting and range routing, with supplied sample rows.
#ifndef SEEKDB_TEST_RUST_RANGE_FIXTURE_H_
#define SEEKDB_TEST_RUST_RANGE_FIXTURE_H_
#include "sql/engine/px/ob_slice_calc.h"
#include "sql/engine/px/exchange/ob_px_ms_coord_op.h"
#include "sql/engine/px/datahub/components/ob_dh_sample.h"
#include "data_plane/ddl/ob_ddl_seq_generator.h"
#include "rust_partition_range_fixture.h"
namespace rust_range_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
using Row = ObChunkDatumStore::StoredRow;
template <typename Provider>
void run(const ObSortSpec &spec, ObEvalCtx &eval, Provider &provider,
    ObArenaAllocator &arena, const std::vector<const Row *> &inputs,
    bool with_ddl = false, bool native = false, ObExpr *partition_ddl = nullptr,
    ObExpr *tablet_output = nullptr, const ExprFixedArray *partition_calc = nullptr)
{
  const int key_count = with_ddl ? 1 : 2;
  // Reuse a distinct, generated integer output frame for the DDL slice ID.
  // Keep the original two-key tests as well: the output must not alias a key.
  ObExpr *ddl = with_ddl ? spec.all_exprs_.at(1) : nullptr;
  ObSortCollations collations(arena, key_count);
  ObSortFuncs functions(arena, key_count);
  ExprFixedArray expressions(arena);
  CHECK(expressions.init(key_count) == OB_SUCCESS);
  for (int i = 0; i < key_count; ++i) {
    CHECK(spec.sort_collations_.at(i).field_idx_ == uint32_t(i));
    CHECK(expressions.push_back(spec.all_exprs_.at(i)) == OB_SUCCESS);
    CHECK(collations.push_back(spec.sort_collations_.at(i)) == OB_SUCCESS);
    CHECK(functions.push_back(spec.sort_cmp_funs_.at(i)) == OB_SUCCESS);
  }
  ObChunkDatumStore samples("RustRanges");
  CHECK(samples.init(INT64_MAX, ObCtxIds::WORK_AREA, "RustRanges", false) == OB_SUCCESS);
  std::vector<const Row *> rows;
  for (auto *input : inputs) {
    Row *copy = nullptr;
    CHECK(samples.add_row(input->cells(), key_count, 0, &copy) == OB_SUCCESS && copy);
    rows.push_back(copy);
  }
  ObPxMSCoordSpec coord_spec(arena, PHY_PX_MERGE_SORT_COORD);
  coord_spec.plan_ = spec.plan_; coord_spec.max_batch_size_ = spec.max_batch_size_;
  ObPxMSCoordOp coordinator(eval.exec_ctx_, coord_spec, nullptr);
  ObDynamicSamplePieceMsgCtx::SortDef definition;
  definition.exprs_ = &expressions; definition.collations_ = &collations;
  definition.cmp_funs_ = &functions;
  ObDynamicSamplePieceMsgCtx sampler(1, 1, INT64_MAX, eval.exec_ctx_, coordinator, definition);
  ObSEArray<uint64_t, 1> tablets; CHECK(tablets.push_back(1) == OB_SUCCESS);
  CHECK(sampler.init(tablets) == OB_SUCCESS);
  ObPxTabletRange ranges;
  const int comparisons = provider.comparisons_, resolves = provider.resolves_;
  CHECK(sampler.split_range(&samples, 3, ranges.range_cut_) == OB_SUCCESS);
  CHECK(ranges.range_cut_.count() == 2);
  CHECK(native ? provider.comparisons_ == comparisons : provider.comparisons_ > comparisons);
  const bool ascending = spec.sort_collations_.at(0).is_ascending_;
  const std::vector<int> order = native
      ? (ascending ? std::vector<int>{6,4,1,3,0,5,2} : std::vector<int>{2,5,0,3,1,4,6})
      : (ascending ? std::vector<int>{6,4,0,5,2,1,3} : std::vector<int>{3,1,2,5,0,4,6});
  if (!with_ddl) {
    CHECK(ranges.range_cut_.at(0).at(1).get_int() == order[1] + 1);
    CHECK(ranges.range_cut_.at(1).at(1).get_int() == order[3] + 1);
  }
  ObRangeSliceIdCalc routing(arena, 3, &ranges, &expressions,
      functions, collations, ddl);
  const auto expected = [&](int row) {
    int position = std::find(order.begin(), order.end(), row) - order.begin();
    // Without the ordinal tie-breaker both copies of z belong to the lower
    // boundary, including descending order where the sample cuts split a tie.
    if (with_ddl && (row == 0 || row == 5)) {
      position = std::min(position, int(std::find(order.begin(), order.end(), row == 0 ? 5 : 0) - order.begin()));
    }
    return position <= 1 ? 0 : position <= 3 ? 1 : 2;
  };
  if (partition_ddl) {
    CHECK(with_ddl);
    rust_partition_range_test::run(eval, provider, expressions, functions, collations,
        rows, ranges, expected, *ddl, *partition_ddl, native, tablet_output, partition_calc);
  }
  ObEvalCtx::BatchInfoScopeGuard frame(eval);
  frame.set_batch_size(1); frame.set_batch_idx(0);
  const auto reset_ddl = [&]() {
    if (ddl) ddl->locate_datum_for_write(eval).set_int(-99);
  };
  const auto check_ddl = [&](int count, int index) {
    if (ddl) {
      oceanbase::storage::ObTabletSliceParam packed(count, index);
      if (ddl->locate_expr_datum(eval).get_int() != packed.slice_id_) {
        std::cerr << "range DDL count=" << count << " index=" << index
                  << " actual=" << ddl->locate_expr_datum(eval).get_int()
                  << " expected=" << packed.slice_id_ << " frame=" << eval.get_batch_idx()
                  << " size=" << eval.get_batch_size() << std::endl;
      }
      CHECK(ddl->locate_expr_datum(eval).get_int() == packed.slice_id_);
    }
  };
  const auto unchanged_ddl = [&]() {
    if (ddl) CHECK(ddl->locate_expr_datum(eval).get_int() == -99);
  };
  ObSliceIdxCalc::SliceIdxArray indexes;
  for (int tasks : {1, 2, 3, 5}) {
    routing.task_cnt_ = tasks;
    for (int i = 0; i < int(rows.size()); ++i) {
      CHECK(rows[i]->to_expr(expressions, eval) == OB_SUCCESS);
      reset_ddl();
      const int ret = routing.get_slice_indexes_inner(expressions, eval, indexes);
      if (ret != OB_SUCCESS) {
        const ObDatumAccessContext *access = nullptr;
        CHECK(eval.get_datum_access_ctx(access) == OB_SUCCESS);
        std::cerr << "range route ret=" << ret << " tasks=" << tasks << " input=" << i
                  << " access=" << bool(access) << std::endl;
      }
      CHECK(ret == OB_SUCCESS);
      CHECK(indexes.count() == 1 && indexes.at(0) == expected(i) % tasks);
      check_ddl(3, expected(i)); // DDL stores the range, not range % task count.
    }
  }
  routing.task_cnt_ = 3;
  if (native) {
    // A separately generated CAST(... AS BINARY) SQL spec, not a plugin spec
    // with its expression map removed. Native comparisons must bypass Rust.
    if (eval.max_batch_size_ >= 3) {
      frame.set_batch_size(3);
      for (int tasks : {1, 2, 3, 5}) for (int first = 0; first < 7; ++first) {
        routing.task_cnt_ = tasks;
        for (int i = 0; i < 3; ++i) {
          frame.set_batch_idx(i);
          CHECK(rows[(first + i) % rows.size()]->to_expr(expressions, eval) == OB_SUCCESS);
          reset_ddl();
        }
        uint64_t bits = 0;
        auto &skip = *to_bit_vector(&bits);
        skip.set(2);
        int64_t *batch = nullptr;
        CHECK(routing.get_slice_idx_batch_inner(expressions, eval, skip, 3, batch) == OB_SUCCESS && batch);
        for (int i = 0; i < 3; ++i) {
          frame.set_batch_idx(i);
          if (i == 2) { CHECK(batch[i] == OB_INVALID_INDEX); unchanged_ddl(); }
          else {
            CHECK(batch[i] == expected((first + i) % rows.size()) % tasks);
            check_ddl(3, expected((first + i) % rows.size()));
          }
        }
      }
    }
    CHECK(provider.comparisons_ == comparisons && provider.resolves_ == resolves);
    sampler.destroy();
    return;
  }
  class Cancel final : public ObIExtraStatusCheck {
  public:
    explicit Cancel(const int &calls) : calls_(calls), before_(calls) {}
    const char *name() const override { return "plugin-range-cancel"; }
    int check() const override { return calls_ == before_ ? OB_SUCCESS : OB_TIMEOUT; }
  private:
    const int &calls_; int before_;
  };
  CHECK(rows[0]->to_expr(expressions, eval) == OB_SUCCESS);
  const char invalid = char(0xff);
  expressions.at(0)->locate_expr_datum(eval).set_string(ObString(1, &invalid));
  reset_ddl();
  CHECK(routing.get_slice_indexes_inner(expressions, eval, indexes) == OB_INVALID_ARGUMENT);
  CHECK(indexes.at(0) == OB_INVALID_INDEX);
  unchanged_ddl();
  CHECK(rows[0]->to_expr(expressions, eval) == OB_SUCCESS);
  const int before_cancel = provider.comparisons_;
  { Cancel cancel(provider.comparisons_);
    ObIExtraStatusCheck::Guard guard(eval.exec_ctx_, cancel);
    CHECK(routing.get_slice_indexes_inner(expressions, eval, indexes) == OB_TIMEOUT);
    CHECK(indexes.at(0) == OB_INVALID_INDEX); }
  unchanged_ddl();
  CHECK(provider.comparisons_ == before_cancel + 1);
  CHECK(routing.get_slice_indexes_inner(expressions, eval, indexes) == OB_SUCCESS);
  check_ddl(3, expected(0));
  reset_ddl();
  routing.task_cnt_ = 0;
  CHECK(routing.get_slice_indexes_inner(expressions, eval, indexes) == OB_INVALID_ARGUMENT);
  CHECK(indexes.at(0) == OB_INVALID_INDEX);
  unchanged_ddl();
  routing.task_cnt_ = 3;
  ObPxTabletRange empty;
  routing.range_ = &empty;
  CHECK(routing.get_slice_indexes_inner(expressions, eval, indexes) == OB_SUCCESS && indexes.at(0) == 0);
  check_ddl(1, 0);
  routing.range_ = &ranges;
  if (eval.max_batch_size_ >= 3) {
    uint64_t bits = 0;
    auto &skip = *to_bit_vector(&bits);
    frame.set_batch_size(3);
    const auto fill = [&](int first) {
      for (int i = 0; i < 3; ++i) {
        frame.set_batch_idx(i);
        CHECK(rows[(first + i) % rows.size()]->to_expr(expressions, eval) == OB_SUCCESS);
        reset_ddl();
      }
    };
    for (int first = 0; first < 7; ++first) {
      fill(first);
      int64_t *batch = nullptr;
      CHECK(routing.get_slice_idx_batch_inner(expressions, eval, skip, 3, batch) == OB_SUCCESS && batch);
      for (int i = 0; i < 3; ++i) {
        frame.set_batch_idx(i);
        CHECK(batch[i] == expected((first + i) % rows.size()));
        check_ddl(3, expected((first + i) % rows.size()));
      }
    }
    fill(0); frame.set_batch_idx(2);
    expressions.at(0)->locate_expr_datum(eval).set_string(ObString(1, &invalid));
    int64_t *batch = nullptr;
    CHECK(routing.get_slice_idx_batch_inner(expressions, eval, skip, 3, batch) == OB_INVALID_ARGUMENT);
    CHECK(batch == nullptr);
    for (int i = 0; i < 3; ++i) { frame.set_batch_idx(i); unchanged_ddl(); }
    skip.set(2);
    int64_t skipped_ddl = -99;
    if (ddl) ddl->locate_expr_datum(eval).ptr_ = reinterpret_cast<const char *>(&skipped_ddl);
    CHECK(routing.get_slice_idx_batch_inner(expressions, eval, skip, 3, batch) == OB_SUCCESS);
    CHECK(batch && batch[0] == expected(0) && batch[1] == expected(1) && batch[2] == OB_INVALID_INDEX);
    if (ddl) CHECK(ddl->locate_expr_datum(eval).ptr_ == reinterpret_cast<const char *>(&skipped_ddl));
    for (int i = 0; i < 3; ++i) {
      frame.set_batch_idx(i);
      if (i == 2) unchanged_ddl(); else check_ddl(3, expected(i));
    }
    skip.unset(2); fill(0);
    const int before = provider.comparisons_;
    { Cancel cancel(provider.comparisons_);
      ObIExtraStatusCheck::Guard guard(eval.exec_ctx_, cancel);
      CHECK(routing.get_slice_idx_batch_inner(expressions, eval, skip, 3, batch) == OB_TIMEOUT);
      CHECK(batch == nullptr); }
    CHECK(provider.comparisons_ == before + 1);
    for (int i = 0; i < 3; ++i) { frame.set_batch_idx(i); unchanged_ddl(); }
    CHECK(routing.get_slice_idx_batch_inner(expressions, eval, skip, 4, batch) == OB_INVALID_ARGUMENT && !batch);
    CHECK(routing.get_slice_idx_batch_inner(expressions, eval, skip, 3, batch) == OB_SUCCESS && batch);
    CHECK(routing.get_slice_idx_batch_inner(expressions, eval, skip, 0, batch) == OB_SUCCESS && !batch);
    routing.task_cnt_ = 0;
    CHECK(routing.get_slice_idx_batch_inner(expressions, eval, skip, 3, batch) == OB_INVALID_ARGUMENT && !batch);
    routing.task_cnt_ = 3;
    fill(0); routing.range_ = &empty; skip.set(2);
    if (ddl) ddl->locate_expr_datum(eval).ptr_ = reinterpret_cast<const char *>(&skipped_ddl);
    CHECK(routing.get_slice_idx_batch_inner(expressions, eval, skip, 3, batch) == OB_SUCCESS);
    CHECK(batch && batch[0] == 0 && batch[1] == 0 && batch[2] == OB_INVALID_INDEX);
    if (ddl) CHECK(ddl->locate_expr_datum(eval).ptr_ == reinterpret_cast<const char *>(&skipped_ddl));
    for (int i = 0; i < 3; ++i) {
      frame.set_batch_idx(i);
      if (i == 2) unchanged_ddl(); else check_ddl(1, 0);
    }
    routing.range_ = &ranges;
    // Unlike the precomputed-key cases above, feed the generated input column
    // and invalidate its unary expression chain. This proves that RANGE asks
    // a nested Rust SQL function for a batch, not one scalar call per row.
    std::vector<ObExpr *> chain;
    ObExpr *leaf = expressions.at(0);
    bool has_function = false;
    while (leaf && leaf->type_ != T_REF_COLUMN) {
      has_function |= leaf->type_ == T_FUN_SYS_PLUGIN_FUNCTION;
      chain.push_back(leaf);
      // SQL function arg 0 is its serialized binding; its value is arg 1.
      const int value_arg = leaf->type_ == T_FUN_SYS_PLUGIN_FUNCTION ? 1 : 0;
      CHECK(leaf->arg_cnt_ > value_arg && leaf->args_[value_arg]);
      leaf = leaf->args_[value_arg];
    }
    if (has_function) {
      CHECK(leaf && leaf->is_batch_result());
      bits = 0;
      for (int first = 0; first < 7; ++first) {
        fill(first);
        for (auto *expr : chain) {
          expr->get_eval_info(eval).evaluated_ = false;
          expr->get_eval_info(eval).projected_ = false;
          if (expr->is_batch_result()) expr->get_evaluated_flags(eval).reset(3);
        }
        for (int i = 0; i < 3; ++i) {
          leaf->locate_batch_datums(eval)[i] = rows[(first + i) % rows.size()]->cells()[0];
          leaf->get_evaluated_flags(eval).set(i);
        }
        leaf->get_eval_info(eval).evaluated_ = true;
        leaf->get_eval_info(eval).projected_ = true;
        leaf->get_eval_info(eval).cnt_ = 3;
        const int scalar = provider.scalar_functions_;
        const int calls = provider.batch_calls("seekdb_rust_identity");
        CHECK(routing.get_slice_idx_batch_inner(expressions, eval, skip, 3, batch) == OB_SUCCESS && batch);
        CHECK(provider.scalar_functions_ == scalar);
        CHECK(provider.batch_calls("seekdb_rust_identity") > calls);
        for (int i = 0; i < 3; ++i) {
          frame.set_batch_idx(i);
          CHECK(batch[i] == expected((first + i) % rows.size()));
          check_ddl(3, expected((first + i) % rows.size()));
        }
      }
    }
  }
  CHECK(provider.resolves_ == resolves);
  sampler.destroy();
}
} // namespace rust_range_test
#endif
