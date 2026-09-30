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

// Actual DAS row expansion, real data-plane write-plan conversion and actual
// leased GIS DSO; schema/SRS inputs are fixtures, no storage write is issued.
static void spatial_das_write(GisProvider &provider)
{
  using namespace oceanbase::share::schema;
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
  class Allocator final : public ObIAllocator {
  public:
    ObArenaAllocator arena;
    int calls = 0, fail_at = 0;
    void *alloc(const int64_t size) override {
      return ++calls == fail_at ? nullptr : arena.alloc(size);
    }
    void *alloc(const int64_t size, const ObMemAttr &) override { return alloc(size); }
    void free(void *ptr) override { arena.free(ptr); }
  } allocator;
  ObArenaAllocator metadata;
  // Exceed the datum row's 16 inline cells so allocation failures also occur
  // after one or more index rows have been appended, not only before them.
  constexpr int columns = 17;
  ObTableSchema schema;
  schema.set_table_id(50001); schema.set_database_id(50000);
  schema.set_schema_version(1); schema.set_table_type(USER_INDEX);
  schema.set_index_type(INDEX_TYPE_NORMAL_LOCAL); schema.set_rowkey_column_num(columns);
  CHECK(schema.set_table_name("__idx_50002_gis_das_write_fixture") == OB_SUCCESS);
  ObDASInsCtDef ctdef(metadata);
  CHECK(ctdef.column_ids_.init(columns) == OB_SUCCESS);
  CHECK(ctdef.column_types_.init(columns) == OB_SUCCESS);
  CHECK(ctdef.column_accuracys_.init(columns) == OB_SUCCESS);
  IntFixedArray projector(metadata);
  CHECK(projector.init(columns) == OB_SUCCESS);
  for (int i = 0; i < columns; ++i) {
    ObColumnSchemaV2 column;
    column.set_table_id(50001); column.set_column_id(16 + i);
    column.set_rowkey_position(i + 1); column.set_data_type(i == 1 ? ObVarcharType : ObUInt64Type);
    column.set_collation_type(CS_TYPE_BINARY); column.set_data_length(i == 1 ? 32 : 8);
    const std::string name = i == 0 ? "cell" : i == 1 ? "mbr" : "pk" + std::to_string(i);
    CHECK(column.set_column_name(name.c_str()) == OB_SUCCESS);
    CHECK(schema.add_column(column) == OB_SUCCESS);
    CHECK(ctdef.column_ids_.push_back(16 + i) == OB_SUCCESS);
    CHECK(ctdef.column_types_.push_back(column.get_meta_type()) == OB_SUCCESS);
    CHECK(ctdef.column_accuracys_.push_back(column.get_accuracy()) == OB_SUCCESS);
    CHECK(projector.push_back(i) == OB_SUCCESS);
  }
  const int plan_status = ctdef.table_param_.build(&schema, 1, ctdef.column_ids_);
  if (plan_status != OB_SUCCESS) std::cerr << "DAS fixture write-plan build: " << plan_status << std::endl;
  CHECK(plan_status == OB_SUCCESS);
  CHECK(ctdef.table_param_.get_data_table().get_rowkey_column_num() == columns);
  void *buffer = metadata.alloc(sizeof(ObDASWriteBuffer::DmlRow) + columns * sizeof(ObDatum));
  CHECK(buffer);
  auto &row = *new (buffer) ObDASWriteBuffer::DmlRow;
  row.cnt_ = columns;
  uint64_t key = 42;
  for (int i = 0; i < columns; ++i) new (row.cells() + i) ObDatum();
  row.cells()[0].set_null(); row.cells()[1].set_null();
  for (int i = 2; i < columns; ++i) {
    row.cells()[i].ptr_ = reinterpret_cast<char *>(&key); row.cells()[i].set_uint(key);
  }
  ObDomainIndexRow output;
  oceanbase::blocksstable::ObDatumRow sentinel;
  CHECK(output.push_back(&sentinel) == OB_SUCCESS);
  int cases = 0;
  const auto run = [&](std::vector<uint8_t> bytes, int expected) {
    const ObString swkb(bytes.size(), reinterpret_cast<char *>(bytes.data()));
    const int ret = ObDASDomainUtils::generate_spatial_index_rows(
        allocator, srs, ctdef, swkb, projector, row, output);
    ++cases;
    if (ret != expected) std::cerr << "DAS spatial write case=" << cases << ": " << ret << " expected " << expected << std::endl;
    CHECK(ret == expected && output.at(0) == &sentinel);
    if (ret != OB_SUCCESS) CHECK(output.count() == 1);
  };
  const auto clear = [&]() { while (output.count() > 1) output.pop_back(); };
  auto point = gis_spatial_test::geometry(1, {0, 0});
  uint32_t routing_srid = 123;
  for (size_t size = 0; size < 10; ++size) {
    CHECK(ObGeoTypeUtil::get_srid_from_wkb(
        ObString(size, reinterpret_cast<char *>(point.data())), routing_srid) == OB_ERR_GIS_INVALID_DATA);
    CHECK(routing_srid == 123);
  }
  for (uint8_t version : {uint8_t(0), uint8_t(2), uint8_t(0x41)}) {
    auto malformed = point; malformed[4] = version;
    run(malformed, OB_ERR_GIS_INVALID_DATA);
  }
  auto malformed = point; malformed[5] = 2;
  run(malformed, OB_ERR_GIS_INVALID_DATA);
  std::string unaligned(1, 'x');
  unaligned.append(reinterpret_cast<const char *>(point.data()), point.size());
  CHECK(ObGeoTypeUtil::get_srid_from_wkb(
      ObString(point.size(), unaligned.data() + 1), routing_srid) == OB_SUCCESS && routing_srid == 0);
  const int before = provider.cover_calls_;
  run(point, OB_SUCCESS);
  CHECK(provider.cover_calls_ == before + 1 && output.count() == 2);
  CHECK(output.at(1)->storage_datums_[0].get_uint() == UINT64_C(0x1000000000000001));
  CHECK(output.at(1)->storage_datums_[2].get_uint() == 42);
  ObString mbr = output.at(1)->storage_datums_[1].get_string();
  CHECK(mbr.length() == 16);
  ObSpatialMBR decoded;
  CHECK(ObSpatialMBR::from_string(mbr, ObDomainOpType::T_INVALID, decoded, true) == OB_SUCCESS);
  CHECK(decoded.x_min_ == 0 && decoded.y_min_ == 0);
  clear();
  auto line = gis_spatial_test::geometry(2, {-50, -20, 30, 40});
  allocator.calls = 0;
  run(line, OB_SUCCESS);
  CHECK(output.count() > 2);
  for (int i = 1; i < output.count(); ++i) {
    CHECK(output.at(i)->storage_datums_[2].get_uint() == 42);
    CHECK(output.at(i)->storage_datums_[1].get_string().length() == 32);
  }
  const int allocation_count = allocator.calls;
  CHECK(allocation_count == output.count() + 1); // MBR + row array + each row's datums.
  clear();
  for (int fail = 1; fail <= allocation_count; ++fail) {
    allocator.calls = 0; allocator.fail_at = fail;
    run(line, OB_ALLOCATE_MEMORY_FAILED);
  }
  allocator.fail_at = 0;
  run(line, OB_SUCCESS); clear();
  auto empty = gis_spatial_test::geometry(7, {}); gis_spatial_test::append_u32(empty, 0);
  run(empty, OB_SUCCESS); CHECK(output.count() == 1);
  run(gis_spatial_test::geometry(1, {200, 200}), OB_SUCCESS);
  CHECK(output.count() == 2 && output.at(1)->storage_datums_[0].get_uint() == UINT64_MAX); clear();
  auto geographic = point;
  geographic[0] = 0xe6; geographic[1] = 0x10;
  run(geographic, OB_SUCCESS); CHECK(output.count() == 2); clear();
  run(gis_spatial_test::geometry(1, {0, 0}, 99999), OB_ERR_SRS_NOT_FOUND);
  srs.failure = OB_TIMEOUT;
  run(point, OB_TIMEOUT); run(geographic, OB_TIMEOUT);
  srs.failure = OB_SUCCESS;
  for (int fault = 1; fault <= 9; ++fault) {
    provider.index_fault_ = fault;
    run(point, fault == 1 || fault == 3 ? OB_TIMEOUT : OB_ERR_UNEXPECTED);
  }
  provider.index_fault_ = 0;
  auto *saved = g_mp; g_mp = nullptr;
  run(point, OB_NOT_SUPPORTED); g_mp = saved;
  projector.at(2) = columns; run(point, OB_INVALID_ARGUMENT);
  projector.at(2) = -1; run(point, OB_INVALID_ARGUMENT);
  projector.at(2) = 2;
  run(point, OB_SUCCESS); clear();
  std::cout << "PASS: DAS spatial row expansion, SRS routing, cell/MBR/PK layout and atomic failure output; no storage writes" << std::endl;
}
