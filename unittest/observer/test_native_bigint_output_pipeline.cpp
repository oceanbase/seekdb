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

#include "observer/mysql/obsm_row.h"
#include "sql/engine/table/ob_table_scan_op.h"
#include "sql/engine/ob_physical_plan.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "sql/executor/ob_execute_result.h"
#include "sql/session/ob_sql_session_info.h"
#include "storage/access/ob_multiple_merge.h"
#include "storage/access/ob_table_access_param.h"
#include "storage/access/ob_table_read_info.h"
#include "data_plane/access/ob_table_scan_param.h"
#include "data_plane/blocksstable/ob_storage_datum.h"
#include "data_plane/transaction/ob_tx_read_snapshot.h"
#include "share/rc/ob_server_runtime.h"
#include "lib/container/ob_array_helper.h"
#include "lib/oblog/ob_log.h"
#include <gtest/gtest.h>
#include <string>

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::storage;
using namespace oceanbase::obmysql;
using namespace oceanbase::blocksstable;

namespace
{
// Supply an already-admitted input and an already-decoded storage row. Keep the
// production output gate, generic batch wrapper, zero-argument br_it_, DAS
// open/scan/release, projector, default fusion and result/protocol consumers.
// No test-only access or branching is added to production classes.
template <typename Tag, typename Tag::Type Member>
struct PrivateMember
{
  friend typename Tag::Type member(Tag) { return Member; }
};
struct InputReady
{
  using Type = bool sql::ObTableScanOp::*;
  friend Type member(InputReady);
};
template struct PrivateMember<InputReady, &sql::ObTableScanOp::native_point_get_ready_>;
struct ProcessRow
{
  using Type = int (ObMultipleMerge::*)(bool, ObDatumRow &, ObDatumRow *&);
  friend Type member(ProcessRow);
};
template struct PrivateMember<ProcessRow, &ObMultipleMerge::process_fuse_row>;

class DebugLogScope final
{
public:
  DebugLogScope() : saved_level_(ObThreadLogLevelUtils::get_level())
  { ObThreadLogLevelUtils::init(static_cast<int8_t>(OB_LOG_LEVEL_DEBUG)); }
  ~DebugLogScope() { ObThreadLogLevelUtils::init(saved_level_); }
private:
  int8_t saved_level_;
};

class DefaultReadInfo final : public ObTableReadInfo
{
public:
  const ObIArray<share::schema::ObColumnParam *> *get_columns() const override { return &columns_; }
  ObSEArray<share::schema::ObColumnParam *, 3> columns_;
};

class FusedRows final : public ObMultipleMerge
{
public:
  int init_fixture(ObIAllocator &arena, ObTableAccessParam &param, ObTableAccessContext &context)
  {
    access_param_ = &param;
    access_ctx_ = &context;
    need_fill_default_ = true;
    int ret = cur_row_.init(arena, 3);
    if (ret == OB_SUCCESS) { ret = nop_pos_.init(arena, 3); }
    return ret;
  }
  int process(ObDatumRow &raw)
  {
    ObDatumRow *output = nullptr;
    const int ret = (this->*member(ProcessRow{}))(false, raw, output);
    return ret == OB_SUCCESS && output == nullptr ? OB_ERR_UNEXPECTED : ret;
  }
protected:
  int calc_scan_range() override { return OB_NOT_SUPPORTED; }
  int construct_iters() override { return OB_NOT_SUPPORTED; }
  int is_range_valid() const override { return OB_NOT_SUPPORTED; }
  int inner_get_next_row(ObDatumRow &) override { return OB_NOT_SUPPORTED; }
  int pause(bool &do_pause) override { do_pause = false; return OB_SUCCESS; }
};

class TabletRows final : public ObNewRowIterator
{
public:
  int get_next_row(ObNewRow *&row) override { row = nullptr; return OB_NOT_SUPPORTED; }
  int get_next_rows(int64_t &count, int64_t capacity) override
  {
    ++advances_;
    last_capacity_ = capacity;
    count = 0;
    if (emitted_ || miss_) { return OB_ITER_END; }
    emitted_ = true;
    const int ret = fused_.process(raw_);
    if (ret != OB_SUCCESS) { return ret; }
    if (after_project_error_ != OB_SUCCESS) { return after_project_error_; }
    count = 1;
    return OB_SUCCESS;
  }
  void reset() override { emitted_ = false; }
  ObDatumRow raw_;
  FusedRows fused_;
  bool emitted_ = false;
  bool miss_ = false;
  int after_project_error_ = OB_SUCCESS;
  int advances_ = 0;
  int64_t last_capacity_ = 0;
};

class TabletService final : public ObITabletScan
{
public:
  int table_scan(ObVTableScanParam &base, ObNewRowIterator *&result) override
  {
    ++opens_;
    ObTableScanParam &param = static_cast<ObTableScanParam &>(base);
    if (!param.is_valid()) { return OB_INVALID_ARGUMENT; }
    int ret = param.row2exprs_projector_->init(*param.output_exprs_, *param.op_, indexes_);
    if (ret == OB_SUCCESS) {
      access_.output_exprs_ = param.output_exprs_;
      access_.row2exprs_projector_ = param.row2exprs_projector_;
      access_.iter_param_.op_ = param.op_;
      access_.iter_param_.read_info_ = &read_info_;
      access_.iter_param_.out_cols_project_ = &indexes_;
      // Poison every access datum, including the unselected expression, after
      // projector initialization has established the real frame destinations.
      ObEvalCtx &eval = param.op_->get_eval_ctx();
      for (int64_t i = 0; i < 3; ++i) {
        ObExpr &expr = *param.output_exprs_->at(i);
        char *buffer = eval.frames_[expr.frame_idx_] + expr.res_buf_off_;
        MEMSET(buffer, 0x5a, 16 * sizeof(int64_t));
        for (int64_t j = 0; j < 16; ++j) {
          ObDatum &datum = expr.locate_batch_datums(eval)[j];
          datum.ptr_ = unreadable_poison_ ? reinterpret_cast<const char *>(uintptr_t(1))
                                       : buffer + j * sizeof(int64_t);
          datum.pack_ = sizeof(int64_t);
        }
        MEMCPY(saved_datums_[i], expr.locate_batch_datums(eval), sizeof(saved_datums_[i]));
      }
      rows_.reset();
      active_ = true;
      result = &rows_;
    }
    return ret;
  }
  int reuse_scan_iter(bool, ObNewRowIterator *iter) override
  { EXPECT_EQ(&rows_, iter); rows_.reset(); return OB_SUCCESS; }
  int revert_scan_iter(ObNewRowIterator *iter) override
  { EXPECT_EQ(&rows_, iter); active_ = false; ++releases_; return OB_SUCCESS; }

  TabletRows rows_;
  ObTableAccessParam access_;
  ObTableAccessContext context_;
  DefaultReadInfo read_info_;
  ObSEArray<int32_t, 3> indexes_;
  char saved_datums_[3][16 * sizeof(ObDatum)] = {};
  bool active_ = false;
  bool unreadable_poison_ = false;
  int opens_ = 0;
  int releases_ = 0;
};

class PreparedScanTask final : public ObDASScanOp
{
public:
  using ObDASScanOp::ObDASScanOp;
  using ObDASScanOp::get_output_result_iter;
};

class PreparedTableScan final : public sql::ObTableScanOp
{
public:
  PreparedTableScan(ObExecContext &ctx, const ObOpSpec &spec, PreparedScanTask &task)
    : sql::ObTableScanOp(ctx, spec, nullptr), task_(task) {}
  ObDASScanRtDef &runtime() { return tsc_rtdef_.scan_rtdef_; }
  void adopt_task_result() { output_ = static_cast<ObDASIter *>(task_.get_output_result_iter()); }
  int inner_open() override
  {
    reset_native_bigint_consumer();
    ObDASScanRtDef &rtdef = runtime();
    rtdef.eval_ctx_ = &eval_ctx_;
    rtdef.stmt_allocator_.set_alloc(&ctx_.get_allocator());
    rtdef.scan_allocator_.set_alloc(&ctx_.get_allocator());
    rtdef.timeout_ts_ = INT64_MAX;
    return rtdef.init_pd_op(ctx_, static_cast<const ObTableScanSpec &>(spec_).tsc_ctdef_.scan_ctdef_);
  }
  int do_init_before_get_row() override
  {
    int ret = OB_SUCCESS;
    if (need_init_before_get_row_) {
      this->*member(InputReady{}) = true;
      ret = prepare_native_bigint_output();
      if (ret == OB_SUCCESS) { ret = task_.open_op(); }
      if (ret == OB_SUCCESS) {
        adopt_task_result();
        need_init_before_get_row_ = false;
      }
    }
    return ret;
  }
private:
  PreparedScanTask &task_;
};

int forbidden_eval(const ObExpr &, ObEvalCtx &, const ObBitVector &, int64_t)
{ return OB_ERR_UNEXPECTED; }

class NativeBigintOutputPipeline : public ::testing::Test
{
protected:
  NativeBigintOutputPipeline()
    : arena_(), exec_(arena_), session_(), plan_(), spec_(arena_, PHY_TABLE_SCAN),
      task_(arena_), root_(exec_, spec_, task_), batch_info_(root_.get_eval_ctx()), result_(), snapshot_(), key_datums_(), key_(), keys_(),
      exprs_(), frames_(), tablet_(), old_service_(nullptr), can_release_(false), opened_(false)
  {}
  void SetUp() override
  {
    old_service_ = share::server_service<ObITabletScan>();
    share::bind_server_service<ObITabletScan>(&tablet_);
    ASSERT_EQ(OB_SUCCESS, session_.test_init(1, 1, nullptr));
    exec_.set_my_session(&session_);
    ASSERT_EQ(OB_SUCCESS, exec_.create_physical_plan_ctx());
    exec_.get_physical_plan_ctx()->set_timeout_timestamp(INT64_MAX);
    plan_.set_batch_size(16);
    spec_.plan_ = &plan_;
    spec_.max_batch_size_ = 16;
    ObDASScanCtDef &ctdef = spec_.tsc_ctdef_.scan_ctdef_;
    ctdef.ref_table_id_ = 1000001;
    ctdef.schema_version_ = 1;
    ctdef.is_get_ = true;
    ctdef.pd_expr_spec_.max_batch_size_ = 16;
    frames_[0] = static_cast<char *>(arena_.alloc(4096));
    ASSERT_NE(nullptr, frames_[0]);
    MEMSET(frames_[0], 0, 4096);
    exec_.set_frames(frames_);
    root_.get_eval_ctx().frames_ = frames_;
    root_.get_eval_ctx().max_batch_size_ = 16;
    batch_info_.set_batch_size(16);
    ASSERT_EQ(OB_SUCCESS, ctdef.pd_expr_spec_.access_exprs_.init(3));
    ASSERT_EQ(OB_SUCCESS, ctdef.result_output_.init(3));
    ASSERT_EQ(OB_SUCCESS, spec_.output_.init(3));
    ASSERT_EQ(OB_SUCCESS, tablet_.rows_.raw_.init(arena_, 3));
    ASSERT_EQ(OB_SUCCESS, tablet_.rows_.fused_.init_fixture(arena_, tablet_.access_, tablet_.context_));
    for (int64_t i = 0; i < 3; ++i) {
      ObExpr &expr = exprs_[i];
      expr.type_ = T_REF_COLUMN;
      expr.arg_cnt_ = 0;
      expr.batch_result_ = true;
      expr.batch_idx_mask_ = UINT64_MAX;
      expr.frame_idx_ = 0;
      expr.datum_off_ = 1024 * i;
      expr.eval_info_off_ = 1024 * i + 256;
      expr.eval_flags_off_ = 1024 * i + 320;
      expr.res_buf_off_ = 1024 * i + 512;
      expr.res_buf_len_ = sizeof(int64_t);
      expr.obj_meta_.set_int();
      expr.datum_meta_ = ObDatumMeta(ObIntType, CS_TYPE_BINARY, 0);
      expr.obj_datum_map_ = OBJ_DATUM_8BYTE_DATA;
      for (int64_t j = 0; j < 16; ++j) { new(frames_[0] + expr.datum_off_ + j * sizeof(ObDatum)) ObDatum(); }
      ASSERT_EQ(OB_SUCCESS, ctdef.pd_expr_spec_.access_exprs_.push_back(&expr));
      ASSERT_EQ(OB_SUCCESS, ctdef.result_output_.push_back(&expr));
      ASSERT_EQ(OB_SUCCESS, tablet_.indexes_.push_back((i + 1) % 3));
      auto *column = OB_NEWx(share::schema::ObColumnParam, &arena_, arena_);
      ASSERT_NE(nullptr, column);
      DatumValue default_value;
      ASSERT_EQ(OB_SUCCESS, default_value.set_int(90 + i, arena_));
      ASSERT_EQ(OB_SUCCESS, column->set_orig_default_value(default_value));
      ASSERT_EQ(OB_SUCCESS, tablet_.read_info_.columns_.push_back(column));
      tablet_.rows_.raw_.storage_datums_[i].set_int(i == 0 ? INT64_MIN : i == 1 ? 19 : INT64_MAX);
    }
    // SELECT e2,e0,e2; the unselected e1 is mapped last in storage.
    ASSERT_EQ(OB_SUCCESS, spec_.output_.push_back(&exprs_[2]));
    ASSERT_EQ(OB_SUCCESS, spec_.output_.push_back(&exprs_[0]));
    ASSERT_EQ(OB_SUCCESS, spec_.output_.push_back(&exprs_[2]));
    snapshot_.init_none_read();
    task_.set_scan_ctdef(&ctdef);
    task_.set_scan_rtdef(&root_.runtime());
    task_.set_snapshot(&snapshot_);
    task_.set_tablet_id(ObTabletID(1000001));
    // Admission is an explicit fixture input, not the subject of this test.
    // Keep public storage descriptors and their local payload buffers alive
    // through DAS release; this mock tablet never performs a Memtable lookup.
    for (int64_t i = 0; i < 3; ++i) {
      key_datums_[i].reuse();
      key_datums_[i].set_int(i + 1);
    }
    key_.datums_ = key_datums_;
    key_.count_ = 3;
    keys_.init(1, &key_, 1);
    task_.set_native_point_get_keys(&keys_);
    result_.set_static_engine_root(&root_);
    ASSERT_EQ(OB_SUCCESS, result_.open(exec_));
    opened_ = true;
    ASSERT_EQ(OB_SUCCESS, task_.init_scan_param());
    can_release_ = true;
  }
  void TearDown() override
  {
    if (can_release_) { EXPECT_EQ(OB_SUCCESS, task_.release_op()); }
    if (opened_) { EXPECT_EQ(OB_SUCCESS, result_.close(exec_)); }
    EXPECT_FALSE(tablet_.active_);
    share::bind_server_service<ObITabletScan>(old_service_);
    for (int64_t i = 0; i < tablet_.read_info_.columns_.count(); ++i) {
      tablet_.read_info_.columns_.at(i)->~ObColumnParam();
    }
  }
  void arm()
  {
    bool accepted = false;
    ASSERT_EQ(0, tablet_.rows_.advances_);
    ASSERT_EQ(OB_SUCCESS, result_.try_enable_native_bigint_output(3, accepted));
    ASSERT_TRUE(accepted);
    ASSERT_EQ(0, tablet_.opens_);
  }
  void expect_values(const ResultRowView &view, int64_t first, int64_t second)
  {
    ASSERT_EQ(3, view.count());
    for (int64_t i = 0; i < 3; ++i) {
      DatumValue value;
      ASSERT_EQ(OB_SUCCESS, view.get_value(i, value));
      EXPECT_EQ(i == 1 ? second : first, value.get_int());
    }
  }
  void expect_poison_unchanged()
  {
    for (int64_t i = 0; i < 3; ++i) {
      EXPECT_EQ(0, MEMCMP(tablet_.saved_datums_[i], exprs_[i].locate_batch_datums(root_.get_eval_ctx()),
                          sizeof(tablet_.saved_datums_[i])));
      const char *data = frames_[0] + exprs_[i].res_buf_off_;
      for (int64_t j = 0; j < 16 * static_cast<int64_t>(sizeof(int64_t)); ++j) { EXPECT_EQ(0x5a, data[j]); }
      EXPECT_FALSE(exprs_[i].get_eval_info(root_.get_eval_ctx()).projected_);
    }
  }
  ObArenaAllocator arena_;
  ObExecContext exec_;
  ObSQLSessionInfo session_;
  ObPhysicalPlan plan_;
  ObTableScanSpec spec_;
  PreparedScanTask task_;
  PreparedTableScan root_;
  ObEvalCtx::BatchInfoScopeGuard batch_info_;
  ObExecuteResult result_;
  transaction::ObTxReadSnapshot snapshot_;
  ObStorageDatum key_datums_[3];
  DatumLookupKey key_;
  ObArrayHelper<DatumLookupKey> keys_;
  ObExpr exprs_[3];
  char *frames_[1];
  TabletService tablet_;
  ObITabletScan *old_service_;
  bool can_release_;
  bool opened_;
};

TEST_F(NativeBigintOutputPipeline, default_batch16_getter_skips_old_frames_and_encodes_native_values)
{
  DebugLogScope debug_log;
  ASSERT_TRUE(OB_LOGGER.need_to_print(OB_LOG_ROOT::M_SQL, OB_LOG_LEVEL_DEBUG));
  // NDEBUG without ENABLE_DEBUG_LOG removes DEBUG print code at compile time.
  // Preserve that validation boundary in the test XML instead of treating a
  // release run as proof that the debug-only print guards actually executed.
  RecordProperty("debug_log_calls_compiled", IS_LOG_ENABLED(DEBUG) ? 1 : 0);
  tablet_.unreadable_poison_ = true;
  arm();
  // If generic eval is accidentally retained, it fails instead of hiding the
  // missing projection behind T_REF_COLUMN's normal null evaluator.
  for (ObExpr &expr : exprs_) { expr.eval_batch_func_ = forbidden_eval; }
  ResultRowView view;
  ASSERT_EQ(OB_SUCCESS, result_.get_next_result_row(exec_, view));
  ASSERT_TRUE(view.is_inline_datum_row());
  EXPECT_FALSE(view.has_legacy_datums());
  expect_values(view, INT64_MIN, 19);
  EXPECT_EQ(16, tablet_.rows_.last_capacity_);
  EXPECT_EQ(1, root_.get_monitor_info().output_row_count_);
  EXPECT_EQ(1, root_.get_monitor_info().output_batches_);
  expect_poison_unchanged();
  ObSEArray<ObField, 3> fields;
  ObSEArray<int64_t, 3> indexes;
  ObSEArray<DatumValue, 3> fallback;
  for (int64_t i = 0; i < 3; ++i) {
    ObField field;
    field.type_meta_.set_int();
    ASSERT_TRUE(DatumMysqlRow::can_encode_inline_bigint_field(field));
    ASSERT_EQ(OB_SUCCESS, fields.push_back(field));
    ASSERT_EQ(OB_SUCCESS, indexes.push_back(-3));
  }
  ObDataTypeCastParams params;
  for (const auto protocol : {TEXT, BINARY}) {
    DatumMysqlRow row(protocol, view, indexes, fallback, params, session_, fields, CHARSET_UTF8MB4, nullptr);
    for (int64_t i = 0; i < 3; ++i) {
      ObMySQLCellValue cell;
      ASSERT_EQ(OB_SUCCESS, row.build_cell_value(i, arena_, cell));
      const int64_t expected = i == 1 ? 19 : INT64_MIN;
      if (protocol == TEXT) { EXPECT_EQ(std::to_string(expected), std::string(cell.get_bytes(), cell.get_bytes_len())); }
      else { EXPECT_EQ(static_cast<uint64_t>(expected), cell.value_); }
    }
  }
  expect_poison_unchanged();
  ResultRowView end;
  ASSERT_EQ(OB_ITER_END, result_.get_next_result_row(exec_, end));
  EXPECT_FALSE(view.inline_row_valid());
  DatumValue stale;
  EXPECT_EQ(OB_STATE_NOT_MATCH, view.get_value(0, stale));
  bool accepted = true;
  EXPECT_EQ(OB_STATE_NOT_MATCH, result_.try_enable_native_bigint_output(3, accepted));
  EXPECT_FALSE(accepted);
}

TEST_F(NativeBigintOutputPipeline, unsigned_field_rejects_handshake_before_first_advance_and_keeps_complete_shadow)
{
  ObField field;
  field.type_meta_.set_int();
  field.flags_ = UNSIGNED_FLAG;
  EXPECT_FALSE(DatumMysqlRow::can_encode_inline_bigint_field(field));
  EXPECT_EQ(0, tablet_.rows_.advances_);
  // The driver does not call the handshake for a rejected field.
  ResultRowView view;
  ASSERT_EQ(OB_SUCCESS, result_.get_next_result_row(exec_, view));
  EXPECT_FALSE(view.is_inline_datum_row());
  EXPECT_TRUE(view.has_legacy_datums());
  expect_values(view, INT64_MIN, 19);
  EXPECT_EQ(INT64_MAX, exprs_[1].locate_batch_datums(root_.get_eval_ctx())[0].get_int());
  EXPECT_TRUE(exprs_[2].get_eval_info(root_.get_eval_ctx()).projected_);
}

TEST_F(NativeBigintOutputPipeline, selected_nop_runs_same_row_through_real_default_fusion)
{
  arm();
  tablet_.rows_.raw_.storage_datums_[0].set_nop();
  ResultRowView view;
  ASSERT_EQ(OB_SUCCESS, result_.get_next_result_row(exec_, view));
  EXPECT_FALSE(view.is_inline_datum_row());
  EXPECT_TRUE(view.has_legacy_datums());
  expect_values(view, 90, 19);
  EXPECT_EQ(1, tablet_.rows_.advances_);
  EXPECT_EQ(INT64_MAX, exprs_[1].locate_batch_datums(root_.get_eval_ctx())[0].get_int());
}

TEST_F(NativeBigintOutputPipeline, late_unselected_nop_preflights_all_access_values_and_fuses_default)
{
  arm();
  tablet_.rows_.raw_.storage_datums_[2].set_nop();
  ResultRowView view;
  ASSERT_EQ(OB_SUCCESS, result_.get_next_result_row(exec_, view));
  EXPECT_FALSE(view.is_inline_datum_row());
  EXPECT_TRUE(view.has_legacy_datums());
  expect_values(view, INT64_MIN, 19);
  EXPECT_EQ(92, exprs_[1].locate_batch_datums(root_.get_eval_ctx())[0].get_int());
  EXPECT_EQ(1, tablet_.rows_.advances_);
}

TEST_F(NativeBigintOutputPipeline, null_default_is_materialized_by_legacy_fusion)
{
  arm();
  DatumValue null;
  null.set_null();
  ASSERT_EQ(OB_SUCCESS, tablet_.read_info_.columns_.at(0)->set_orig_default_value(null));
  tablet_.rows_.raw_.storage_datums_[0].set_nop();
  ResultRowView view;
  ASSERT_EQ(OB_SUCCESS, result_.get_next_result_row(exec_, view));
  ASSERT_TRUE(view.has_legacy_datums());
  DatumValue value;
  ASSERT_EQ(OB_SUCCESS, view.get_value(0, value));
  EXPECT_TRUE(value.is_null());
  ASSERT_EQ(OB_SUCCESS, view.get_value(2, value));
  EXPECT_TRUE(value.is_null());
}

TEST_F(NativeBigintOutputPipeline, native_null_is_typed_and_never_reads_poison)
{
  arm();
  tablet_.rows_.raw_.storage_datums_[0].set_null();
  ResultRowView view;
  ASSERT_EQ(OB_SUCCESS, result_.get_next_result_row(exec_, view));
  ASSERT_TRUE(view.inline_row_valid());
  ASSERT_FALSE(view.has_legacy_datums());
  const Datum *value = nullptr;
  const SqlType *type = nullptr;
  ASSERT_EQ(OB_SUCCESS, view.get_inline_value(0, value, type));
  EXPECT_TRUE(value->is_null());
  EXPECT_EQ(ObIntType, type->legacy_meta().get_type());
  DatumValue borrowed;
  ASSERT_EQ(OB_SUCCESS, view.get_value(0, borrowed));
  EXPECT_TRUE(borrowed.is_null());
  expect_poison_unchanged();
}

TEST_F(NativeBigintOutputPipeline, scalar_checker_and_diagnosis_consumers_are_rejected_before_storage)
{
  bool accepted = true;
  spec_.need_check_output_datum_ = true;
  ASSERT_EQ(OB_SUCCESS, result_.try_enable_native_bigint_output(3, accepted));
  EXPECT_FALSE(accepted);
  spec_.need_check_output_datum_ = false;
  session_.set_diagnosis_enabled(true);
  ASSERT_EQ(OB_SUCCESS, result_.try_enable_native_bigint_output(3, accepted));
  EXPECT_FALSE(accepted);
  session_.set_diagnosis_enabled(false);
  plan_.set_batch_size(0);
  ASSERT_EQ(OB_SUCCESS, result_.try_enable_native_bigint_output(3, accepted));
  EXPECT_FALSE(accepted);
  plan_.set_batch_size(16);
  EXPECT_EQ(0, tablet_.opens_);
  ResultRowView view;
  ASSERT_EQ(OB_SUCCESS, result_.get_next_result_row(exec_, view));
  EXPECT_TRUE(view.has_legacy_datums());
  EXPECT_FALSE(view.is_inline_datum_row());
  expect_values(view, INT64_MIN, 19);
}

TEST_F(NativeBigintOutputPipeline, error_after_project_does_not_publish_an_unchecked_candidate)
{
  arm();
  tablet_.rows_.after_project_error_ = OB_TIMEOUT;
  ResultRowView view;
  ASSERT_EQ(OB_TIMEOUT, result_.get_next_result_row(exec_, view));
  EXPECT_FALSE(view.is_datum_row());
  const Datum *values = nullptr;
  const SqlType *type = nullptr;
  int64_t count = 0;
  uint64_t token = 0;
  const uint64_t *generation = nullptr;
  EXPECT_FALSE(root_.try_get_native_output(0, values, type, count, token, generation));
  EXPECT_EQ(0, root_.get_monitor_info().output_row_count_);
}

TEST_F(NativeBigintOutputPipeline, scalar_adapter_disarms_permission_before_saving_expression_datums)
{
  arm();
  // This overload uses the generic vector-to-scalar adapter and its holder.
  // It must withdraw the ResultRowView capability before storage advances.
  ASSERT_EQ(OB_SUCCESS, result_.get_next_row());
  EXPECT_TRUE(root_.has_legacy_output());
  EXPECT_EQ(INT64_MIN, exprs_[2].locate_expr_datum(root_.get_eval_ctx()).get_int());
  EXPECT_EQ(19, exprs_[0].locate_expr_datum(root_.get_eval_ctx()).get_int());
  EXPECT_EQ(16, tablet_.rows_.last_capacity_);
}

TEST_F(NativeBigintOutputPipeline, miss_does_not_publish_a_native_row)
{
  arm();
  tablet_.rows_.miss_ = true;
  ResultRowView view;
  ASSERT_EQ(OB_ITER_END, result_.get_next_result_row(exec_, view));
  EXPECT_FALSE(view.is_datum_row());
  EXPECT_EQ(0, root_.get_monitor_info().output_row_count_);
}

TEST_F(NativeBigintOutputPipeline, das_release_invalidates_view_and_reopen_retains_consumer_permission)
{
  arm();
  ResultRowView previous;
  ASSERT_EQ(OB_SUCCESS, result_.get_next_result_row(exec_, previous));
  ASSERT_TRUE(previous.inline_row_valid());
  ASSERT_EQ(OB_SUCCESS, task_.release_op());
  EXPECT_FALSE(previous.inline_row_valid());
  // No output prepare or new capability request between release and open.
  ASSERT_EQ(OB_SUCCESS, task_.open_op());
  root_.adopt_task_result();
  // Restart only the executor's row cursor. The root and DAS consumer remain
  // real, and their next get_next_batch must publish the new generation.
  result_.set_static_engine_root(&root_);
  ResultRowView next;
  ASSERT_EQ(OB_SUCCESS, result_.get_next_result_row(exec_, next));
  ASSERT_TRUE(next.inline_row_valid());
  expect_values(next, INT64_MIN, 19);
  ASSERT_EQ(OB_SUCCESS, result_.close(exec_));
  opened_ = false;
  EXPECT_FALSE(next.inline_row_valid());
  EXPECT_TRUE(root_.has_legacy_output());
}
} // namespace
