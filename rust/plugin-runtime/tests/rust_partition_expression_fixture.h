// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Generated calc_tablet_id -> real DAS/schema partition lookup -> one-sided
// tablet remapping -> Rust range comparison -> actual batch sender.
#ifndef SEEKDB_TEST_RUST_PARTITION_EXPRESSION_FIXTURE_H_
#define SEEKDB_TEST_RUST_PARTITION_EXPRESSION_FIXTURE_H_
#include "sql/engine/expr/ob_expr_calc_partition_id.h"
#include "sql/engine/px/ob_px_sqc_handler.h"
#include "rust_tablet_transmit_fixture.h"
namespace rust_partition_expression_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share::schema;
inline void build_schema(ObTableSchema &schema, bool full = false)
{
  schema.set_table_id(full ? 311240 : 311239);
  schema.set_database_id(OB_SYS_DATABASE_ID);
  schema.set_schema_version(42);
  schema.set_table_type(USER_TABLE);
  CHECK(schema.set_table_name("rust_range_partitions") == OB_SUCCESS);
  schema.set_part_level(PARTITION_LEVEL_TWO);
  schema.get_part_option().set_part_func_type(PARTITION_FUNC_TYPE_RANGE);
  schema.get_part_option().set_part_num(3);
  schema.get_sub_part_option().set_part_func_type(PARTITION_FUNC_TYPE_RANGE);
  schema.get_sub_part_option().set_part_num(full ? 3 : 1);
  for (int i = 0; i < 3; ++i) {
    ObPartition part;
    part.set_table_id(schema.get_table_id()); part.set_part_id(101 + i);
    part.set_part_idx(i); part.set_sub_part_num(full ? 3 : 1);
    ObObj bound; bound.set_int(10 * (i + 1));
    CHECK(part.set_high_bound_val(ObRowkey(&bound, 1)) == OB_SUCCESS);
    for (int j = 0; j < (full ? 3 : 1); ++j) {
      ObSubPartition sub;
      sub.set_table_id(schema.get_table_id()); sub.set_part_id(101 + i);
      sub.set_sub_part_id(201 + i * 3 + j); sub.set_sub_part_idx(j);
      sub.set_tablet_id(full ? 1011 + i * 100 + j * 11 : 11 * (i + 1));
      if (full) bound.set_int(10 * (j + 1)); else bound.set_max_value();
      CHECK(sub.set_high_bound_val(ObRowkey(&bound, 1)) == OB_SUCCESS);
      CHECK(part.add_partition(sub) == OB_SUCCESS);
    }
    CHECK(schema.add_partition(part) == OB_SUCCESS);
  }
}
template <typename Provider, typename Expected>
void run(ObEvalCtx &eval, Provider &provider, const ExprFixedArray &keys,
    const ObSortFuncs &functions, const ObSortCollations &collations,
    const std::vector<const ObChunkDatumStore::StoredRow *> &rows, Expected expected,
    ObExpr &input, ObExpr &calc, ObExpr &ddl, ObExpr &tablet_output)
{
  auto &exec = eval.exec_ctx_;
  auto *info = dynamic_cast<CalcPartitionBaseInfo *>(calc.extra_info_);
  CHECK(info && info->partition_id_calc_type_ == CALC_IGNORE_SUB_PART);
  CHECK(calc.eval_func_ == ObExprCalcPartitionBase::calc_partition_level_two);
  CHECK(calc.arg_cnt_ == 2 && calc.args_[0] == &input);
  const ObTableSchema *schema = nullptr;
  CHECK(exec.get_sql_ctx()->schema_guard_->get_table_schema(info->ref_table_id_, schema) == OB_SUCCESS && schema);
  // The enclosing negative tests also have tablet 44 with a bad channel. This
  // real schema has only three tablets, so use its own matching SQC range set.
  struct SqcScope {
    ObExecContext &exec_; ObPxSqcHandler *saved_; ObPxSqcHandler handler_;
    explicit SqcScope(ObExecContext &exec) : exec_(exec), saved_(exec.get_sqc_handler()) {
      CHECK(saved_ && handler_.init(exec.get_runtime_services()) == OB_SUCCESS);
      Ob2DArray<ObPxTabletRange> ranges;
      for (int64_t i = 0; i < saved_->get_partition_ranges().count(); ++i) {
        const auto &range = saved_->get_partition_ranges().at(i);
        if (range.tablet_id_ != 44) CHECK(ranges.push_back(range) == OB_SUCCESS);
      }
      CHECK(ranges.count() == 3 && handler_.set_partition_ranges(ranges) == OB_SUCCESS);
      exec_.set_sqc_handler(&handler_);
    }
    ~SqcScope() { exec_.set_sqc_handler(saved_); handler_.reset(); }
  } sqc(exec);
  ObPxPartChInfo channels;
  for (int ch : {2, 4}) CHECK(channels.part_ch_array_.push_back(ObPxPartChMapItem(11, ch)) == OB_SUCCESS);
  for (int ch : {7, 9, 11, 13}) CHECK(channels.part_ch_array_.push_back(ObPxPartChMapItem(22, ch)) == OB_SUCCESS);
  CHECK(channels.part_ch_array_.push_back(ObPxPartChMapItem(33, 17)) == OB_SUCCESS);
  ObSlaveMapPkeyRangeIdxCalc routing(exec, *schema, &calc, ObPQDistributeMethod::NONE,
      ObNullDistributeMethod::NONE, channels, keys, &functions, &collations,
      OB_REPARTITION_ONE_SIDE_ONE_LEVEL_FIRST, &ddl);
  ObEvalCtx::BatchInfoScopeGuard frame(eval);
  const auto clear_calc = [&] {
    calc.get_eval_info(eval).evaluated_ = false;
    calc.get_eval_info(eval).projected_ = false;
    if (calc.is_batch_result()) calc.get_evaluated_flags(eval).reset(eval.max_batch_size_);
  };
  const auto fill = [&](int row, int value) {
    CHECK(rows[row]->to_expr(keys, eval) == OB_SUCCESS);
    input.locate_datum_for_write(eval).set_int(value); input.set_evaluated_projected(eval);
    ddl.locate_datum_for_write(eval).set_int(-99);
  };
  const auto channel_for = [&](int row, int partition) {
    return partition == 0 ? (expected(row) < 2 ? 2 : 4) : partition == 1 ? 7 + 2 * expected(row) : 17;
  };
  const auto ddl_for = [&](int row, int partition) {
    return oceanbase::storage::ObTabletSliceParam(partition == 2 ? 1 : 3,
        partition == 2 ? 0 : expected(row)).slice_id_;
  };
  const int resolves = provider.resolves_;
  // Reusing the same calculator must rebuild both the channel and part->tablet
  // maps. Previously destroy left the latter created, so the second init failed.
  for (int pass = 0; pass < 2; ++pass) {
    const int initialized = routing.init();
    if (initialized != OB_SUCCESS) std::cerr << "partition expression init=" << initialized << " pass=" << pass << std::endl;
    CHECK(initialized == OB_SUCCESS);
    frame.set_batch_size(1); frame.set_batch_idx(0);
    ObSliceIdxCalc::SliceIdxArray output;
    for (int partition = 0; partition < 3; ++partition) {
      // Include exact partition boundaries: RANGE upper bounds are exclusive.
      for (int value : {partition * 10, partition * 10 + 9}) {
        for (int row = 0; row < int(rows.size()); ++row) {
          fill(row, value); clear_calc();
          CHECK(routing.get_slice_indexes<ObSliceIdxCalc::SM_REPART_RANGE>(keys, eval, output) == OB_SUCCESS);
          CHECK(calc.locate_expr_datum(eval).get_int() == 101 + partition);
          CHECK(routing.get_last_tablet_id() == 11 * (partition + 1));
          CHECK(output.at(0) == channel_for(row, partition));
          CHECK(ddl.locate_expr_datum(eval).get_int() == ddl_for(row, partition));
        }
      }
    }
    fill(0, 30); clear_calc();
    CHECK(routing.get_slice_indexes<ObSliceIdxCalc::SM_REPART_RANGE>(keys, eval, output) == OB_NO_PARTITION_FOR_GIVEN_VALUE);
    CHECK(output.at(0) == OB_INVALID_INDEX && ddl.locate_expr_datum(eval).get_int() == -99);
    if (eval.max_batch_size_ >= 3) {
      frame.set_batch_size(3);
      const auto fill_batch = [&](bool missing) {
        for (int i = 0; i < 3; ++i) {
          frame.set_batch_idx(i); fill(i, i == 2 && missing ? 30 : 10 * i);
        }
        clear_calc();
      };
      const std::vector<int> wanted_channels = {channel_for(0, 0), channel_for(1, 1), 17};
      const std::vector<int> wanted_tablets = {11, 22, 33};
      const std::vector<int64_t> wanted_ddl = {ddl_for(0, 0), ddl_for(1, 1), ddl_for(2, 2)};
      fill_batch(false);
      rust_tablet_transmit_test::run(exec, routing, tablet_output, ddl,
          wanted_channels, wanted_tablets, wanted_ddl);
      const int64_t *published = nullptr;
      CHECK(routing.get_previous_batch_tablet_ids(3, published) == OB_SUCCESS && published);
      for (int i = 0; i < 3; ++i) CHECK(published[i] == wanted_tablets[i]);
      fill_batch(true);
      rust_tablet_transmit_test::run(exec, routing, tablet_output, ddl,
          wanted_channels, wanted_tablets, wanted_ddl, 0, OB_NO_PARTITION_FOR_GIVEN_VALUE);
      CHECK(routing.get_previous_batch_tablet_ids(3, published) == OB_STATE_NOT_MATCH && !published);
      for (int i = 0; i < 3; ++i) {
        frame.set_batch_idx(i); CHECK(ddl.locate_expr_datum(eval).get_int() == -99);
      }
      fill_batch(true);
      rust_tablet_transmit_test::run(exec, routing, tablet_output, ddl,
          wanted_channels, wanted_tablets, wanted_ddl, uint64_t(4));
      CHECK(routing.get_previous_batch_tablet_ids(3, published) == OB_SUCCESS && published);
      CHECK(published[0] == 11 && published[1] == 22 && published[2] == OB_INVALID_INDEX);
    }
    CHECK(routing.destroy() == OB_SUCCESS);
    CHECK(routing.destroy() == OB_SUCCESS);
  }
  CHECK(provider.resolves_ == resolves);
}
template <typename Provider, typename Expected>
void run_two_levels(ObEvalCtx &eval, Provider &provider, const ExprFixedArray &keys,
    const ObSortFuncs &functions, const ObSortCollations &collations,
    const std::vector<const ObChunkDatumStore::StoredRow *> &rows,
    const ObPxTabletRange &sampled, Expected expected, ObExpr &first_input,
    ObExpr &sub_input, ObExpr &sub_calc, ObExpr &full_calc, ObExpr &ddl,
    ObExpr &tablet_output, bool native)
{
  auto &exec = eval.exec_ctx_;
  CHECK(&first_input != &sub_input);
  for (auto *expr : {&sub_calc, &full_calc}) {
    CHECK(expr->eval_func_ == ObExprCalcPartitionBase::calc_partition_level_two);
    CHECK(expr->arg_cnt_ == 2 && expr->args_[0] == &first_input && expr->args_[1] == &sub_input);
    auto *info = dynamic_cast<CalcPartitionBaseInfo *>(expr->extra_info_);
    CHECK(info && info->ref_table_id_ == 311240);
    CHECK(info->partition_id_calc_type_ == (expr == &sub_calc ? CALC_IGNORE_FIRST_PART : CALC_NORMAL));
  }
  const ObTableSchema *schema = nullptr;
  CHECK(exec.get_sql_ctx()->schema_guard_->get_table_schema(311240, schema) == OB_SUCCESS && schema);
  const auto tablet_for = [](int first, int sub) { return 1011 + first * 100 + sub * 11; };
  const auto channel_for = [&](int row, int sub) {
    return sub == 0 ? (expected(row) < 2 ? 2 : 4) : sub == 1 ? 7 + 2 * expected(row) : 17;
  };
  const auto ddl_for = [&](int row, int sub) {
    return oceanbase::storage::ObTabletSliceParam(sub == 2 ? 1 : 3, sub == 2 ? 0 : expected(row)).slice_id_;
  };
  // Invalid channel metadata must not select an arbitrary first partition or
  // overwrite a previously bound expression context on a failed initialization.
  CHECK(ObExprCalcPartitionBase::set_first_part_id(exec, sub_calc, 102) == OB_SUCCESS);
  for (bool reverse : {false, true}) {
    ObPxPartChInfo mixed;
    for (int first : {int(reverse), int(!reverse)})
      CHECK(mixed.part_ch_array_.push_back(ObPxPartChMapItem(tablet_for(first, 0), 2)) == OB_SUCCESS);
    ObSlaveMapPkeyRangeIdxCalc invalid(exec, *schema, &sub_calc, ObPQDistributeMethod::NONE,
        ObNullDistributeMethod::NONE, mixed, keys, &functions, &collations,
        OB_REPARTITION_ONE_SIDE_ONE_LEVEL_SUB, &ddl);
    CHECK(invalid.init() == OB_INVALID_ARGUMENT);
    int64_t first = OB_INVALID_ID;
    CHECK(ObExprCalcPartitionBase::get_first_part_id(exec, sub_calc, first) == OB_SUCCESS && first == 102);
    CHECK(invalid.destroy() == OB_SUCCESS);
  }
  const int resolves = provider.resolves_, comparisons = provider.comparisons_;
  ObEvalCtx::BatchInfoScopeGuard frame(eval);
  for (bool sub_only : {true, false}) {
    ObExpr &calc = sub_only ? sub_calc : full_calc;
    ObPxPartChInfo channels;
    ObSlaveMapPkeyRangeIdxCalc routing(exec, *schema, &calc, ObPQDistributeMethod::NONE,
        ObNullDistributeMethod::NONE, channels, keys, &functions, &collations,
        sub_only ? OB_REPARTITION_ONE_SIDE_ONE_LEVEL_SUB : OB_REPARTITION_ONE_SIDE_TWO_LEVEL, &ddl);
    // The same SUB expression and calculator move 101 -> 103 -> 102; stale
    // first-partition context would produce a different tablet or a lookup error.
    for (int fixed_first : {0, 2, 1}) {
      struct SqcScope {
        ObExecContext &exec_; ObPxSqcHandler *saved_; ObPxSqcHandler handler_;
        explicit SqcScope(ObExecContext &exec) : exec_(exec), saved_(exec.get_sqc_handler()) {
          CHECK(handler_.init(exec.get_runtime_services()) == OB_SUCCESS);
          exec_.set_sqc_handler(&handler_);
        }
        ~SqcScope() { exec_.set_sqc_handler(saved_); handler_.reset(); }
      } sqc(exec);
      Ob2DArray<ObPxTabletRange> ranges;
      channels.part_ch_array_.reset();
      for (int first = 0; first < 3; ++first) if (!sub_only || first == fixed_first) {
        for (int sub = 0; sub < 3; ++sub) {
          const int tablet = tablet_for(first, sub);
          ObPxTabletRange range; range.tablet_id_ = tablet;
          if (sub != 2) CHECK(range.range_cut_.assign(sampled.range_cut_) == OB_SUCCESS);
          CHECK(ranges.push_back(range) == OB_SUCCESS);
          const std::vector<int> tasks = sub == 0 ? std::vector<int>{2, 4} :
              sub == 1 ? std::vector<int>{7, 9, 11, 13} : std::vector<int>{17};
          for (int task : tasks)
            CHECK(channels.part_ch_array_.push_back(ObPxPartChMapItem(tablet, task)) == OB_SUCCESS);
        }
      }
      CHECK(sqc.handler_.set_partition_ranges(ranges) == OB_SUCCESS);
      CHECK(routing.init() == OB_SUCCESS);
      if (sub_only) {
        int64_t first = OB_INVALID_ID;
        CHECK(ObExprCalcPartitionBase::get_first_part_id(exec, calc, first) == OB_SUCCESS && first == 101 + fixed_first);
      }
      const auto clear_calc = [&] {
        calc.get_eval_info(eval).evaluated_ = false;
        calc.get_eval_info(eval).projected_ = false;
        if (calc.is_batch_result()) calc.get_evaluated_flags(eval).reset(eval.max_batch_size_);
      };
      const auto fill = [&](int row, int first_value, int sub_value) {
        CHECK(rows[row]->to_expr(keys, eval) == OB_SUCCESS);
        first_input.locate_datum_for_write(eval).set_int(first_value);
        first_input.set_evaluated_projected(eval);
        // Independent, generated integer frame. This matrix tests partition
        // evaluation, not re-evaluation of the fixture's ordinal+1000 expression.
        sub_input.locate_datum_for_write(eval).set_int(sub_value);
        sub_input.set_evaluated_projected(eval);
        ddl.locate_datum_for_write(eval).set_int(-99);
      };
      frame.set_batch_size(1); frame.set_batch_idx(0);
      ObSliceIdxCalc::SliceIdxArray output;
      for (int first = 0; first < 3; ++first) if (!sub_only || first == fixed_first) {
        for (int sub = 0; sub < 3; ++sub) for (int edge : {0, 9}) {
          for (int row = 0; row < int(rows.size()); ++row) {
            // An out-of-range first input proves SUB uses its bound context.
            fill(row, sub_only ? 999 : first * 10 + edge, sub * 10 + edge); clear_calc();
            CHECK(routing.get_slice_indexes<ObSliceIdxCalc::SM_REPART_RANGE>(keys, eval, output) == OB_SUCCESS);
            CHECK(calc.locate_expr_datum(eval).get_int() == tablet_for(first, sub));
            CHECK(routing.get_last_tablet_id() == tablet_for(first, sub));
            CHECK(output.at(0) == channel_for(row, sub));
            CHECK(ddl.locate_expr_datum(eval).get_int() == ddl_for(row, sub));
          }
        }
      }
      for (bool first_missing : {false, true}) {
        if (sub_only && first_missing) continue;
        fill(0, first_missing ? 30 : fixed_first * 10, first_missing ? 0 : 30); clear_calc();
        CHECK(routing.get_slice_indexes<ObSliceIdxCalc::SM_REPART_RANGE>(keys, eval, output) == OB_NO_PARTITION_FOR_GIVEN_VALUE);
        CHECK(output.at(0) == OB_INVALID_INDEX && ddl.locate_expr_datum(eval).get_int() == -99);
      }
      if (eval.max_batch_size_ >= 3) {
        frame.set_batch_size(3);
        const auto row_for = [&](int i) { return (fixed_first * 3 + i) % int(rows.size()); };
        const auto fill_batch = [&](int missing) {
          for (int i = 0; i < 3; ++i) {
            frame.set_batch_idx(i);
            const int first_value = sub_only ? 999 : ((fixed_first + i) % 3) * 10;
            fill(row_for(i), i == 2 && missing == 1 ? 30 : first_value, i == 2 && missing == 2 ? 30 : i * 10);
          }
          clear_calc();
        };
        const std::vector<int> wanted_channels = {channel_for(row_for(0), 0), channel_for(row_for(1), 1), 17};
        const std::vector<int> wanted_tablets = {tablet_for(fixed_first, 0),
            tablet_for(sub_only ? fixed_first : (fixed_first + 1) % 3, 1),
            tablet_for(sub_only ? fixed_first : (fixed_first + 2) % 3, 2)};
        const std::vector<int64_t> wanted_ddl = {ddl_for(row_for(0), 0), ddl_for(row_for(1), 1), ddl_for(row_for(2), 2)};
        fill_batch(0);
        rust_tablet_transmit_test::run(exec, routing, tablet_output, ddl, wanted_channels, wanted_tablets, wanted_ddl,
            0, OB_SUCCESS, keys.at(0));
        const int64_t *published = nullptr;
        CHECK(routing.get_previous_batch_tablet_ids(3, published) == OB_SUCCESS && published);
        for (int i = 0; i < 3; ++i) CHECK(published[i] == wanted_tablets[i]);
        for (int missing : {1, 2}) {
          if (sub_only && missing == 1) continue;
          fill_batch(missing);
          rust_tablet_transmit_test::run(exec, routing, tablet_output, ddl, wanted_channels, wanted_tablets,
              wanted_ddl, 0, OB_NO_PARTITION_FOR_GIVEN_VALUE, keys.at(0));
          CHECK(routing.get_previous_batch_tablet_ids(3, published) == OB_STATE_NOT_MATCH && !published);
          for (int i = 0; i < 3; ++i) {
            frame.set_batch_idx(i); CHECK(ddl.locate_expr_datum(eval).get_int() == -99);
          }
          fill_batch(missing);
          rust_tablet_transmit_test::run(exec, routing, tablet_output, ddl, wanted_channels, wanted_tablets, wanted_ddl,
              uint64_t(4), OB_SUCCESS, keys.at(0));
          CHECK(routing.get_previous_batch_tablet_ids(3, published) == OB_SUCCESS && published);
          CHECK(published[0] == wanted_tablets[0] && published[1] == wanted_tablets[1] && published[2] == OB_INVALID_INDEX);
        }
        // Put three rows into one actual channel buffer, so the wire reader
        // exercises a 2+1 batch split, not only single-row channel buffers.
        // Choose distinct values in ranges 0/1, which share channel 2 for this
        // tablet. This varies payload lengths, NULL and DDL IDs across blocks.
        std::vector<int> burst_rows, burst_channels;
        std::vector<int64_t> burst_ddl;
        for (int row : {6, 0, 1, 2, 3, 4, 5}) {
          if (burst_rows.size() < 3 && expected(row) < 2) {
            burst_rows.push_back(row); burst_channels.push_back(channel_for(row, 0));
            burst_ddl.push_back(ddl_for(row, 0));
          }
        }
        CHECK(burst_rows.size() == 3);
        for (int i = 0; i < 3; ++i) {
          CHECK(burst_channels[i] == 2);
          frame.set_batch_idx(i); fill(burst_rows[i], sub_only ? 999 : fixed_first * 10, 0);
        }
        clear_calc();
        rust_tablet_transmit_test::run(exec, routing, tablet_output, ddl,
            burst_channels,
            std::vector<int>(3, tablet_for(fixed_first, 0)),
            burst_ddl, 0, OB_SUCCESS, keys.at(0));
        CHECK(routing.get_previous_batch_tablet_ids(3, published) == OB_SUCCESS && published);
        for (int i = 0; i < 3; ++i) CHECK(published[i] == tablet_for(fixed_first, 0));
      }
      CHECK(routing.destroy() == OB_SUCCESS);
    }
  }
  CHECK(provider.resolves_ == resolves);
  CHECK(native ? provider.comparisons_ == comparisons : provider.comparisons_ > comparisons);
}
}
#endif
