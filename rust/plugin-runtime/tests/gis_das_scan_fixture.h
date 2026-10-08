/*
 * Copyright (c) 2026 OceanBase.
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
#pragma once

// Actual DAS spatial iterator and actual leased MBR service. Only the storage
// row source is controlled; this does not issue a tablet scan or storage read.
static void spatial_das_scan(GisProvider &provider)
{
  constexpr int capacity = 4;
  ObArenaAllocator arena;
  ObExecContext execution(arena);
  ObEvalCtx eval(execution);
  eval.max_batch_size_ = capacity;
  alignas(16) char frame_data[3][1024] = {};
  char *frames[3];
  ObExpr key, mbr, transaction;
  ObExpr *expressions[] = {&key, &mbr, &transaction};
  for (int i = 0; i < 3; ++i) {
    frames[i] = frame_data[i];
    auto &expr = *expressions[i];
    expr.frame_idx_ = i; expr.datum_off_ = 0; expr.eval_info_off_ = 256;
    expr.res_buf_off_ = 512; expr.res_buf_len_ = 128;
    expr.batch_result_ = true; expr.batch_idx_mask_ = UINT64_MAX;
    expr.type_ = T_REF_COLUMN;
    expr.datum_meta_.type_ = i == 0 ? ObUInt64Type : ObVarcharType;
    expr.obj_meta_.set_type(expr.datum_meta_.type_);
    expr.obj_meta_.set_collation_type(CS_TYPE_BINARY);
    expr.obj_datum_map_ = ObDatum::get_obj_datum_map_type(expr.datum_meta_.type_);
    new (frames[i] + 256) ObEvalInfo();
    for (int j = 0; j < capacity; ++j) new (frames[i] + j * sizeof(ObDatum)) ObDatum();
  }
  eval.frames_ = frames;
  struct Row {
    uint64_t id;
    std::string box, tx;
    bool null_mbr = false;
  };
  class Source final : public ObNewRowIterator {
  public:
    ObEvalCtx &eval;
    ObExpr **exprs;
    std::vector<Row> rows;
    size_t pos = 0;
    uint64_t keys[capacity]{};
    int terminal = OB_ITER_END;
    bool empty_success = false;
    Source(ObEvalCtx &e, ObExpr **x) : eval(e), exprs(x) {}
    void reset() override { pos = 0; terminal = OB_ITER_END; empty_success = false; }
    void fill(int index) {
      const Row &row = rows[pos++]; keys[index] = row.id;
      auto &key = exprs[0]->locate_batch_datums(eval)[index];
      key.ptr_ = reinterpret_cast<char *>(&keys[index]); key.set_uint(row.id);
      auto &box = exprs[1]->locate_batch_datums(eval)[index];
      if (row.null_mbr) box.set_null();
      else box.set_string(ObString(row.box.size(), row.box.data()));
      exprs[2]->locate_batch_datums(eval)[index].set_string(ObString(row.tx.size(), row.tx.data()));
    }
    int get_next_row(ObNewRow *&) override { return OB_NOT_SUPPORTED; }
    int get_next_row() override {
      if (pos == rows.size()) return terminal;
      fill(0); return OB_SUCCESS;
    }
    int get_next_rows(int64_t &count, int64_t requested) override {
      count = 0;
      if (empty_success) return OB_SUCCESS;
      while (count < requested && pos < rows.size()) fill(count++);
      return pos == rows.size() ? terminal : OB_SUCCESS;
    }
  } source(eval, expressions);
  class ScanService final : public ObITabletScan {
  public:
    int releases = 0;
    int revert_scan_iter(ObNewRowIterator *) override { ++releases; return OB_SUCCESS; }
  } service;
  struct Binding {
    ObITabletScan *previous = oceanbase::share::server_service<ObITabletScan>();
    explicit Binding(ObITabletScan *current) { oceanbase::share::bind_server_service<ObITabletScan>(current); }
    ~Binding() { oceanbase::share::bind_server_service<ObITabletScan>(previous); }
  } binding(&service);
  ObDASScanCtDef ctdef(arena);
  ctdef.ref_table_id_ = 500001;
  CHECK(ctdef.result_output_.init(3) == OB_SUCCESS);
  for (auto *expr : expressions) CHECK(ctdef.result_output_.push_back(expr) == OB_SUCCESS);
  ctdef.trans_info_expr_ = &transaction;
  ObDASScanRtDef rtdef; rtdef.eval_ctx_ = &eval;
  ObDASSpatialScanIterParam param;
  param.scan_ctdef_ = &ctdef; param.scan_rtdef_ = &rtdef; param.eval_ctx_ = &eval;
  param.exec_ctx_ = &execution; param.output_ = &ctdef.result_output_; param.max_size_ = capacity;
  // Match the DAS factory's arena allocation + explicit release lifecycle.
  // DAS base destruction calls a pure virtual release hook; factory-managed
  // iterators are released before their arena storage is reclaimed instead.
  ObDASSpatialScanIter *scan = OB_NEWx(ObDASSpatialScanIter, &arena, arena);
  CHECK(scan != nullptr);
  CHECK(scan->init(param) == OB_SUCCESS);
  scan->get_output_result_iter() = &source;
  oceanbase::storage::ObTableScanParam scan_param;
  const ObSpatialMBR query(0, 2, 0, 2, ObDomainOpType::T_GEO_INTERSECTS);
  CHECK(scan_param.mbr_filters_.push_back(query) == OB_SUCCESS);
  scan->set_scan_param(scan_param);
  const auto row = [](uint64_t id, double x) {
    Row result{id, {}, "tx" + std::to_string(id)};
    char bytes[OB_DEFAULT_MBR_SIZE]; int64_t size = 0;
    ObSpatialMBR box(x, x, 1, 1, ObDomainOpType::T_INVALID); box.is_point_ = true;
    CHECK(box.to_char(bytes, size) == OB_SUCCESS);
    result.box.assign(bytes, size); return result;
  };
  const auto reset = [&](std::vector<Row> rows) {
    source.rows = std::move(rows); source.pos = 0; source.terminal = OB_ITER_END;
    source.empty_success = false;
  };
  const auto key_at = [&](int i) { return key.locate_batch_datums(eval)[i].get_uint(); };
  const auto tx_at = [&](int i) {
    ObString text = transaction.locate_batch_datums(eval)[i].get_string();
    return std::string(text.ptr(), text.length());
  };
  reset({row(1, 10), row(2, 1), row(3, 20)});
  const int first_status = scan->get_next_row();
  if (first_status != OB_SUCCESS || key_at(0) != 2) {
    std::cerr << "DAS first row: status=" << first_status << " key=" << key_at(0)
              << " source_pos=" << source.pos << " batch_idx=" << eval.get_batch_idx()
              << " provider_fault=" << provider.spatial_fault_ << std::endl;
  }
  CHECK(first_status == OB_SUCCESS && key_at(0) == 2);
  CHECK(scan->get_next_row() == OB_ITER_END && source.pos == 3);
  // Both storage layouts locate MBR correctly, with or without transaction info.
  ctdef.trans_info_expr_ = nullptr;
  ctdef.result_output_.pop_back();
  reset({row(1, 10), row(2, 1)});
  CHECK(scan->get_next_row() == OB_SUCCESS && key_at(0) == 2);
  CHECK(ctdef.result_output_.push_back(&transaction) == OB_SUCCESS);
  ctdef.trans_info_expr_ = &transaction;
  // Full-range/empty-filter bypass must not survive rebinding a narrow scan.
  ObNewRange whole; whole.set_whole_range();
  CHECK(scan_param.key_ranges_.push_back(whole) == OB_SUCCESS);
  scan->set_scan_param(scan_param);
  reset({row(4, 10)});
  const int before = provider.spatial_calls_;
  CHECK(scan->get_next_row() == OB_SUCCESS && key_at(0) == 4 && provider.spatial_calls_ == before);
  scan_param.key_ranges_.reuse(); scan->set_scan_param(scan_param);
  reset({row(5, 10)}); CHECK(scan->get_next_row() == OB_ITER_END);
  scan_param.mbr_filters_.reuse(); scan->set_scan_param(scan_param);
  reset({row(6, 10)}); CHECK(scan->get_next_row() == OB_SUCCESS);
  CHECK(scan_param.mbr_filters_.push_back(query) == OB_SUCCESS); scan->set_scan_param(scan_param);
  reset({row(7, 10)}); CHECK(scan->get_next_row() == OB_ITER_END);
  // Filters from separate ranges are ORed, retaining the first match.
  CHECK(scan_param.mbr_filters_.push_back(ObSpatialMBR(20, 22, 0, 2, ObDomainOpType::T_GEO_INTERSECTS)) == OB_SUCCESS);
  reset({row(8, 30), row(9, 21), row(10, 1)});
  CHECK(scan->get_next_row() == OB_SUCCESS && key_at(0) == 9);
  CHECK(scan->get_next_row() == OB_SUCCESS && key_at(0) == 10);
  CHECK(scan->get_next_row() == OB_ITER_END);
  scan_param.mbr_filters_.pop_back();
  // Consume a fully rejected batch, compact the following final batch, and
  // preserve the key/MBR/transaction association and caller batch context.
  reset({row(1, 10), row(2, 10), row(3, 10), row(4, 10), row(5, 1), row(6, 10), row(7, 1)});
  int64_t count = 99;
  {
    ObEvalCtx::BatchInfoScopeGuard context(eval);
    context.set_batch_idx(2); context.set_batch_size(3);
    CHECK(scan->get_next_rows(count, capacity) == OB_ITER_END && count == 2);
    CHECK(key_at(0) == 5 && key_at(1) == 7 && tx_at(0) == "tx5" && tx_at(1) == "tx7");
    CHECK(eval.get_batch_idx() == 2 && eval.get_batch_size() == 3);
  }
  CHECK(scan->get_next_rows(count, capacity) == OB_ITER_END && count == 0);
  reset({row(1, 1), row(2, 1), row(3, 1), row(4, 1), row(5, 1)});
  CHECK(scan->get_next_rows(count, capacity + 10) == OB_SUCCESS && count == capacity);
  CHECK(scan->get_next_rows(count, capacity + 10) == OB_ITER_END && count == 1 && key_at(0) == 5);
  reset({row(1, 1), row(2, 10), row(3, 1)});
  CHECK(scan->get_next_rows(count, 1) == OB_SUCCESS && count == 1 && key_at(0) == 1);
  CHECK(scan->get_next_rows(count, 1) == OB_ITER_END && count == 1 && key_at(0) == 3);
  Row bad = row(2, 1); bad.box = "bad";
  reset({row(1, 1), bad});
  CHECK(scan->get_next_rows(count, capacity) == OB_INVALID_ARGUMENT && count == 0);
  bad.null_mbr = true; reset({bad}); CHECK(scan->get_next_row() == OB_INVALID_ARGUMENT);
  mbr.obj_meta_.set_type(ObCharType);
  reset({row(1, 1)}); CHECK(scan->get_next_row() == OB_INVALID_ARGUMENT);
  mbr.obj_meta_.set_type(ObVarcharType);
  for (int fault = 1; fault <= 8; ++fault) {
    provider.spatial_fault_ = fault;
    reset({row(1, 1)});
    const int status = scan->get_next_rows(count, capacity);
    CHECK(status != OB_SUCCESS && status != OB_ITER_END && count == 0);
  }
  provider.spatial_fault_ = 0;
  auto *saved = g_mp; g_mp = nullptr;
  reset({row(1, 1)}); CHECK(scan->get_next_row() == OB_NOT_SUPPORTED); g_mp = saved;
  reset({row(1, 1)}); source.terminal = OB_TIMEOUT;
  CHECK(scan->get_next_rows(count, capacity) == OB_TIMEOUT && count == 0);
  reset({}); source.empty_success = true;
  CHECK(scan->get_next_rows(count, capacity) == OB_ERR_UNEXPECTED && count == 0);
  CHECK(scan->get_next_rows(count, 0) == OB_INVALID_ARGUMENT && count == 0);
  CHECK(scan->release() == OB_SUCCESS && service.releases == 1);
  arena.free(scan);
  std::cout << "PASS: actual DAS scalar/batch MBR scan, OR ranges, rebind reset, compaction/last batch and failure isolation; controlled storage source" << std::endl;
}
