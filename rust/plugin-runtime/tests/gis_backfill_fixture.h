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

// Actual table-scan spatial DDL expansion and actual GIS DSO. A controlled DAS
// iterator supplies source rows; no tablet IO, checksum reporting or DDL commit.
static void spatial_backfill(GisProvider &provider)
{
  oceanbase::omt::ObSrsCacheSnapShot snapshot;
  CHECK(snapshot.init() == OB_SUCCESS);
  gis_srs_lookup_test::Row srs_row;
  srs_row.srid = 4326;
  const ObSrsItem *srs_item = nullptr;
  CHECK(snapshot.parse_srs_item(&srs_row, srs_item) == OB_SUCCESS && srs_item);
  CHECK(snapshot.add_srs_item(srs_row.srid, srs_item) == OB_SUCCESS);
  class SrsProvider final : public ObISrsProvider {
  public:
    ObISrsSnapshot &snapshot;
    ObSrsBoundsItem bounds;
    int failure = OB_SUCCESS;
    explicit SrsProvider(ObISrsSnapshot &s) : snapshot(s) {
      bounds.minX_ = bounds.minY_ = -100; bounds.maxX_ = bounds.maxY_ = 100;
    }
    int get_tenant_srs_guard(ObSrsCacheGuard &guard) override {
      if (failure != OB_SUCCESS) return failure;
      guard.bind(snapshot); return OB_SUCCESS;
    }
    int get_srs_bounds(uint64_t, const ObSrsItem *, const ObSrsBoundsItem *&out) override {
      if (failure != OB_SUCCESS) return failure;
      out = &bounds; return OB_SUCCESS;
    }
  } srs(snapshot);
  ObArenaAllocator arena;
  ObExecContext execution(arena);
  LobService lob;
  execution.set_srs_provider(&srs); execution.set_lob_read_service(&lob);
  ObTableScanSpec spec(arena, PHY_TABLE_SCAN);
  spec.set_spatial_ddl(true);
  alignas(16) char frame_data[4][1024]{};
  char *frames[4];
  ObExpr geo, key, cell, mbr;
  ObExpr *exprs[] = {&geo, &key, &cell, &mbr};
  CHECK(spec.output_.init(4) == OB_SUCCESS);
  for (int i = 0; i < 4; ++i) {
    frames[i] = frame_data[i];
    ObExpr &expr = *exprs[i];
    expr.frame_idx_ = i; expr.datum_off_ = 0; expr.eval_info_off_ = 256;
    expr.res_buf_off_ = 512; expr.res_buf_len_ = 128;
    expr.type_ = i == 2 ? T_FUN_SYS_SPATIAL_CELLID : i == 3 ? T_FUN_SYS_SPATIAL_MBR : T_REF_COLUMN;
    expr.datum_meta_.type_ = i == 0 ? ObGeometryType : i == 3 ? ObVarcharType : ObUInt64Type;
    expr.datum_meta_.cs_type_ = CS_TYPE_BINARY;
    expr.obj_meta_.set_type(expr.datum_meta_.type_);
    expr.obj_meta_.set_collation_type(CS_TYPE_BINARY);
    expr.obj_datum_map_ = ObDatum::get_obj_datum_map_type(expr.datum_meta_.type_);
    new (frames[i]) ObDatum(); new (frames[i] + 256) ObEvalInfo();
    CHECK(spec.output_.push_back(&expr) == OB_SUCCESS);
  }
  class Scan final : public oceanbase::sql::ObTableScanOp {
  public:
    Scan(ObExecContext &ctx, const ObOpSpec &spec) : oceanbase::sql::ObTableScanOp(ctx, spec, nullptr) {
      need_init_before_get_row_ = false; limit_param_.limit_ = -1;
    }
    void bind(ObDASIter *source, char **frames) { output_ = source; eval_ctx_.frames_ = frames; }
    void reset_pending() {
      if (domain_index_.dom_rows_) domain_index_.dom_rows_->reuse();
      domain_index_.domain_row_index_ = 0;
      iter_end_ = false;
    }
    int64_t pending() const { return domain_index_.dom_rows_ ? domain_index_.dom_rows_->count() : 0; }
    int init_rows() { return init_spatial_index_rows(); }
    bool cache_initialized() const { return domain_index_.dom_rows_ != nullptr; }
  } scan(execution, spec);
  struct Row { uint64_t key; std::vector<uint8_t> bytes; bool null = false; };
  class Source final : public ObDASIter {
  public:
    ObEvalCtx &eval;
    ObExpr **exprs;
    std::vector<Row> rows;
    size_t position = 0;
    int terminal = OB_ITER_END;
    Source(ObEvalCtx &e, ObExpr **x) : eval(e), exprs(x) {}
    int inner_init(ObDASIterParam &) override { return OB_SUCCESS; }
    int inner_reuse() override { position = 0; return OB_SUCCESS; }
    int inner_release() override { std::vector<Row>().swap(rows); return OB_SUCCESS; }
    int inner_get_next_rows(int64_t &, int64_t) override { return OB_NOT_SUPPORTED; }
    int inner_get_next_row() override {
      if (position == rows.size()) return terminal;
      const Row &row = rows[position++];
      auto &datum = exprs[0]->locate_datum_for_write(eval);
      if (row.null) datum.set_null();
      else datum.set_string(reinterpret_cast<const char *>(row.bytes.data()), row.bytes.size());
      exprs[1]->locate_datum_for_write(eval).set_uint(row.key);
      for (int i = 0; i < 2; ++i) exprs[i]->get_eval_info(eval).evaluated_ = true;
      return OB_SUCCESS;
    }
  };
  Source *source = OB_NEWx(Source, &arena, scan.get_eval_ctx(), exprs);
  CHECK(source);
  ObDASIterParam param(ObDASIterType::DAS_ITER_SCAN);
  param.max_size_ = 1; param.eval_ctx_ = &scan.get_eval_ctx();
  param.exec_ctx_ = &execution; param.output_ = &spec.output_;
  CHECK(source->init(param) == OB_SUCCESS);
  scan.bind(source, frames);
  auto point = gis_spatial_test::geometry(1, {1, 2});
  auto line = gis_spatial_test::geometry(2, {-50, -20, 30, 40});
  auto empty = gis_spatial_test::geometry(7, {}); gis_spatial_test::append_u32(empty, 0);
  const auto reset = [&](std::vector<Row> rows) {
    scan.reset_pending(); source->rows = std::move(rows); source->position = 0;
    source->terminal = OB_ITER_END;
  };
  int test_case = 0;
  const auto next = [&](int expected) {
    ++test_case;
    const int actual = scan.inner_get_next_row();
    if (actual != expected) std::cerr << "backfill case=" << test_case << " status=" << actual << " expected=" << expected
                                    << " source_position=" << source->position << std::endl;
    CHECK(actual == expected);
    if (actual != OB_SUCCESS) CHECK(scan.pending() == 0);
  };
  // NULL/empty between valid rows must not emit stale generated columns or PKs.
  reset({{1, {}, true}, {2, empty}, {3, point}, {4, {}, true}, {5, empty}, {6, point}});
  next(OB_SUCCESS); CHECK(source->position == 3 && key.locate_expr_datum(scan.get_eval_ctx()).get_uint() == 3);
  const uint64_t point_cell = cell.locate_expr_datum(scan.get_eval_ctx()).get_uint();
  const auto point_box = mbr.locate_expr_datum(scan.get_eval_ctx()).get_string();
  CHECK(point_cell != 0 && point_box.length() == 16);
  next(OB_SUCCESS); CHECK(source->position == 6 && key.locate_expr_datum(scan.get_eval_ctx()).get_uint() == 6);
  next(OB_ITER_END); next(OB_ITER_END);
  reset({{1, {}, true}, {2, empty}}); next(OB_ITER_END);
  reset({{7, line}});
  int count = 0;
  for (int ret = scan.inner_get_next_row(); ret != OB_ITER_END; ret = scan.inner_get_next_row()) {
    CHECK(ret == OB_SUCCESS && source->position == 1);
    CHECK(key.locate_expr_datum(scan.get_eval_ctx()).get_uint() == 7);
    CHECK(cell.locate_expr_datum(scan.get_eval_ctx()).get_uint() != 0);
    CHECK(mbr.locate_expr_datum(scan.get_eval_ctx()).get_string().length() == 32);
    CHECK(++count <= SAPTIAL_INDEX_DEFAULT_ROW_COUNT);
  }
  CHECK(count > 1);
  reset({{8, gis_spatial_test::geometry(1, {200, 200})}});
  next(OB_SUCCESS); CHECK(cell.locate_expr_datum(scan.get_eval_ctx()).get_uint() == UINT64_MAX);
  reset({{9, gis_spatial_test::geometry(1, {1, 2}, 4326)}}); next(OB_SUCCESS);
  reset({{10, gis_spatial_test::geometry(1, {1, 2}, 99999)}}); next(OB_ERR_SRS_NOT_FOUND);
  srs.failure = OB_TIMEOUT;
  reset({{1, point}}); next(OB_TIMEOUT);
  reset({{1, gis_spatial_test::geometry(1, {1, 2}, 4326)}}); next(OB_TIMEOUT);
  srs.failure = OB_SUCCESS;
  execution.set_srs_provider(nullptr); reset({{1, point}}); next(OB_NOT_INIT);
  execution.set_srs_provider(&srs);
  for (int fault = 1; fault <= 9; ++fault) {
    provider.index_fault_ = fault;
    reset({{1, point}}); next(fault == 1 || fault == 3 ? OB_TIMEOUT : OB_ERR_UNEXPECTED);
  }
  provider.index_fault_ = 0;
  auto *saved = g_mp; g_mp = nullptr;
  reset({{1, point}}); next(OB_NOT_SUPPORTED); g_mp = saved;
  reset({{1, {}}}); next(OB_ERR_GIS_INVALID_DATA); // zero bytes is not SQL NULL
  reset({{1, {1, 2, 3}}}); next(OB_ERR_GIS_INVALID_DATA);
  reset({{1, point}, {2, {1, 2, 3}}}); next(OB_SUCCESS); next(OB_ERR_GIS_INVALID_DATA);
  const ObObjMeta raw_meta = geo.obj_meta_;
  geo.obj_meta_.set_has_lob_header();
  std::vector<uint8_t> inrow(sizeof(ObLobCommon) + point.size());
  auto *lob_header = new (inrow.data()) ObLobCommon();
  std::memcpy(lob_header->buffer_, point.data(), point.size());
  reset({{11, inrow}}); next(OB_SUCCESS);
  CHECK(cell.locate_expr_datum(scan.get_eval_ctx()).get_uint() == point_cell);
  geo.obj_meta_ = raw_meta;
  reset({}); source->terminal = OB_TIMEOUT; next(OB_TIMEOUT);
  // A failed projection initialization must remain retryable.
  Scan malformed(execution, spec); malformed.bind(source, frames);
  mbr.type_ = T_REF_COLUMN;
  CHECK(malformed.init_rows() == OB_ERR_UNEXPECTED && !malformed.cache_initialized());
  mbr.type_ = T_FUN_SYS_SPATIAL_MBR;
  CHECK(malformed.init_rows() == OB_SUCCESS && malformed.cache_initialized());
  class Allocator final : public ObIAllocator {
  public:
    ObArenaAllocator arena;
    int calls = 0, fail_at = 0;
    void *alloc(int64_t size) override {
      return ++calls == fail_at ? nullptr : arena.alloc(size);
    }
    void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
    void free(void *ptr) override { arena.free(ptr); }
  } failing;
  ObExecContext failing_context(failing);
  for (int fail = 1; fail <= 3; ++fail) {
    Scan allocation_failure(failing_context, spec);
    failing.calls = 0; failing.fail_at = fail;
    CHECK(allocation_failure.init_rows() == OB_ALLOCATE_MEMORY_FAILED);
    CHECK(!allocation_failure.cache_initialized());
    failing.fail_at = 0;
    CHECK(allocation_failure.init_rows() == OB_SUCCESS && allocation_failure.cache_initialized());
  }
  // Helper does not overwrite an existing output prefix or MBR bytes on error.
  ObS2Cellids cells;
  CHECK(cells.push_back(123) == OB_SUCCESS);
  char buffer[OB_DEFAULT_MBR_SIZE]; std::memset(buffer, 0x5a, sizeof(buffer));
  ObString box(7, buffer);
  provider.index_fault_ = 3;
  CHECK(ObGeoTypeUtil::get_cellid_mbr_from_geom(
      ObString(point.size(), reinterpret_cast<const char *>(point.data())), nullptr, &srs.bounds,
      cells, box) == OB_TIMEOUT);
  CHECK(cells.size() == 1 && cells[0] == 123 && box.length() == 7);
  for (char byte : buffer) CHECK(byte == 0x5a);
  provider.index_fault_ = 0;
  CHECK(source->release() == OB_SUCCESS);
  arena.free(source); // arena-managed DAS iterator: release owns resource cleanup
  std::cout << "PASS: actual table-scan spatial backfill, NULL/empty skipping, multi-cell/PK layout, SRS and failure isolation; controlled DAS source" << std::endl;
}
