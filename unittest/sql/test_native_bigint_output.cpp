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

#include "query/engine/basic/native_bigint_output.h"
#include "query/engine/basic/ob_row_to_expr_projector.h"
#include "data_plane/blocksstable/ob_storage_datum.h"
#include "data_plane/transaction/ob_tx_read_snapshot.h"
#include "sql/das/ob_das_scan_op.h"
#include "sql/engine/ob_exec_context.h"
#include "lib/charset/ob_charset.h"
#include <gtest/gtest.h>
#include <type_traits>

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::storage;
using oceanbase::blocksstable::ObStorageDatum;

namespace
{
static_assert(!std::is_copy_constructible<NativeBigintOutput>::value, "borrowed slots require a fixed owner");
static_assert(!std::is_move_constructible<NativeBigintOutput>::value, "moving invalidates generation and slots");

struct OutputView
{
  const Datum *values = nullptr;
  const SqlType *type = nullptr;
  int64_t count = 0;
  uint64_t generation = 0;
  const uint64_t *current_generation = nullptr;

  bool get(const NativeBigintOutput &output, int64_t idx = 0)
  { return output.try_get(idx, values, type, count, generation, current_generation); }
  bool current() const
  { return current_generation != nullptr && generation == *current_generation; }
};

// Uses the real scalar/batch storage-to-Expr projector. The mapping deliberately
// differs from both Expr order and selected output order.
struct ProjectorFrame
{
  ObArenaAllocator arena;
  ObExecContext exec;
  ObEvalCtx eval;
  ObPushdownExprSpec spec;
  ObPushdownOperator op;
  NativeBigintOutput native;
  ObRow2ExprsProjector projector;
  ObExpr exprs[3];
  char *frames[1];
  ObSEArray<ObExpr *, 3> selected;
  ObSEArray<int32_t, 3> indexes;
  ObStorageDatum storage[3];
  bool batch;

  explicit ProjectorFrame(bool vectorized = true)
    : arena(), exec(arena), eval(exec), spec(arena), op(eval, spec), native(),
      projector(arena), exprs(), frames(), selected(), indexes(), storage(), batch(vectorized)
  {
    frames[0] = static_cast<char *>(arena.alloc(4096));
    EXPECT_NE(nullptr, frames[0]);
    if (frames[0] == nullptr) { return; }
    MEMSET(frames[0], 0, 4096);
    exec.set_frames(frames);
    eval.frames_ = frames;
    spec.max_batch_size_ = batch ? 16 : 0;
    EXPECT_EQ(OB_SUCCESS, spec.access_exprs_.init(3));
    for (int64_t i = 0; i < 3; ++i) {
      ObExpr &expr = exprs[i];
      expr.type_ = T_REF_COLUMN;
      expr.arg_cnt_ = 0;
      expr.batch_result_ = batch;
      expr.batch_idx_mask_ = batch ? UINT64_MAX : 0;
      expr.frame_idx_ = 0;
      expr.datum_off_ = 1024 * i;
      expr.eval_info_off_ = 1024 * i + 256;
      expr.eval_flags_off_ = 1024 * i + 320;
      expr.res_buf_off_ = 1024 * i + 512;
      expr.res_buf_len_ = sizeof(int64_t);
      expr.obj_meta_.set_int();
      expr.datum_meta_ = ObDatumMeta(ObIntType, CS_TYPE_BINARY, 0);
      expr.obj_datum_map_ = OBJ_DATUM_8BYTE_DATA;
      for (int64_t j = 0; j < (batch ? 16 : 1); ++j) {
        new(frames[0] + expr.datum_off_ + j * sizeof(ObDatum)) ObDatum();
      }
      EXPECT_EQ(OB_SUCCESS, spec.access_exprs_.push_back(&expr));
      // e0 reads storage[1], e1 reads storage[2], e2 reads storage[0].
      EXPECT_EQ(OB_SUCCESS, indexes.push_back((i + 1) % 3));
      storage[i].reuse();
      storage[i].set_int(i == 0 ? INT64_MIN : i == 1 ? 19 : INT64_MAX);
    }
    // SELECT e2,e0,e2 exercises both reordering and a duplicate output.
    EXPECT_EQ(OB_SUCCESS, selected.push_back(&exprs[2]));
    EXPECT_EQ(OB_SUCCESS, selected.push_back(&exprs[0]));
    EXPECT_EQ(OB_SUCCESS, selected.push_back(&exprs[2]));
  }

  int bind(bool omit_shadow = false)
  {
    bool supported = false;
    int ret = native.prepare(selected, supported, omit_shadow);
    EXPECT_TRUE(supported);
    if (ret == OB_SUCCESS) {
      projector.bind_native_bigint_output(&native);
      ret = projector.init(spec.access_exprs_, op, indexes);
    }
    return ret;
  }

  int project(int64_t idx = 0, int64_t *nop_count = nullptr)
  {
    ObEvalCtx::BatchInfoScopeGuard guard(eval);
    guard.set_batch_idx(idx);
    guard.set_batch_size(batch ? 16 : 1);
    int16_t nops[3] = {};
    int64_t count = 0;
    const int ret = projector.project(spec.access_exprs_, storage, nops, count);
    if (nop_count != nullptr) { *nop_count = count; }
    return ret;
  }
};

TEST(NativeBigintOutput, actual_batch16_projector_preserves_shadow_and_maps_duplicate_reordered_columns)
{
  ProjectorFrame frame;
  ASSERT_EQ(OB_SUCCESS, frame.bind());
  ASSERT_EQ(OB_SUCCESS, frame.project());
  OutputView view;
  EXPECT_FALSE(view.get(frame.native)); // storage candidate is not a result yet
  frame.native.publish(OB_SUCCESS, 1, false, false);
  ASSERT_TRUE(view.get(frame.native));
  ASSERT_EQ(3, view.count);
  EXPECT_EQ(INT64_MIN, view.values[0].get_int());
  EXPECT_EQ(19, view.values[1].get_int());
  EXPECT_EQ(INT64_MIN, view.values[2].get_int());
  ASSERT_NE(nullptr, view.type);
  EXPECT_EQ(ObIntType, view.type->legacy_meta().get_type());
  EXPECT_TRUE(view.current());
  EXPECT_EQ(19, frame.exprs[0].locate_batch_datums(frame.eval)[0].get_int());
  EXPECT_EQ(INT64_MAX, frame.exprs[1].locate_batch_datums(frame.eval)[0].get_int());
  EXPECT_EQ(INT64_MIN, frame.exprs[2].locate_batch_datums(frame.eval)[0].get_int());
  // New values are inline, not borrowed from either storage or legacy Expr.
  frame.storage[0].set_int(22);
  frame.exprs[2].locate_batch_datums(frame.eval)[0].set_int(33);
  EXPECT_EQ(INT64_MIN, view.values[0].get_int());
  EXPECT_EQ(INT64_MIN, view.values[2].get_int());
}

TEST(NativeBigintOutput, native_only_projector_leaves_entire_frame_unchanged)
{
  ProjectorFrame frame;
  ASSERT_EQ(OB_SUCCESS, frame.bind(true));
  char before[4096];
  MEMCPY(before, frame.frames[0], sizeof(before));
  ASSERT_EQ(OB_SUCCESS, frame.project());
  EXPECT_EQ(0, MEMCMP(before, frame.frames[0], sizeof(before)));
  EXPECT_FALSE(frame.projector.can_read_legacy_projection());
  EXPECT_FALSE(frame.native.native_only_ready());
  ASSERT_EQ(OB_SUCCESS, frame.native.publish(OB_SUCCESS, 1, false, false));
  EXPECT_TRUE(frame.native.native_only_ready());
  OutputView view;
  ASSERT_TRUE(view.get(frame.native));
  EXPECT_EQ(INT64_MIN, view.values[0].get_int());
  EXPECT_EQ(19, view.values[1].get_int());
  frame.native.invalidate();
  EXPECT_FALSE(view.current());
  EXPECT_FALSE(frame.projector.can_read_legacy_projection());
}

TEST(NativeBigintOutput, native_only_publication_failure_cannot_claim_a_legacy_shadow)
{
  ProjectorFrame frame;
  ASSERT_EQ(OB_SUCCESS, frame.bind(true));
  for (int64_t count : {int64_t(1), int64_t(2)}) {
    ASSERT_EQ(OB_SUCCESS, frame.project());
    EXPECT_EQ(OB_ERR_UNEXPECTED, frame.native.publish(OB_SUCCESS, count, count == 1, false));
    EXPECT_FALSE(frame.native.native_only_ready());
    EXPECT_FALSE(frame.projector.can_read_legacy_projection());
    OutputView view;
    EXPECT_FALSE(view.get(frame.native));
    EXPECT_EQ(OB_SUCCESS, frame.native.publish(OB_SUCCESS, 1, false, false));
    EXPECT_FALSE(view.get(frame.native));
  }
}

TEST(NativeBigintOutput, unselected_nop_falls_back_before_any_partial_native_only_projection)
{
  ProjectorFrame frame;
  ASSERT_EQ(OB_SUCCESS, frame.bind(true));
  ASSERT_EQ(OB_SUCCESS, frame.project());
  ASSERT_EQ(OB_SUCCESS, frame.native.publish(OB_SUCCESS, 1, false, false));
  OutputView old;
  ASSERT_TRUE(old.get(frame.native));
  frame.storage[2].set_nop(); // e1 is not selected but still needs default fusion.
  int64_t nops = 0;
  ASSERT_EQ(OB_SUCCESS, frame.project(0, &nops));
  EXPECT_EQ(1, nops);
  EXPECT_FALSE(old.current());
  ASSERT_EQ(OB_SUCCESS, frame.native.publish(OB_SUCCESS, 1, false, false));
  EXPECT_TRUE(frame.projector.can_read_legacy_projection());
  EXPECT_FALSE(frame.native.native_only_ready());
  EXPECT_EQ(INT64_MIN, frame.exprs[2].locate_batch_datums(frame.eval)[0].get_int());
  EXPECT_EQ(19, frame.exprs[0].locate_batch_datums(frame.eval)[0].get_int());
}

TEST(NativeBigintOutput, native_only_mapping_requires_every_access_expression_to_be_plain_bigint)
{
  ProjectorFrame frame;
  frame.exprs[1].type_ = T_OP_ADD; // not in the output list
  ASSERT_EQ(OB_SUCCESS, frame.bind(true));
  ASSERT_EQ(OB_SUCCESS, frame.project());
  ASSERT_EQ(OB_SUCCESS, frame.native.publish(OB_SUCCESS, 1, false, false));
  EXPECT_TRUE(frame.projector.can_read_legacy_projection());
  OutputView view;
  EXPECT_FALSE(view.get(frame.native));
  EXPECT_EQ(INT64_MAX, frame.exprs[1].locate_batch_datums(frame.eval)[0].get_int());
}

TEST(NativeBigintOutput, actual_scalar_projector_preserves_typed_null_and_integer_extremes)
{
  ProjectorFrame frame(false);
  ASSERT_EQ(OB_SUCCESS, frame.bind());
  frame.storage[0].set_null();
  ASSERT_EQ(OB_SUCCESS, frame.project());
  frame.native.publish(OB_SUCCESS, 1, false, false);
  OutputView view;
  ASSERT_TRUE(view.get(frame.native));
  EXPECT_TRUE(view.values[0].is_null());
  EXPECT_TRUE(view.values[2].is_null());
  EXPECT_EQ(19, view.values[1].get_int());
  EXPECT_EQ(ObIntType, view.type->legacy_meta().get_type());
  EXPECT_TRUE(frame.exprs[2].locate_expr_datum(frame.eval).is_null());
  frame.storage[0].reuse();
  frame.storage[0].set_int(INT64_MAX);
  ASSERT_EQ(OB_SUCCESS, frame.project());
  EXPECT_FALSE(view.current());
  frame.native.publish(OB_SUCCESS, 1, false, false);
  ASSERT_TRUE(view.get(frame.native));
  EXPECT_EQ(INT64_MAX, view.values[0].get_int());
}

TEST(NativeBigintOutput, nop_uses_the_existing_default_fusion_path_and_invalidates_previous_view)
{
  ProjectorFrame frame;
  ASSERT_EQ(OB_SUCCESS, frame.bind());
  ASSERT_EQ(OB_SUCCESS, frame.project());
  frame.native.publish(OB_SUCCESS, 1, false, false);
  OutputView previous;
  ASSERT_TRUE(previous.get(frame.native));
  frame.storage[1].set_nop();
  int64_t nop_count = 0;
  ASSERT_EQ(OB_SUCCESS, frame.project(0, &nop_count));
  EXPECT_EQ(1, nop_count);
  EXPECT_FALSE(previous.current());
  frame.native.publish(OB_SUCCESS, 1, false, false);
  OutputView next;
  EXPECT_FALSE(next.get(frame.native));
  EXPECT_EQ(nullptr, next.values);
  EXPECT_EQ(nullptr, next.type);
}

TEST(NativeBigintOutput, unaligned_storage_payload_is_copied_directly_to_inline_scalar)
{
  ProjectorFrame frame;
  ASSERT_EQ(OB_SUCCESS, frame.bind());
  char bytes[sizeof(int64_t) + 1];
  const int64_t expected = INT64_MAX;
  MEMCPY(bytes + 1, &expected, sizeof(expected));
  frame.storage[0].ptr_ = bytes + 1;
  frame.storage[0].pack_ = sizeof(expected);
  // Test the native capture directly: the retained legacy projector still has
  // its historical scalar-buffer alignment contract.
  frame.native.project(frame.storage, 0);
  frame.native.publish(OB_SUCCESS, 1, false, false);
  OutputView view;
  ASSERT_TRUE(view.get(frame.native));
  MEMSET(bytes, 0, sizeof(bytes));
  EXPECT_EQ(expected, view.values[0].get_int());
  EXPECT_EQ(expected, view.values[2].get_int());
}

TEST(NativeBigintOutput, final_result_gate_rejects_errors_end_skips_empty_or_multiple_rows)
{
  ProjectorFrame frame;
  ASSERT_EQ(OB_SUCCESS, frame.bind());
  struct Gate { int result; int64_t count; bool end; bool skipped; };
  const Gate gates[] = {
    {OB_ERR_UNEXPECTED, 1, false, false}, {OB_ITER_END, 0, true, false},
    {OB_SUCCESS, 0, false, false}, {OB_SUCCESS, 2, false, false},
    {OB_SUCCESS, 1, true, false}, {OB_SUCCESS, 1, false, true}
  };
  for (const Gate &gate : gates) {
    ASSERT_EQ(OB_SUCCESS, frame.project());
    frame.native.publish(OB_SUCCESS, 1, false, false);
    OutputView previous;
    ASSERT_TRUE(previous.get(frame.native));
    frame.native.publish(gate.result, gate.count, gate.end, gate.skipped);
    EXPECT_FALSE(previous.current());
    OutputView next;
    EXPECT_FALSE(next.get(frame.native));
    // A second final-success call cannot resurrect a failed candidate.
    frame.native.publish(OB_SUCCESS, 1, false, false);
    EXPECT_FALSE(next.get(frame.native));
  }
}

TEST(NativeBigintOutput, nonzero_batch_index_and_explicit_advance_cannot_reuse_an_old_row)
{
  ProjectorFrame frame;
  ASSERT_EQ(OB_SUCCESS, frame.bind());
  ASSERT_EQ(OB_SUCCESS, frame.project());
  frame.native.publish(OB_SUCCESS, 1, false, false);
  OutputView previous, next;
  ASSERT_TRUE(previous.get(frame.native));
  EXPECT_FALSE(next.get(frame.native, 1));
  frame.native.invalidate();
  EXPECT_FALSE(previous.current());
  EXPECT_FALSE(next.get(frame.native));
  ASSERT_EQ(OB_SUCCESS, frame.project(1));
  frame.native.publish(OB_SUCCESS, 1, false, false);
  EXPECT_FALSE(next.get(frame.native));
}

TEST(NativeBigintOutput, malformed_scalar_and_special_flags_fall_back_without_partial_publication)
{
  ProjectorFrame frame;
  ASSERT_EQ(OB_SUCCESS, frame.bind());
  const ObDatum::FlagType flags[] = {ObDatum::OUTROW, ObDatum::EXT, ObDatum::HAS_LOB_HEADER};
  for (const auto flag : flags) {
    frame.storage[0].reuse();
    frame.storage[0].set_int(12);
    frame.storage[0].flag_ = flag;
    frame.native.project(frame.storage, 0);
    frame.native.publish(OB_SUCCESS, 1, false, false);
    OutputView view;
    EXPECT_FALSE(view.get(frame.native));
  }
  frame.storage[0].reuse();
  frame.storage[0].set_int(12);
  for (uint32_t length : {0u, 4u, 16u}) {
    frame.storage[0].pack_ = length;
    frame.native.project(frame.storage, 0);
    frame.native.publish(OB_SUCCESS, 1, false, false);
    OutputView view;
    EXPECT_FALSE(view.get(frame.native));
  }
  frame.storage[0].ptr_ = nullptr;
  frame.storage[0].pack_ = sizeof(int64_t);
  frame.native.project(frame.storage, 0);
  frame.native.publish(OB_SUCCESS, 1, false, false);
  OutputView view;
  EXPECT_FALSE(view.get(frame.native));
}

TEST(NativeBigintOutput, prepare_rejects_non_bigint_computed_or_virtual_outputs_and_bad_mapping)
{
  ProjectorFrame frame;
  bool supported = true;
  frame.exprs[2].obj_meta_.set_uint64();
  EXPECT_EQ(OB_SUCCESS, frame.native.prepare(frame.selected, supported));
  EXPECT_FALSE(supported);
  frame.exprs[2].obj_meta_.set_int();
  frame.exprs[2].type_ = T_OP_ADD;
  EXPECT_EQ(OB_SUCCESS, frame.native.prepare(frame.selected, supported));
  EXPECT_FALSE(supported);
  frame.exprs[2].type_ = T_REF_COLUMN;
  frame.exprs[2].arg_cnt_ = 1;
  EXPECT_EQ(OB_SUCCESS, frame.native.prepare(frame.selected, supported));
  EXPECT_FALSE(supported);
  frame.exprs[2].arg_cnt_ = 0;
  ASSERT_EQ(OB_SUCCESS, frame.native.prepare(frame.selected, supported));
  ASSERT_TRUE(supported);
  frame.indexes.at(2) = -1;
  frame.native.bind_projector(frame.spec.access_exprs_, frame.indexes);
  frame.native.project(frame.storage, 0);
  frame.native.publish(OB_SUCCESS, 1, false, false);
  OutputView view;
  EXPECT_FALSE(view.get(frame.native));
  frame.indexes.at(2) = 0;
  frame.spec.access_exprs_.at(2) = &frame.exprs[1]; // e2 is missing
  frame.native.bind_projector(frame.spec.access_exprs_, frame.indexes);
  frame.native.project(frame.storage, 0);
  frame.native.publish(OB_SUCCESS, 1, false, false);
  EXPECT_FALSE(view.get(frame.native));
}

TEST(NativeBigintOutput, projector_detach_destroy_and_rebind_invalidate_views_but_keep_legacy_projection)
{
  ProjectorFrame frame;
  ASSERT_EQ(OB_SUCCESS, frame.bind());
  ASSERT_EQ(OB_SUCCESS, frame.project());
  frame.native.publish(OB_SUCCESS, 1, false, false);
  OutputView previous;
  ASSERT_TRUE(previous.get(frame.native));
  frame.projector.bind_native_bigint_output(nullptr);
  EXPECT_FALSE(previous.current());
  ASSERT_EQ(OB_SUCCESS, frame.project());
  OutputView next;
  EXPECT_FALSE(next.get(frame.native));
  // Existing cached projector initialization must bind the new mapping too.
  frame.projector.bind_native_bigint_output(&frame.native);
  ASSERT_EQ(OB_SUCCESS, frame.projector.init(frame.spec.access_exprs_, frame.op, frame.indexes));
  ASSERT_EQ(OB_SUCCESS, frame.project());
  frame.native.publish(OB_SUCCESS, 1, false, false);
  ASSERT_TRUE(next.get(frame.native));
  frame.projector.destroy();
  EXPECT_FALSE(next.current());
}

TEST(NativeBigintOutput, das_init_release_rescan_and_failed_reuse_invalidate_even_before_storage_reopens)
{
  ProjectorFrame frame;
  ASSERT_EQ(OB_SUCCESS, frame.bind());
  ObDASScanCtDef ctdef(frame.arena);
  ObDASScanRtDef rtdef;
  rtdef.eval_ctx_ = &frame.eval;
  rtdef.stmt_allocator_.set_alloc(&frame.arena);
  rtdef.scan_allocator_.set_alloc(&frame.arena);
  ASSERT_EQ(OB_SUCCESS, rtdef.init_pd_op(frame.exec, ctdef));
  rtdef.p_row2exprs_projector_->bind_native_bigint_output(&frame.native);
  ObDASScanOp task(frame.arena);
  task.set_scan_ctdef(&ctdef);
  task.set_scan_rtdef(&rtdef);
  transaction::ObTxReadSnapshot snapshot;
  snapshot.init_none_read();
  task.set_snapshot(&snapshot);
  // The restoration/invalidation methods themselves are production code;
  // this fixture opens no storage service and supplies no fake retry logic.
  auto publish = [&]() {
    frame.native.project(frame.storage, 0);
    frame.native.publish(OB_SUCCESS, 1, false, false);
  };
  publish();
  OutputView view;
  ASSERT_TRUE(view.get(frame.native));
  ASSERT_EQ(OB_SUCCESS, task.init_scan_param());
  EXPECT_FALSE(view.current());
  publish();
  ASSERT_TRUE(view.get(frame.native));
  ASSERT_EQ(OB_SUCCESS, task.release_op());
  EXPECT_FALSE(view.current());
  publish();
  ASSERT_TRUE(view.get(frame.native));
  EXPECT_EQ(OB_ERR_UNEXPECTED, task.rescan()); // no downstream iterator
  EXPECT_FALSE(view.current());
  publish();
  ASSERT_TRUE(view.get(frame.native));
  EXPECT_EQ(OB_ERR_UNEXPECTED, task.reuse_iter());
  EXPECT_FALSE(view.current());
}
} // namespace

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
