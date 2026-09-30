// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Actual PX heaps with generated SQL ordering metadata and a real Rust DSO.
// Channel input rows are supplied here; this does not run the PX scheduler/DTL.
#ifndef SEEKDB_TEST_RUST_PX_MERGE_FIXTURE_H_
#define SEEKDB_TEST_RUST_PX_MERGE_FIXTURE_H_
#include "sql/engine/px/exchange/ob_row_heap.h"
#include "sql/engine/sort/ob_sort_op.h"
#include "sql/engine/expr/plugin_function_expr.h"
#include "rust_range_fixture.h"
namespace rust_px_merge_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
using Row = ObChunkDatumStore::StoredRow;
using Last = ObChunkDatumStore::LastStoredRow;

template <typename Compare, typename Stored, typename Provider>
void merge(const ObSortSpec &spec, ObEvalCtx &eval, Provider &provider,
    const std::vector<const Stored *> &rows, bool native, int channels)
{
  const int ranks[] = {1, 3, 2, 4, 0, 1, -1};
  const int bytes[] = {3, 1, 4, 2, 0, 3, -1}; // NULL, a, aa, bbb, z, emoji.
  const bool ascending = spec.sort_collations_.at(0).is_ascending_;
  std::vector<int> expected;
  for (int i = 0; i < int(rows.size()); ++i) expected.push_back(i);
  std::sort(expected.begin(), expected.end(), [&](int l, int r) {
    const int a = native ? bytes[l] : ranks[l], b = native ? bytes[r] : ranks[r];
    return a == b ? (ascending ? l < r : l > r) : (ascending ? a < b : a > b);
  });
  std::vector<std::vector<int>> inputs(channels);
  for (int i : expected) inputs[i % channels].push_back(i);
  std::vector<size_t> positions(channels, 0);
  ObRowHeap<Compare, Stored> heap;
  const ObDatumAccessContext *access = nullptr;
  CHECK(eval.get_datum_access_ctx(access) == OB_SUCCESS);
  CHECK(heap.init(channels, &spec.sort_collations_, &spec.sort_cmp_funs_, access,
      native ? nullptr : &spec.all_exprs_, &eval) == OB_SUCCESS);
  const int before = provider.comparisons_, resolves = provider.resolves_;
  for (int i = 0; i < channels; ++i) {
    if (inputs[i].empty()) heap.shrink();
    else CHECK(heap.push(rows[inputs[i][positions[i]++]]) == OB_SUCCESS);
  }
  for (int i : expected) {
    const Stored *out = nullptr;
    const int ret = heap.pop(out);
    if (ret != OB_SUCCESS || out != rows[i]) {
      const auto found = std::find(rows.begin(), rows.end(), out);
      std::cerr << "PX merge ret=" << ret << " native=" << native << " ascending=" << ascending
                << " channels=" << channels << " expected=" << i << " actual="
                << (found == rows.end() ? -1 : int(found - rows.begin())) << std::endl;
    }
    CHECK(ret == OB_SUCCESS && out == rows[i]);
    const int channel = heap.writable_channel_idx();
    if (positions[channel] == inputs[channel].size()) heap.shrink();
    else CHECK(heap.push(rows[inputs[channel][positions[channel]++]]) == OB_SUCCESS);
  }
  CHECK(heap.count() == 0 && heap.capacity() == 0 && provider.resolves_ == resolves);
  CHECK(native ? provider.comparisons_ == before : provider.comparisons_ > before);
}

template <typename Compare, typename Stored, typename Provider>
void failures(const ObSortSpec &spec, ObEvalCtx &eval, Provider &provider,
    const std::vector<const Stored *> &rows, Row &corrupt)
{
  const ObDatumAccessContext *access = nullptr;
  CHECK(eval.get_datum_access_ctx(access) == OB_SUCCESS);
  ObRowHeap<Compare, Stored> heap;
  const auto init = [&] {
    heap.reset_heap();
    CHECK(heap.init(3, &spec.sort_collations_, &spec.sort_cmp_funs_, access,
        &spec.all_exprs_, &eval) == OB_SUCCESS);
  };
  ObSEArray<ObSortCmpFunc, 1> incomplete;
  CHECK(incomplete.push_back(spec.sort_cmp_funs_.at(0)) == OB_SUCCESS);
  CHECK(heap.init(3, &spec.sort_collations_, &incomplete, access,
      &spec.all_exprs_, &eval) == OB_INVALID_ARGUMENT);
  init();
  auto *info = dynamic_cast<PluginTypeValueExtraInfo *>(
      spec.all_exprs_.at(spec.sort_collations_.at(0).field_idx_)->extra_info_);
  CHECK(info && info->valid() && info->ordering_);
  ++info->ordering_->binding_.catalog_epoch;
  const int unchanged = provider.comparisons_;
  CHECK(heap.push(rows[0]) == OB_SUCCESS && heap.push(rows[1]) == OB_INVALID_DATA);
  CHECK(provider.comparisons_ == unchanged);
  --info->ordering_->binding_.catalog_epoch;
  init();
  CHECK(heap.push(rows[0]) == OB_SUCCESS && heap.push(rows[1]) == OB_SUCCESS);
  auto &key = corrupt.cells()[spec.sort_collations_.at(0).field_idx_];
  const auto saved = key;
  const char invalid = char(0xff); key.set_string(ObString(1, &invalid));
  const int before = provider.comparisons_;
  CHECK(heap.push(rows[2]) == OB_INVALID_ARGUMENT); // Actual Rust UTF-8 comparator rejects it.
  CHECK(provider.comparisons_ == before + 1);
  const Stored *out = nullptr;
  CHECK(heap.pop(out) == OB_INVALID_ARGUMENT && out == nullptr);
  CHECK(provider.comparisons_ == before + 1); // Sticky error, no further callbacks.
  key = saved;

  init();
  for (int i = 0; i < 3; ++i) CHECK(heap.push(rows[i]) == OB_SUCCESS);
  class Cancel final : public ObIExtraStatusCheck {
  public:
    explicit Cancel(const int &calls) : calls_(calls), before_(calls) {}
    const char *name() const override { return "plugin-px-merge-cancel"; }
    int check() const override { return calls_ == before_ ? OB_SUCCESS : OB_TIMEOUT; }
  private:
    const int &calls_; int before_;
  } cancel(provider.comparisons_);
  const int calls = provider.comparisons_;
  { ObIExtraStatusCheck::Guard guard(eval.exec_ctx_, cancel);
    CHECK(heap.pop(out) == OB_TIMEOUT && out == nullptr); }
  CHECK(provider.comparisons_ == calls + 1);
  // Reset/reinit must discard the previous comparator error.
  init();
  for (int i = 0; i < 3; ++i) CHECK(heap.push(rows[i]) == OB_SUCCESS);
  CHECK(heap.pop(out) == OB_SUCCESS && out);
}

template <typename Provider>
void run(const ObSortSpec &spec, ObEvalCtx &eval, Provider &provider,
    oceanbase::share::plugin::ObPluginLoader &loader, ObArenaAllocator &arena,
    bool native_spec = false, ObExpr *partition_ddl = nullptr, ObExpr *tablet_output = nullptr,
    const ExprFixedArray *partition_calc = nullptr)
{
  CHECK(spec.sort_collations_.count() == 2);
  const auto key = spec.sort_collations_.at(0).field_idx_;
  const auto ordinal = spec.sort_collations_.at(1).field_idx_;
  ObChunkDatumStore store("RustPxMerge");
  CHECK(store.init(INT64_MAX, ObCtxIds::WORK_AREA, "RustPxMerge", false) == OB_SUCCESS);
  const char *words[] = {"z", "aa", "🙂", "bbb", "a", "z", nullptr};
  std::vector<const Row *> rows;
  std::vector<const Last *> lasts;
  std::vector<std::unique_ptr<Last>> owners;
  for (int i = 0; i < 7; ++i) {
    std::vector<ObDatum> values(spec.all_exprs_.count());
    for (auto &value : values) value.set_null();
    if (words[i]) values[key].set_string(ObString::make_string(words[i]));
    int64_t id = i + 1;
    values[ordinal].ptr_ = reinterpret_cast<const char *>(&id);
    values[ordinal].set_int(i + 1);
    Row *row = nullptr;
    CHECK(store.add_row(values.data(), values.size(), 0, &row) == OB_SUCCESS && row);
    rows.push_back(row);
    auto last = std::make_unique<Last>(arena); last->store_row_ = row;
    lasts.push_back(last.get()); owners.push_back(std::move(last));
  }
  for (bool native : {false, true}) for (int channels : {3, 9}) {
    if (native_spec && !native) continue;
    merge<ObDatumRowCompare>(spec, eval, provider, rows, native, channels);
    merge<ObMaxDatumRowCompare>(spec, eval, provider, lasts, native, channels);
  }
  if (!native_spec) {
    failures<ObDatumRowCompare>(spec, eval, provider, rows, *const_cast<Row *>(rows[2]));
    failures<ObMaxDatumRowCompare>(spec, eval, provider, lasts, *const_cast<Row *>(rows[2]));
  }
  rust_range_test::run(spec, eval, provider, arena, rows, false, native_spec);
  rust_range_test::run(spec, eval, provider, arena, rows, true, native_spec, partition_ddl, tablet_output, partition_calc);
  oceanbase::share::plugin::ObPluginStatusSnapshot status;
  CHECK(loader.get_status("org.seekdb.rust-text", status) == OB_SUCCESS && status.lease_count_ == 0);
}
} // namespace rust_px_merge_test
#endif
