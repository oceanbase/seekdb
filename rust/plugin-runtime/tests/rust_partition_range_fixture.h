// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Actual SQC range storage and partition/channel routing; no worker scheduling.
#ifndef SEEKDB_TEST_RUST_PARTITION_RANGE_FIXTURE_H_
#define SEEKDB_TEST_RUST_PARTITION_RANGE_FIXTURE_H_
#include "sql/engine/px/ob_px_sqc_handler.h"
#include "sql/engine/expr/plugin_function_expr.h"
#include "rust_tablet_transmit_fixture.h"
#include "rust_partition_expression_fixture.h"
namespace rust_partition_range_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
template <typename Provider, typename Expected>
void run(ObEvalCtx &eval, Provider &provider, const ExprFixedArray &keys,
    const ObSortFuncs &functions, const ObSortCollations &collations,
    const std::vector<const ObChunkDatumStore::StoredRow *> &rows,
    const ObPxTabletRange &sampled, Expected expected, ObExpr &tablet, ObExpr &ddl, bool native,
    ObExpr *tablet_output, const ExprFixedArray *partition_calc)
{
  CHECK(keys.count() == 1 && &tablet != &ddl && keys.at(0) != &ddl);
  auto &exec = eval.exec_ctx_;
  struct SqcScope {
    ObExecContext &exec_; ObPxSqcHandler *saved_; ObPxSqcHandler handler_;
    explicit SqcScope(ObExecContext &exec) : exec_(exec), saved_(exec.get_sqc_handler()) {
      CHECK(handler_.init(exec.get_runtime_services()) == OB_SUCCESS);
      exec_.set_sqc_handler(&handler_);
    }
    ~SqcScope() { exec_.set_sqc_handler(saved_); handler_.reset(); }
  } sqc(exec);
  Ob2DArray<ObPxTabletRange> ranges;
  for (int id : {11, 22, 33, 44}) {
    ObPxTabletRange range;
    range.tablet_id_ = id;
    if (id != 33) CHECK(range.range_cut_.assign(sampled.range_cut_) == OB_SUCCESS);
    CHECK(ranges.push_back(range) == OB_SUCCESS);
  }
  CHECK(sqc.handler_.set_partition_ranges(ranges) == OB_SUCCESS);
  CHECK(sqc.handler_.get_partition_ranges().count() == 4);
  ObPxPartChInfo channels;
  for (int channel : {2, 4}) CHECK(channels.part_ch_array_.push_back(ObPxPartChMapItem(11, channel)) == OB_SUCCESS);
  for (int channel : {7, 9, 11, 13}) CHECK(channels.part_ch_array_.push_back(ObPxPartChMapItem(22, channel)) == OB_SUCCESS);
  CHECK(channels.part_ch_array_.push_back(ObPxPartChMapItem(33, 17)) == OB_SUCCESS);
  CHECK(channels.part_ch_array_.push_back(ObPxPartChMapItem(44, -7)) == OB_SUCCESS);
  oceanbase::share::schema::ObTableSchema schema;
  ObSortFuncs incomplete(exec.get_allocator());
  ObSlaveMapPkeyRangeIdxCalc bad_shape(exec, schema, &tablet, ObPQDistributeMethod::NONE,
      ObNullDistributeMethod::NONE, channels, keys, &incomplete, &collations,
      OB_REPARTITION_NO_REPARTITION, &ddl);
  CHECK(bad_shape.init() == OB_INVALID_ARGUMENT);
  ObSlaveMapPkeyRangeIdxCalc routing(exec, schema, &tablet, ObPQDistributeMethod::NONE,
      ObNullDistributeMethod::NONE, channels, keys, &functions, &collations,
      OB_REPARTITION_NO_REPARTITION, &ddl);
  ObEvalCtx::BatchInfoScopeGuard frame(eval);
  frame.set_batch_size(1); frame.set_batch_idx(0);
  const int resolves = provider.resolves_, comparisons = provider.comparisons_;
  const auto fill = [&](int row, int id) {
    CHECK(rows[row]->to_expr(keys, eval) == OB_SUCCESS);
    tablet.locate_datum_for_write(eval).set_int(id);
    tablet.set_evaluated_projected(eval);
    ddl.locate_datum_for_write(eval).set_int(-99);
  };
  const auto unchanged = [&] { CHECK(ddl.locate_expr_datum(eval).get_int() == -99); };
  const auto channel_for = [&](int row, int id) {
    const int range = expected(row);
    return id == 11 ? (range < 2 ? 2 : 4) : id == 22 ? 7 + 2 * range : 17;
  };
  const auto check_ddl = [&](int row, int id) {
    oceanbase::storage::ObTabletSliceParam slice(id == 33 ? 1 : 3, id == 33 ? 0 : expected(row));
    CHECK(ddl.locate_expr_datum(eval).get_int() == slice.slice_id_);
  };
  ObSliceIdxCalc::SliceIdxArray output;
  const int64_t *published_tablets = nullptr;
  CHECK(routing.get_previous_batch_tablet_ids(3, published_tablets) != OB_SUCCESS && !published_tablets);
  fill(0, 11);
  CHECK(routing.get_slice_indexes<ObSliceIdxCalc::SM_REPART_RANGE>(keys, eval, output) == OB_NOT_INIT);
  CHECK(output.at(0) == OB_INVALID_INDEX); unchanged();
  CHECK(routing.init() == OB_SUCCESS);
  CHECK(routing.init() == OB_INIT_TWICE);
  for (int id : {11, 22, 33}) for (int row = 0; row < int(rows.size()); ++row) {
    fill(row, id);
    CHECK(routing.get_slice_indexes<ObSliceIdxCalc::SM_REPART_RANGE>(keys, eval, output) == OB_SUCCESS);
    CHECK(output.at(0) == channel_for(row, id)); check_ddl(row, id);
  }
  for (int id : {0, 99, 44}) {
    fill(0, id);
    const int error = id == 0 ? OB_NO_PARTITION_FOR_GIVEN_VALUE : id == 99 ? OB_HASH_NOT_EXIST : OB_INVALID_ARGUMENT;
    CHECK(routing.get_slice_indexes<ObSliceIdxCalc::SM_REPART_RANGE>(keys, eval, output) == error);
    CHECK(output.at(0) == OB_INVALID_INDEX); unchanged();
  }
  class Cancel final : public ObIExtraStatusCheck {
  public:
    explicit Cancel(const int &calls) : calls_(calls), before_(calls) {}
    const char *name() const override { return "plugin-partition-range-cancel"; }
    int check() const override { return calls_ == before_ ? OB_SUCCESS : OB_TIMEOUT; }
  private:
    const int &calls_; int before_;
  };
  const char invalid = char(0xff);
  if (!native) {
    fill(0, 11);
    auto *info = dynamic_cast<PluginTypeValueExtraInfo *>(keys.at(0)->extra_info_);
    CHECK(info && info->valid() && info->ordering_);
    const int before_bad_binding = provider.comparisons_;
    ++info->ordering_->binding_.catalog_epoch;
    CHECK(routing.get_slice_indexes<ObSliceIdxCalc::SM_REPART_RANGE>(keys, eval, output) == OB_INVALID_DATA);
    CHECK(output.at(0) == OB_INVALID_INDEX); unchanged();
    CHECK(provider.comparisons_ == before_bad_binding);
    --info->ordering_->binding_.catalog_epoch;
    fill(0, 11);
    keys.at(0)->locate_expr_datum(eval).set_string(ObString(1, &invalid));
    CHECK(routing.get_slice_indexes<ObSliceIdxCalc::SM_REPART_RANGE>(keys, eval, output) == OB_INVALID_ARGUMENT);
    CHECK(output.at(0) == OB_INVALID_INDEX); unchanged();
    fill(0, 11);
    const int calls = provider.comparisons_;
    { Cancel cancel(provider.comparisons_); ObIExtraStatusCheck::Guard guard(exec, cancel);
      CHECK(routing.get_slice_indexes<ObSliceIdxCalc::SM_REPART_RANGE>(keys, eval, output) == OB_TIMEOUT); }
    CHECK(provider.comparisons_ == calls + 1 && output.at(0) == OB_INVALID_INDEX); unchanged();
    CHECK(routing.get_slice_indexes<ObSliceIdxCalc::SM_REPART_RANGE>(keys, eval, output) == OB_SUCCESS);
    check_ddl(0, 11);
  }
  if (eval.max_batch_size_ >= 3) {
    frame.set_batch_size(3);
    const int tablets[] = {11, 22, 33};
    const auto fill_batch = [&](int first) {
      for (int i = 0; i < 3; ++i) { frame.set_batch_idx(i); fill((first + i) % rows.size(), tablets[i]); }
    };
    const auto unchanged_batch = [&] {
      for (int i = 0; i < 3; ++i) { frame.set_batch_idx(i); unchanged(); }
    };
    uint64_t bits = 0; auto &skip = *to_bit_vector(&bits);
    int64_t *batch = nullptr;
    const auto route = [&](int size) {
      return routing.get_slice_idx_batch<ObSliceIdxCalc::SM_REPART_RANGE>(keys, eval, skip, size, batch);
    };
    std::vector<ObExpr *> chain;
    ObExpr *leaf = keys.at(0);
    bool has_function = false;
    if (!native) while (leaf->type_ != T_REF_COLUMN) {
      chain.push_back(leaf);
      has_function |= leaf->type_ == T_FUN_SYS_PLUGIN_FUNCTION;
      const int value_arg = leaf->type_ == T_FUN_SYS_PLUGIN_FUNCTION ? 1 : 0;
      CHECK(leaf->arg_cnt_ > value_arg && leaf->args_[value_arg]);
      leaf = leaf->args_[value_arg];
    }
    const auto recompute_keys = [&](int first) {
      if (has_function) {
        CHECK(leaf->is_batch_result());
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
      }
    };
    for (int first = 0; first < 7; ++first) {
      fill_batch(first); recompute_keys(first);
      const int scalars = provider.scalar_functions_, batches = provider.batch_calls("seekdb_rust_identity");
      CHECK(route(3) == OB_SUCCESS && batch);
      CHECK(routing.get_previous_batch_tablet_ids(3, published_tablets) == OB_SUCCESS && published_tablets);
      for (int i = 0; i < 3; ++i) CHECK(published_tablets[i] == tablets[i]);
      CHECK(routing.get_previous_batch_tablet_ids(2, published_tablets) == OB_STATE_NOT_MATCH && !published_tablets);
      CHECK(provider.scalar_functions_ == scalars);
      if (has_function) CHECK(provider.batch_calls("seekdb_rust_identity") > batches);
      for (int i = 0; i < 3; ++i) {
        frame.set_batch_idx(i);
        CHECK(batch[i] == channel_for((first + i) % rows.size(), tablets[i]));
        check_ddl((first + i) % rows.size(), tablets[i]);
      }
    }
    fill_batch(0); frame.set_batch_idx(2);
    tablet.locate_datum_for_write(eval).set_int(99);
    CHECK(route(3) == OB_HASH_NOT_EXIST && !batch); unchanged_batch();
    CHECK(routing.get_previous_batch_tablet_ids(3, published_tablets) == OB_STATE_NOT_MATCH && !published_tablets);
    skip.set(2);
    int64_t skipped_ddl = -99;
    ddl.locate_expr_datum(eval).ptr_ = reinterpret_cast<const char *>(&skipped_ddl);
    CHECK(route(3) == OB_SUCCESS && batch);
    CHECK(batch[0] == channel_for(0, 11) && batch[1] == channel_for(1, 22) && batch[2] == OB_INVALID_INDEX);
    CHECK(routing.get_previous_batch_tablet_ids(3, published_tablets) == OB_SUCCESS);
    CHECK(published_tablets[0] == 11 && published_tablets[1] == 22 && published_tablets[2] == OB_INVALID_INDEX);
    CHECK(ddl.locate_expr_datum(eval).ptr_ == reinterpret_cast<const char *>(&skipped_ddl));
    frame.set_batch_idx(2); unchanged(); skip.unset(2);
    if (!native) {
      fill_batch(0); frame.set_batch_idx(1);
      keys.at(0)->locate_expr_datum(eval).set_string(ObString(1, &invalid));
      CHECK(route(3) == OB_INVALID_ARGUMENT && !batch); unchanged_batch();
      fill_batch(0);
      const int calls = provider.comparisons_;
      { Cancel cancel(provider.comparisons_); ObIExtraStatusCheck::Guard guard(exec, cancel);
        CHECK(route(3) == OB_TIMEOUT && !batch); }
      CHECK(provider.comparisons_ == calls + 1); unchanged_batch();
      CHECK(route(3) == OB_SUCCESS && batch);
    }
    fill_batch(0);
    CHECK(route(0) == OB_SUCCESS && !batch); unchanged_batch();
    CHECK(routing.get_previous_batch_tablet_ids(3, published_tablets) == OB_STATE_NOT_MATCH && !published_tablets);
    CHECK(route(4) == OB_INVALID_ARGUMENT && !batch); unchanged_batch();
    CHECK(route(-1) == OB_INVALID_ARGUMENT && !batch); unchanged_batch();
    skip.set(0); skip.set(1); skip.set(2);
    CHECK(route(3) == OB_SUCCESS && batch);
    for (int i = 0; i < 3; ++i) CHECK(batch[i] == OB_INVALID_INDEX);
    unchanged_batch();
    CHECK(tablet_output && tablet_output->type_ == T_PDML_PARTITION_ID && tablet_output->is_batch_result());
    // The production send loop consumes a prefetched batch and writes the
    // generated PDML carrier; only the channel transport is replaced by capture.
    bits = 0; fill_batch(0); recompute_keys(0);
    using Slice = oceanbase::storage::ObTabletSliceParam;
    const std::vector<int64_t> ddl_ids = {Slice(3, expected(0)).slice_id_,
        Slice(3, expected(1)).slice_id_, Slice(1, 0).slice_id_};
    const int sent_scalars = provider.scalar_functions_, sent_batches = provider.batch_calls("seekdb_rust_identity");
    rust_tablet_transmit_test::run(exec, routing, *tablet_output, ddl,
        {channel_for(0, 11), channel_for(1, 22), channel_for(2, 33)}, {11,22,33}, ddl_ids);
    CHECK(provider.scalar_functions_ == sent_scalars);
    if (has_function) CHECK(provider.batch_calls("seekdb_rust_identity") > sent_batches);
    CHECK(routing.get_previous_batch_tablet_ids(3, published_tablets) == OB_SUCCESS);
    fill_batch(0); frame.set_batch_idx(2); tablet.locate_datum_for_write(eval).set_int(99);
    rust_tablet_transmit_test::run(exec, routing, *tablet_output, ddl,
        {channel_for(0, 11), channel_for(1, 22), 0}, {11,22,99}, ddl_ids, 0, OB_HASH_NOT_EXIST);
    unchanged_batch();
    CHECK(routing.get_previous_batch_tablet_ids(3, published_tablets) == OB_STATE_NOT_MATCH && !published_tablets);
    rust_tablet_transmit_test::run(exec, routing, *tablet_output, ddl,
        {channel_for(0, 11), channel_for(1, 22), 0}, {11,22,99}, ddl_ids, 4);
    frame.set_batch_idx(2); unchanged();
    frame.set_batch_idx(0);
    CHECK(routing.get_slice_indexes<ObSliceIdxCalc::SM_REPART_RANGE>(keys, eval, output) == OB_SUCCESS);
    CHECK(routing.get_previous_batch_tablet_ids(3, published_tablets) == OB_STATE_NOT_MATCH && !published_tablets);
    // Existing vectorized repartition calculators also use the new ID contract.
    // Exercise their actual send-loop specializations, not just the capability flag.
    ObAffinitizedRepartSliceIdxCalc affinity(exec, schema, &tablet, 18, channels,
        ObPQDistributeMethod::NONE, ObNullDistributeMethod::NONE,
        OB_REPARTITION_NO_REPARTITION, nullptr, nullptr, false);
    CHECK(affinity.init() == OB_SUCCESS); fill_batch(0);
    rust_tablet_transmit_test::run<ObSliceIdxCalc::AFFINITY_REPART>(exec, affinity, *tablet_output, ddl,
        {2,7,17}, {11,22,33}, {-99,-99,-99});
    CHECK(affinity.get_previous_batch_tablet_ids(3, published_tablets) == OB_SUCCESS);
    CHECK(affinity.destroy() == OB_SUCCESS);
    ObPxPartChInfo single_channels;
    for (auto pair : {std::make_pair(11,2), std::make_pair(22,7), std::make_pair(33,17)}) {
      CHECK(single_channels.part_ch_array_.push_back(ObPxPartChMapItem(pair.first, pair.second)) == OB_SUCCESS);
    }
    ObSlaveMapPkeyRandomIdxCalc random(exec, schema, &tablet, ObPQDistributeMethod::NONE,
        ObNullDistributeMethod::NONE, single_channels, OB_REPARTITION_NO_REPARTITION);
    CHECK(random.init() == OB_SUCCESS); fill_batch(0);
    rust_tablet_transmit_test::run<ObSliceIdxCalc::SM_REPART_RANDOM>(exec, random, *tablet_output, ddl,
        {2,7,17}, {11,22,33}, {-99,-99,-99});
    CHECK(random.get_previous_batch_tablet_ids(3, published_tablets) == OB_SUCCESS);
    CHECK(random.destroy() == OB_SUCCESS);
  }
  CHECK(provider.resolves_ == resolves);
  CHECK(native ? provider.comparisons_ == comparisons : provider.comparisons_ > comparisons);
  CHECK(routing.destroy() == OB_SUCCESS);
  CHECK(routing.get_previous_batch_tablet_ids(3, published_tablets) == OB_STATE_NOT_MATCH && !published_tablets);
  CHECK(routing.init() == OB_SUCCESS);
  frame.set_batch_size(1); frame.set_batch_idx(0); fill(0, 11);
  CHECK(routing.get_slice_indexes<ObSliceIdxCalc::SM_REPART_RANGE>(keys, eval, output) == OB_SUCCESS);
  CHECK(output.at(0) == channel_for(0, 11)); check_ddl(0, 11);
  CHECK(routing.destroy() == OB_SUCCESS);
  CHECK(partition_calc && tablet_output);
  rust_partition_expression_test::run(eval, provider, keys, functions, collations,
      rows, expected, tablet, *partition_calc->at(4), ddl, *tablet_output);
  rust_partition_expression_test::run_two_levels(eval, provider, keys, functions, collations,
      rows, sampled, expected, tablet, *partition_calc->at(1), *partition_calc->at(5),
      *partition_calc->at(6), ddl, *tablet_output, native);
}
} // namespace rust_partition_range_test
#endif
