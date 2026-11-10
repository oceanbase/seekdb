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

#include "sql/engine/window_function/ob_window_function_op.h"
#include "sql/session/ob_sql_session_info.h"
#include "share/system_variable/ob_system_variable_init.h"
#include "sql/engine/px/datahub/components/ob_dh_range_dist_wf.h"
#include "lib/charset/ob_charset.h"
#include "sql/engine/basic/ob_pushdown_filter.h"
#include <gtest/gtest.h>
#include <array>
#include <cstring>
#include <string>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace
{
using Row = ObChunkDatumStore::StoredRow;
using RARow = ObRADatumStore::StoredRow;
using Op = ObWindowFunctionOp;
template <typename Tag, typename Tag::Type Member>
struct PrivateMember { friend typename Tag::Type member(Tag) { return Member; } };
struct SameTag {
  using Type = int (Op::*)(Op::WinFuncCell &, bool &);
  friend Type member(SameTag);
};
template struct PrivateMember<SameTag, &Op::check_same_partition>;
struct HashTag {
  using Type = int (Op::*)(const ObIArray<ObExpr *> *, const Row *, uint64_t &);
  friend Type member(HashTag);
};
template struct PrivateMember<HashTag, &Op::calc_part_exprs_hash>;
struct CopyTag {
  using Type = int (Op::*)(const RARow &, ObWinbufPieceMsg &, int64_t, char *);
  friend Type member(CopyTag);
};
template struct PrivateMember<CopyTag, &Op::copy_datum_row>;
struct InputStoresTag {
  using Type = Op::Stores Op::*;
  friend Type member(InputStoresTag);
};
template struct PrivateMember<InputStoresTag, &Op::input_rows_>;
struct FunctionsTag {
  using Type = Op::WinFuncCellList Op::*;
  friend Type member(FunctionsTag);
};
template struct PrivateMember<FunctionsTag, &Op::wf_list_>;
struct ResultsTag {
  using Type = DatumFixedArray Op::*;
  friend Type member(ResultsTag);
};
template struct PrivateMember<ResultsTag, &Op::curr_row_collect_values_>;
struct PositionTag {
  using Type = int (Op::*)(Op::RowsReader &, Op::WinFuncCell &, int64_t,
      const RARow &, bool, int64_t &, bool &);
  friend Type member(PositionTag);
};
template struct PrivateMember<PositionTag, &Op::get_pos>;
struct CollectTag {
  using Type = int (Op::*)(int64_t, ObDatum &, Op::WinFuncCell &);
  friend Type member(CollectTag);
};
template struct PrivateMember<CollectTag, &Op::collect_result>;
struct OutputTag {
  using Type = int (Op::*)(int64_t, Op::StoreMemberPtr, const RARow **);
  friend Type member(OutputTag);
};
template struct PrivateMember<OutputTag, &Op::output_row>;
struct OutputBatchTag {
  using Type = int (Op::*)(int64_t, Op::RowsStore &, Op::RowsStore &,
      ObEvalCtx::BatchInfoScopeGuard &);
  friend Type member(OutputBatchTag);
};
template struct PrivateMember<OutputBatchTag, &Op::output_rows_store_rows>;

// The operator normally owns these stores and cells. Tests borrow stack
// objects only while the actual consumer runs, and unlink them on every exit.
struct BorrowedStore
{
  BorrowedStore(Op::Stores &stores, Op::RowsStore &row_store)
      : stores_(stores), prior_(stores.cur_) { stores_.cur_ = &row_store; }
  ~BorrowedStore() { stores_.cur_ = prior_; }
  Op::Stores &stores_;
  Op::RowsStore *prior_;
};
struct BorrowedFunctions
{
  BorrowedFunctions(Op::WinFuncCellList &list, Op::WinFuncCell &first,
      Op::WinFuncCell &second) : list_(list), first_(first), second_(second)
  { EXPECT_TRUE(list_.add_last(&first_)); EXPECT_TRUE(list_.add_last(&second_)); }
  ~BorrowedFunctions() { list_.remove(&second_); list_.remove(&first_); }
  Op::WinFuncCellList &list_;
  Op::WinFuncCell &first_;
  Op::WinFuncCell &second_;
};
int failing_ra_compare(const ObDatum &, const ObDatum &, int &result,
    const ObDatumAccessContext *)
{ result = 0; return OB_TIMEOUT; }

ObDatum integer(const int64_t &n)
{ return ObDatum(reinterpret_cast<const char *>(&n), sizeof(n), false); }
ObDatum bytes(const void *p, uint32_t size)
{ return ObDatum(static_cast<const char *>(p), size, false); }
ObDatum text(const std::string &s) { return bytes(s.data(), s.size()); }
ObDatum null_value() { ObDatum d; d.set_null(); return d; }
std::vector<ObDatum> partial_values(const std::string &partition,
    const std::array<char, 16> &coefficient, const int64_t &key, const int64_t &result)
{
  return {text(partition), bytes(coefficient.data(), coefficient.size()), integer(key), integer(result)};
}
int64_t number(const StoredDatumCell &cell)
{
  int64_t n = 0;
  EXPECT_FALSE(cell.is_null()); EXPECT_EQ(sizeof(n), cell.len_);
  if (!cell.is_null() && cell.len_ == sizeof(n)) { MEMCPY(&n, cell.ptr_, sizeof(n)); }
  return n;
}
struct OwnedRow
{
  explicit OwnedRow(const std::vector<ObDatum> &values, uint32_t extra = 0)
  {
    const int64_t size = ObChunkDatumStore::Block::row_store_size(values.data(), values.size(), extra);
    memory_.resize(size);
    row_ = new (memory_.data()) Row;
    EXPECT_EQ(OB_SUCCESS, row_->copy_shadow_datums(values.data(), values.size(), row_->payload_,
        size - sizeof(Row), size, extra));
  }
  void poison() { MEMSET(memory_.data(), 0xA5, memory_.size()); }
  std::vector<char> memory_;
  Row *row_ = nullptr;
};
struct OwnedRARow
{
  explicit OwnedRARow(const std::vector<ObDatum> &values, uint32_t extra = 0)
  {
    for (const auto &value : values) { EXPECT_EQ(OB_SUCCESS, values_.push_back(value)); }
    int64_t quota = 0;
    EXPECT_EQ(OB_SUCCESS, ObRADatumStore::Block::row_store_size(values_, quota, extra));
    memory_.resize(quota - sizeof(uint32_t));
    row_ = new (memory_.data()) RARow;
    EXPECT_EQ(OB_SUCCESS, row_->copy_datums(values_, row_->payload_,
        memory_.size() - sizeof(RARow), quota, extra));
  }
  ObSEArray<ObDatum, 8> values_;
  std::vector<char> memory_;
  RARow *row_ = nullptr;
};
// The protected page makes a four-byte read beyond a valid RA body fail,
// independently of the destination canary that detects an oversized write.
class GuardedBody
{
public:
  explicit GuardedBody(int64_t bytes) : page_(sysconf(_SC_PAGESIZE)), mapped_(MAP_FAILED), body_(nullptr)
  {
    if (bytes > 0 && bytes <= page_) {
      mapped_ = mmap(nullptr, 2 * page_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      if (mapped_ != MAP_FAILED) {
        if (mprotect(static_cast<char *>(mapped_) + page_, page_, PROT_NONE) == 0) {
          body_ = static_cast<char *>(mapped_) + page_ - bytes;
        }
      }
    }
  }
  ~GuardedBody() { if (mapped_ != MAP_FAILED) { munmap(mapped_, 2 * page_); } }
  char *get() const { return body_; }
private:
  int64_t page_;
  void *mapped_;
  char *body_;
};

class FixedWindowCells : public ::testing::Test
{
protected:
  FixedWindowCells() : arena_(), exec_(arena_), spec_(arena_, PHY_WINDOW_FUNCTION),
      op_(exec_, spec_, nullptr), frame_{}, frames_{frame_.data()},
      eval_(op_.get_eval_ctx()), guard_(eval_), exprs_{} {}
  void SetUp() override
  {
    exec_.set_frames(frames_); eval_.frames_ = frames_;
    guard_.set_batch_size(1); guard_.set_batch_idx(0);
    for (int64_t i = 0; i < 4; ++i) {
      ObExpr &e = exprs_[i];
      e.type_ = T_REF_COLUMN; e.frame_idx_ = 0; e.datum_off_ = i * 512;
      e.eval_info_off_ = i * 512 + 64; e.res_buf_off_ = i * 512 + 192; e.res_buf_len_ = 128;
      set_type(i, ObIntType);
      new (frame_.data() + e.datum_off_) Datum;
      new (frame_.data() + e.eval_info_off_) ObEvalInfo;
      e.get_eval_info(eval_).evaluated_ = true;
    }
  }
  void set_type(int64_t i, SqlValueType type, ObCollationType cs = CS_TYPE_BINARY)
  {
    auto &e = exprs_[i];
    e.obj_meta_.set_type(type); e.obj_meta_.set_collation_type(cs);
    if (type == ObDecimalIntType) { e.obj_meta_.set_scale(2); }
    e.datum_meta_ = ObDatumMeta(type, cs, type == ObDecimalIntType ? 2 : 0);
    e.obj_datum_map_ = ObDatum::get_sql_value_datum_map_type(type);
    ASSERT_EQ(OB_SUCCESS, e.prepare_scalar_type(e.obj_meta_, e.prepare_legacy_copy_plan().get_accuracy(), arena_));
  }
  Datum &datum(int64_t i) { return exprs_[i].locate_expr_datum(eval_); }
  void put(int64_t i, const common::ObDatum &value)
  { ASSERT_EQ(OB_SUCCESS, exprs_[i].get_datum_copy_plan().import_legacy(value, datum(i))); }
  int same(Op::WinFuncCell &cell, bool &result) { return (op_.*member(SameTag{}))(cell, result); }
  int copy(const RARow &row, ObWinbufPieceMsg &piece, int64_t capacity, char *buffer)
  { return (op_.*member(CopyTag{}))(row, piece, capacity, buffer); }
  void sort_keys()
  {
    set_type(0, ObVarcharType, CS_TYPE_UTF8MB4_GENERAL_CI);
    set_type(1, ObDecimalIntType);
    ASSERT_EQ(OB_SUCCESS, spec_.rd_sort_collations_.init(3));
    ASSERT_EQ(OB_SUCCESS, spec_.rd_sort_cmp_funcs_.init(3));
    for (int64_t i = 0; i < 3; ++i) {
      ASSERT_EQ(OB_SUCCESS, spec_.rd_sort_collations_.push_back(
          ObSortFieldCollation(i, exprs_[i].datum_meta_.cs_type_, true, NULL_FIRST)));
      ObCmpFunc function; function.cmp_func_ = exprs_[i].basic_funcs_->null_first_cmp_.legacy_function();
      ASSERT_EQ(OB_SUCCESS, spec_.rd_sort_cmp_funcs_.push_back(function));
    }
    spec_.rd_pby_sort_cnt_ = 2;
  }
  void patch_function(ObItemType function, WindowType window = WINDOW_ROWS)
  {
    ASSERT_EQ(OB_SUCCESS, spec_.wf_infos_.init(1));
    WinFuncInfo info; info.set_allocator(&arena_); info.func_type_ = function;
    info.expr_ = &exprs_[3]; info.win_type_ = window;
    ASSERT_EQ(OB_SUCCESS, spec_.wf_infos_.push_back(info));
    ASSERT_EQ(OB_SUCCESS, spec_.rd_wfs_.init(1)); ASSERT_EQ(OB_SUCCESS, spec_.rd_wfs_.push_back(0));
  }
  void partial(ObRDWFPartialInfo &info, Row &first, Row &last, int64_t thread, int64_t last_offset)
  {
    info.first_row_ = &first; info.last_row_ = &last; info.thread_id_ = thread;
    first.extra_payload<int64_t>() = 0; last.extra_payload<int64_t>() = last_offset;
  }
  void ra_store(Op::RowsStore &store)
  {
    store.ra_rs_.set_allocator(arena_);
    ASSERT_EQ(OB_SUCCESS, store.ra_rs_.init(INT64_MAX, ObCtxIds::DEFAULT_CTX_ID, "WinRaCells"));
  }
  void ra_add(Op::RowsStore &store, const std::vector<ObDatum> &values)
  {
    ObSEArray<ObDatum, 4> array;
    for (const auto &value : values) { ASSERT_EQ(OB_SUCCESS, array.push_back(value)); }
    // Bypass operator workarea admission, while retaining the real RA writer,
    // reader and payload ownership. These tests make no spill claim.
    ASSERT_EQ(OB_SUCCESS, store.ra_rs_.add_row(array));
    ++store.stored_row_cnt_; ++store.row_cnt_;
  }
  void ra_order(WinFuncInfo &info, SqlValueType result_type = ObIntType)
  {
    set_type(0, ObVarcharType, CS_TYPE_UTF8MB4_GENERAL_CI);
    set_type(3, result_type);
    info.set_allocator(&arena_); ASSERT_EQ(OB_SUCCESS, info.init(0, 0, 1));
    ASSERT_EQ(OB_SUCCESS, info.sort_collations_.init(1));
    ASSERT_EQ(OB_SUCCESS, info.sort_cmp_funcs_.init(1));
    info.expr_ = &exprs_[3];
    ASSERT_EQ(OB_SUCCESS, info.sort_exprs_.push_back(&exprs_[0]));
    ASSERT_EQ(OB_SUCCESS, info.sort_collations_.push_back(
        ObSortFieldCollation(0, CS_TYPE_UTF8MB4_GENERAL_CI, true, NULL_FIRST)));
    ObCmpFunc cmp; cmp.cmp_func_ = exprs_[0].basic_funcs_->null_first_cmp_.legacy_function();
    ASSERT_EQ(OB_SUCCESS, info.sort_cmp_funcs_.push_back(cmp));
  }
  void ra_peers(Op::RowsStore &store)
  {
    ra_store(store);
    std::string lower = "alpha", upper = "ALPHA", last = "beta";
    ra_add(store, {null_value()}); ra_add(store, {text(lower)});
    ra_add(store, {text(upper)}); ra_add(store, {text(last)});
    lower.assign(lower.size(), 'x'); upper.assign(upper.size(), 'y'); last.assign(last.size(), 'z');
    ASSERT_EQ(OB_SUCCESS, store.ra_rs_.finish_add_row());
  }
  ObArenaAllocator arena_;
  ObExecContext exec_;
  ObWindowFunctionSpec spec_;
  Op op_;
  alignas(8) std::array<char, 4096> frame_;
  char *frames_[1];
  ObEvalCtx &eval_;
  ObEvalCtx::BatchInfoScopeGuard guard_;
  ObExpr exprs_[4];
};

TEST_F(FixedWindowCells, partition_boundary_compares_owned_mixed_keys_and_typed_nulls)
{
  set_type(1, ObVarcharType, CS_TYPE_UTF8MB4_GENERAL_CI); set_type(2, ObDecimalIntType);
  WinFuncInfo info; info.set_allocator(&arena_); ASSERT_EQ(OB_SUCCESS, info.init(0, 3, 0));
  for (int i = 0; i < 3; ++i) { ASSERT_EQ(OB_SUCCESS, info.partition_exprs_.push_back(&exprs_[i])); }
  Op::NonAggrCellRowNumber cell(info, op_);
  int64_t key = INT64_MIN; std::string name = "customer";
  std::array<char, 16> coefficient{}; coefficient.fill(0x12);
  OwnedRow original({integer(key), text(name), bytes(coefficient.data(), coefficient.size())});
  ASSERT_EQ(OB_SUCCESS, cell.part_values_.save_store_row(*original.row_)); original.poison();
  const int64_t current_key = INT64_MIN; const std::string current_name = "CUSTOMER";
  std::array<char, 16> current_coefficient = coefficient;
  key = 0; name.assign(name.size(), 'x'); coefficient.fill(0);
  put(0, integer(current_key)); put(1, text(current_name));
  put(2, bytes(current_coefficient.data(), current_coefficient.size()));
  bool equal = false; ASSERT_EQ(OB_SUCCESS, same(cell, equal)); EXPECT_TRUE(equal);
  current_coefficient[0] += 1;
  ASSERT_EQ(OB_SUCCESS, same(cell, equal)); EXPECT_FALSE(equal);
  OwnedRow nulls({null_value(), null_value(), null_value()});
  ASSERT_EQ(OB_SUCCESS, cell.part_values_.save_store_row(*nulls.row_)); nulls.poison();
  datum(0).set_null(); datum(1).set_null(); datum(2).set_null();
  ASSERT_EQ(OB_SUCCESS, same(cell, equal)); EXPECT_TRUE(equal);
  put(0, integer(current_key));
  ASSERT_EQ(OB_SUCCESS, same(cell, equal)); EXPECT_FALSE(equal);
}

TEST_F(FixedWindowCells, partition_hash_reads_fixed_cells_after_source_overwrite)
{
  set_type(1, ObVarcharType, CS_TYPE_UTF8MB4_GENERAL_CI); set_type(2, ObDecimalIntType);
  ObSEArray<ObExpr *, 3> exprs;
  for (auto &e : {&exprs_[0], &exprs_[1], &exprs_[2]}) { ASSERT_EQ(OB_SUCCESS, exprs.push_back(e)); }
  int64_t key = INT64_MAX; std::string name("a\0b", 3);
  std::array<char, 16> coefficient{}; coefficient.fill(0x11);
  std::array<ObDatum, 3> values{{integer(key), text(name), bytes(coefficient.data(), coefficient.size())}};
  uint64_t expected = 99194853094755497L;
  for (int i = 0; i < 3; ++i) {
    ASSERT_EQ(OB_SUCCESS, storage_predicate_functions(exprs_[i])->murmur_hash_v2_(values[i], expected, expected, nullptr));
  }
  OwnedRow owned({values[0], values[1], values[2]});
  key = 0; name.assign(name.size(), 'x'); coefficient.fill(0);
  uint64_t actual = 0;
  ASSERT_EQ(OB_SUCCESS, (op_.*member(HashTag{}))(&exprs, owned.row_, actual));
  EXPECT_EQ(expected, actual);
  OwnedRow nulls({null_value(), null_value(), null_value()}); expected = 99194853094755497L;
  for (int i = 0; i < 3; ++i) {
    ASSERT_EQ(OB_SUCCESS, storage_predicate_functions(exprs_[i])->murmur_hash_v2_(null_value(), expected, expected, nullptr));
  }
  ASSERT_EQ(OB_SUCCESS, (op_.*member(HashTag{}))(&exprs, nulls.row_, actual)); EXPECT_EQ(expected, actual);
}

TEST_F(FixedWindowCells, partial_key_comparison_handles_chunk_ra_headers_nulls_and_direction)
{
  sort_keys();
  const std::string lower = "customer", upper = "CUSTOMER";
  std::array<char, 16> coefficient{}; coefficient.fill(0x11);
  int64_t first = 3, second = 4;
  OwnedRow chunk({text(lower), bytes(coefficient.data(), coefficient.size()), integer(first)});
  OwnedRARow ra({text(upper), bytes(coefficient.data(), coefficient.size()), integer(second)});
  int cmp = 9;
  ASSERT_EQ(OB_SUCCESS, spec_.rd_pby_cmp(chunk.row_, ra.row_, cmp, nullptr)); EXPECT_EQ(0, cmp);
  ASSERT_EQ(OB_SUCCESS, spec_.rd_oby_cmp(chunk.row_, ra.row_, cmp, nullptr)); EXPECT_LT(cmp, 0);
  spec_.rd_sort_collations_.at(2).is_ascending_ = false;
  ASSERT_EQ(OB_SUCCESS, spec_.rd_oby_cmp(ra.row_, chunk.row_, cmp, nullptr)); EXPECT_LT(cmp, 0);
  OwnedRow nulls({null_value(), null_value(), null_value()});
  ASSERT_EQ(OB_SUCCESS, spec_.rd_pby_cmp(nulls.row_, ra.row_, cmp, nullptr)); EXPECT_LT(cmp, 0);
  ASSERT_EQ(OB_SUCCESS, spec_.rd_pby_cmp(static_cast<const Row *>(nullptr), ra.row_, cmp, nullptr)); EXPECT_GT(cmp, 0);
}

TEST_F(FixedWindowCells, real_rows_patch_keeps_prefix_results_and_partition_boundaries)
{
  sort_keys(); patch_function(T_FUN_SUM);
  const std::string name = "customer", next = "other___";
  std::array<char, 16> coefficient{}; coefficient.fill(0x11);
  // Integers passed to OwnedRow must outlive its synchronous deep copy.
  int64_t k1=1,k2=2,k3=3,k4=4,k5=5,k6=6,k7=7,k8=8,s1=10,s2=20,s3=30,s4=40,s5=50,s6=60,s7=70,s8=80;
  OwnedRow a(partial_values(name, coefficient, k1, s1), 8), b(partial_values(name, coefficient, k2, s2), 8);
  OwnedRow c(partial_values(name, coefficient, k3, s3), 8), d(partial_values(name, coefficient, k4, s4), 8);
  OwnedRow e(partial_values(name, coefficient, k5, s5), 8), f(partial_values(name, coefficient, k6, s6), 8);
  OwnedRow g(partial_values(next, coefficient, k7, s7), 8), h(partial_values(next, coefficient, k8, s8), 8);
  ObRDWFPartialInfo p1(arena_),p2(arena_),p3(arena_),p4(arena_);
  partial(p1,*a.row_,*b.row_,1,1); partial(p2,*c.row_,*d.row_,2,1);
  partial(p3,*e.row_,*f.row_,3,1); partial(p4,*g.row_,*h.row_,4,1);
  ObRDWFPieceMsgCtx context(1,4,0,exec_);
  for (auto *p : {&p4,&p2,&p1,&p3}) { ASSERT_EQ(OB_SUCCESS, context.infos_.push_back(p)); }
  s2=s4=s6=0; coefficient.fill(0);
  ASSERT_EQ(OB_SUCCESS, spec_.rd_generate_patch(context));
  EXPECT_TRUE(a.row_->stored_cells()[3].is_null()); EXPECT_EQ(20,number(c.row_->stored_cells()[3]));
  EXPECT_EQ(60,number(e.row_->stored_cells()[3])); EXPECT_TRUE(g.row_->stored_cells()[3].is_null());
  EXPECT_TRUE(f.row_->stored_cells()[3].is_null()); EXPECT_TRUE(h.row_->stored_cells()[3].is_null());
  EXPECT_EQ(2,c.row_->extra_payload<int64_t>()); EXPECT_EQ(4,e.row_->extra_payload<int64_t>());
}

TEST_F(FixedWindowCells, real_range_patch_merges_following_equal_order_partial_values)
{
  sort_keys(); patch_function(T_FUN_SUM, WINDOW_RANGE);
  const std::string name="customer"; std::array<char,16> coefficient{}; coefficient.fill(0x11);
  int64_t key=1,one=1,two=2,three=3;
  OwnedRow a(partial_values(name, coefficient, key, one), 8), b(partial_values(name, coefficient, key, one), 8);
  OwnedRow c(partial_values(name, coefficient, key, two), 8), d(partial_values(name, coefficient, key, two), 8);
  OwnedRow e(partial_values(name, coefficient, key, three), 8), f(partial_values(name, coefficient, key, three), 8);
  ObRDWFPartialInfo p1(arena_),p2(arena_),p3(arena_);
  partial(p1,*a.row_,*b.row_,1,0);partial(p2,*c.row_,*d.row_,2,0);partial(p3,*e.row_,*f.row_,3,0);
  ObRDWFPieceMsgCtx context(1,3,0,exec_);
  for(auto *p:{&p3,&p1,&p2}){ASSERT_EQ(OB_SUCCESS,context.infos_.push_back(p));}
  ASSERT_EQ(OB_SUCCESS,spec_.rd_generate_patch(context));
  EXPECT_EQ(5,number(b.row_->stored_cells()[3]));EXPECT_EQ(1,number(c.row_->stored_cells()[3]));
  EXPECT_EQ(3,number(d.row_->stored_cells()[3]));EXPECT_EQ(3,number(e.row_->stored_cells()[3]));
  EXPECT_TRUE(a.row_->stored_cells()[3].is_null());EXPECT_TRUE(f.row_->stored_cells()[3].is_null());
}

TEST_F(FixedWindowCells, real_rank_patch_publishes_arena_result_and_frame_offset)
{
  sort_keys(); set_type(3,ObUInt64Type); patch_function(T_WIN_FUN_RANK);
  const std::string name="customer";std::array<char,16> coefficient{};coefficient.fill(0x11);
  int64_t k1=1,k2=2,k3=3,one=1,five=5,two=2;
  OwnedRow a(partial_values(name, coefficient, k1, one), 8), b(partial_values(name, coefficient, k2, five), 8);
  OwnedRow c(partial_values(name, coefficient, k2, one), 8), d(partial_values(name, coefficient, k3, two), 8);
  ObRDWFPartialInfo p1(arena_),p2(arena_);partial(p1,*a.row_,*b.row_,1,4);partial(p2,*c.row_,*d.row_,2,1);
  ObRDWFPieceMsgCtx context(1,2,0,exec_);
  ASSERT_EQ(OB_SUCCESS,context.infos_.push_back(&p2));ASSERT_EQ(OB_SUCCESS,context.infos_.push_back(&p1));
  ASSERT_EQ(OB_SUCCESS,spec_.rd_generate_patch(context));
  EXPECT_EQ(4,number(c.row_->stored_cells()[3]));EXPECT_EQ(5,c.row_->extra_payload<int64_t>());
  EXPECT_TRUE(a.row_->stored_cells()[3].is_null());EXPECT_TRUE(b.row_->stored_cells()[3].is_null());EXPECT_TRUE(d.row_->stored_cells()[3].is_null());
}

TEST_F(FixedWindowCells, ra_to_chunk_uses_exact_body_capacity_and_relocates_payload_and_extra)
{
  int64_t n=17;const std::string s("a\0b",3);std::array<char,16> decimal{};decimal.fill(0x35);
  ObDatum flagged=bytes(decimal.data(),decimal.size());flagged.set_has_lob_header();
  OwnedRARow source({integer(n),text(s),null_value(),flagged},8);
  for(int i=0;i<8;++i){static_cast<char *>(source.row_->get_extra_payload())[i]=char(0x20+i);}
  const int64_t size=source.row_->body_size();
  GuardedBody guarded(size);ASSERT_NE(nullptr,guarded.get());
  auto *ra=new(guarded.get()) RARow;ASSERT_EQ(OB_SUCCESS,ra->assign(source.row_));
  std::vector<char> destination(size+16,0x61);ObWinbufPieceMsg piece;
  ASSERT_EQ(OB_SUCCESS,copy(*ra,piece,size,destination.data()));
  ASSERT_NE(nullptr,piece.datum_row_);EXPECT_EQ(4,piece.datum_row_->cnt_);EXPECT_EQ(size,piece.row_size_);
  EXPECT_EQ(size-int64_t(sizeof(Row)),piece.payload_len_);EXPECT_EQ(4,piece.col_count_);
  EXPECT_EQ(size,piece.datum_row_->row_size_);EXPECT_EQ(source.row_->row_size_-4,piece.row_size_);
  EXPECT_EQ(17,number(piece.datum_row_->stored_cells()[0]));EXPECT_TRUE(piece.datum_row_->stored_cells()[2].is_null());
  EXPECT_EQ(flagged.flag_,piece.datum_row_->stored_cells()[3].flag_);
  for(int i=0;i<8;++i){EXPECT_EQ(char(0x20+i),static_cast<char *>(piece.datum_row_->get_extra_payload())[i]);}
  MEMSET(guarded.get(),0xA5,size);MEMSET(source.memory_.data(),0xA5,source.memory_.size());
  const auto *cells=piece.datum_row_->stored_cells();
  EXPECT_EQ(17,number(cells[0]));EXPECT_EQ(s,std::string(cells[1].ptr_,cells[1].len_));
  EXPECT_EQ(0,MEMCMP(decimal.data(),cells[3].ptr_,decimal.size()));
  const uintptr_t base=reinterpret_cast<uintptr_t>(destination.data()),end=base+size;
  for(int i:{0,1,3}){const uintptr_t p=reinterpret_cast<uintptr_t>(cells[i].ptr_);EXPECT_GE(p,base);EXPECT_LE(p+cells[i].len_,end);}
  for(int64_t i=size;i<int64_t(destination.size());++i){EXPECT_EQ(char(0x61),destination[i]);}
}

TEST_F(FixedWindowCells, ra_to_chunk_preflight_errors_do_not_publish_or_modify_destination)
{
  int64_t n=17;OwnedRARow source({integer(n)});const int64_t size=source.row_->body_size();
  std::vector<char> destination(size+16,0x62);const auto original=destination;
  Row sentinel;ObWinbufPieceMsg piece;piece.datum_row_=&sentinel;piece.row_size_=7;piece.payload_len_=9;piece.col_count_=11;
  EXPECT_EQ(OB_BUF_NOT_ENOUGH,copy(*source.row_,piece,size-1,destination.data()));
  EXPECT_EQ(OB_INVALID_ARGUMENT,copy(*source.row_,piece,size,nullptr));
  source.row_->readable_=false;EXPECT_EQ(OB_INVALID_ARGUMENT,copy(*source.row_,piece,size,destination.data()));source.row_->readable_=true;
  const auto old_count=source.row_->cnt_;source.row_->cnt_+=2;
  EXPECT_EQ(OB_INVALID_ARGUMENT,copy(*source.row_,piece,size,destination.data()));source.row_->cnt_=old_count;
  const StoredDatumCell valid=source.row_->stored_cells()[0];source.row_->stored_cells()[0].ptr_=destination.data();
  EXPECT_EQ(OB_INVALID_ARGUMENT,copy(*source.row_,piece,size,destination.data()));source.row_->stored_cells()[0]=valid;
  const auto quota=source.row_->row_size_;source.row_->row_size_=4;
  EXPECT_EQ(OB_INVALID_ARGUMENT,copy(*source.row_,piece,size,destination.data()));source.row_->row_size_=quota;
  EXPECT_EQ(&sentinel,piece.datum_row_);EXPECT_EQ(7,piece.row_size_);EXPECT_EQ(9,piece.payload_len_);EXPECT_EQ(11,piece.col_count_);
  EXPECT_EQ(original,destination);
}
TEST_F(FixedWindowCells, ra_rank_and_dense_rank_keep_null_and_collation_peers)
{
  Op::RowsStore rows(op_); ra_peers(rows);
  WinFuncInfo info; ra_order(info);
  const int64_t expected[][4] = {{1, 2, 2, 4}, {1, 2, 2, 3}};
  int function = 0;
  for (auto kind : {T_WIN_FUN_RANK, T_WIN_FUN_DENSE_RANK}) {
    info.func_type_ = kind;
    Op::NonAggrCellRankLike cell(info, op_);
    Op::RowsReader reader(rows);
    for (int64_t idx = 0; idx < 4; ++idx) {
      const RARow *row = nullptr; ASSERT_EQ(OB_SUCCESS, rows.get_row(idx, row));
      ObDatum result;
      ASSERT_EQ(OB_SUCCESS, cell.eval(reader, idx, *row, Op::Frame(0, 3), result));
      EXPECT_EQ(expected[function][idx], result.get_int());
    }
    ++function;
  }
}

TEST_F(FixedWindowCells, ra_percent_rank_and_cume_dist_read_owned_peer_rows)
{
  Op::RowsStore rows(op_); ra_peers(rows);
  WinFuncInfo info; ra_order(info, ObDoubleType); info.func_type_ = T_WIN_FUN_PERCENT_RANK;
  Op::NonAggrCellRankLike percent(info, op_);
  Op::NonAggrCellCumeDist cumulative(info, op_);
  Op::RowsReader reader(rows);
  const double rank[] = {0, 1.0 / 3, 1.0 / 3, 1};
  const double cume[] = {.25, .75, .75, 1};
  for (int64_t idx = 0; idx < 4; ++idx) {
    const RARow *row = nullptr; ASSERT_EQ(OB_SUCCESS, rows.get_row(idx, row));
    ObDatum result;
    ASSERT_EQ(OB_SUCCESS, percent.eval(reader, idx, *row, Op::Frame(0, 3), result));
    EXPECT_DOUBLE_EQ(rank[idx], result.get_double());
    ASSERT_EQ(OB_SUCCESS, cumulative.eval(reader, idx, *row, Op::Frame(0, 3), result));
    EXPECT_DOUBLE_EQ(cume[idx], result.get_double());
  }
}

TEST_F(FixedWindowCells, ra_rank_and_cume_comparison_errors_do_not_publish_results)
{
  Op::RowsStore rows(op_); ra_peers(rows);
  WinFuncInfo info; ra_order(info); info.func_type_ = T_WIN_FUN_RANK;
  info.sort_cmp_funcs_.at(0).cmp_func_ = failing_ra_compare;
  Op::NonAggrCellRankLike rank(info, op_);
  Op::NonAggrCellCumeDist cumulative(info, op_);
  Op::RowsReader reader(rows);
  const RARow *row = nullptr; ASSERT_EQ(OB_SUCCESS, rows.get_row(1, row));
  int64_t sentinel = 71; ObDatum result = integer(sentinel);
  EXPECT_EQ(OB_TIMEOUT, rank.eval(reader, 1, *row, Op::Frame(0, 3), result));
  EXPECT_EQ(71, result.get_int()); EXPECT_EQ(0, rank.rank_of_prev_row_);
  EXPECT_EQ(OB_TIMEOUT, cumulative.eval(reader, 1, *row, Op::Frame(0, 3), result));
  EXPECT_EQ(71, result.get_int());
}

TEST_F(FixedWindowCells, ra_current_range_finds_both_peer_boundaries)
{
  Op::RowsStore rows(op_); ra_peers(rows);
  BorrowedStore input(op_.*member(InputStoresTag{}), rows);
  WinFuncInfo info; ra_order(info); info.win_type_ = WINDOW_RANGE;
  info.upper_.is_unbounded_ = false; info.lower_.is_unbounded_ = false;
  Op::NonAggrCellRowNumber cell(info, op_);
  Op::RowsReader reader(rows);
  const RARow *row = nullptr; ASSERT_EQ(OB_SUCCESS, rows.get_row(2, row));
  int64_t position = -1; bool got_null = true;
  ASSERT_EQ(OB_SUCCESS, (op_.*member(PositionTag{}))(reader, cell, 2, *row, true, position, got_null));
  EXPECT_EQ(1, position); EXPECT_FALSE(got_null);
  ASSERT_EQ(OB_SUCCESS, (op_.*member(PositionTag{}))(reader, cell, 2, *row, false, position, got_null));
  EXPECT_EQ(2, position); EXPECT_FALSE(got_null);
  info.sort_cmp_funcs_.at(0).cmp_func_ = failing_ra_compare;
  EXPECT_EQ(OB_TIMEOUT, (op_.*member(PositionTag{}))(reader, cell, 2, *row, true, position, got_null));
}

TEST_F(FixedWindowCells, ra_numeric_range_preserves_bound_comparison_errors)
{
  Op::RowsStore rows(op_); ra_store(rows);
  int64_t first = 1, next = 2, bound = 1;
  ra_add(rows, {integer(first)}); ra_add(rows, {integer(next)});
  ASSERT_EQ(OB_SUCCESS, rows.ra_rs_.finish_add_row());
  BorrowedStore input(op_.*member(InputStoresTag{}), rows);
  WinFuncInfo info; ra_order(info); set_type(0, ObIntType);
  info.sort_collations_.at(0).cs_type_ = CS_TYPE_BINARY;
  info.sort_cmp_funcs_.at(0).cmp_func_ = failing_ra_compare;
  info.win_type_ = WINDOW_RANGE; info.upper_.is_preceding_ = true;
  info.upper_.is_nmb_literal_ = true; info.upper_.is_unbounded_ = false;
  info.upper_.between_value_expr_ = &exprs_[1];
  info.upper_.range_bound_expr_ = &exprs_[2];
  put(1, integer(bound));
  put(2, integer(bound));
  Op::NonAggrCellRowNumber cell(info, op_); Op::RowsReader reader(rows);
  const RARow *row = nullptr; ASSERT_EQ(OB_SUCCESS, rows.get_row(1, row));
  int64_t position = -1; bool got_null = false;
  // Previously tmp_ret was logged but ignored, and the caller got success.
  EXPECT_EQ(OB_TIMEOUT, (op_.*member(PositionTag{}))(reader, cell, 1, *row, true, position, got_null));
}

TEST_F(FixedWindowCells, ra_collect_result_owns_previous_payload_before_store_reuse)
{
  WinFuncInfo first_info, second_info;
  first_info.expr_ = &exprs_[2]; second_info.expr_ = &exprs_[3];
  Op::NonAggrCellRowNumber first(first_info, op_), second(second_info, op_);
  first.wf_idx_ = 1; second.wf_idx_ = 2;
  Op::RowsStore previous(op_), output(op_); ra_store(previous); ra_store(output);
  BorrowedStore first_store(first.res_, previous), second_store(second.res_, output);
  BorrowedFunctions functions(op_.*member(FunctionsTag{}), first, second);
  auto &scratch = op_.*member(ResultsTag{});
  ASSERT_EQ(OB_SUCCESS, scratch.init(2));
  ASSERT_EQ(OB_SUCCESS, scratch.push_back(null_value()));
  ASSERT_EQ(OB_SUCCESS, scratch.push_back(null_value()));
  std::string source = "persist"; ObDatum value = text(source); value.flag_ = 2;
  ra_add(previous, {value}); source.assign(source.size(), 'x');
  int64_t number = INT64_MAX; ObDatum current = integer(number);
  ASSERT_EQ(OB_SUCCESS, (op_.*member(CollectTag{}))(0, current, second));
  const RARow *before = nullptr, *result = nullptr;
  ASSERT_EQ(OB_SUCCESS, previous.get_row(0, before));
  ASSERT_EQ(OB_SUCCESS, output.get_row(0, result));
  ASSERT_EQ(2, result->cnt_);
  EXPECT_NE(before->stored_cells()[0].ptr_, result->stored_cells()[0].ptr_);
  MEMSET(const_cast<char *>(before->stored_cells()[0].ptr_), 'y', before->stored_cells()[0].len_);
  number = 0;
  EXPECT_EQ("persist", std::string(result->stored_cells()[0].ptr_, result->stored_cells()[0].len_));
  EXPECT_EQ(2, result->stored_cells()[0].flag_);
  EXPECT_EQ(INT64_MAX, StoredDatumCodec::decode_legacy_view(result->stored_cells()[1]).get_int());
}

TEST_F(FixedWindowCells, ra_scalar_output_borrows_result_payload_and_preserves_null_flags)
{
  set_type(3, ObVarcharType);
  ASSERT_EQ(OB_SUCCESS, spec_.all_expr_.init(1));
  ASSERT_EQ(OB_SUCCESS, spec_.all_expr_.push_back(&exprs_[0]));
  WinFuncInfo first_info, second_info;
  first_info.expr_ = &exprs_[2]; second_info.expr_ = &exprs_[3];
  Op::NonAggrCellRowNumber first(first_info, op_), second(second_info, op_);
  Op::RowsStore input_rows(op_), result_rows(op_); ra_store(input_rows); ra_store(result_rows);
  BorrowedStore input(op_.*member(InputStoresTag{}), input_rows), result(second.res_, result_rows);
  BorrowedFunctions functions(op_.*member(FunctionsTag{}), first, second);
  int64_t key = INT64_MIN;
  std::string text_value = "result"; ObDatum output_value = text(text_value); output_value.flag_ = ObDatumDesc::EXT;
  ra_add(input_rows, {integer(key)}); ra_add(result_rows, {null_value(), output_value});
  key = 0; text_value.assign(text_value.size(), 'x');
  const RARow *row = nullptr; ASSERT_EQ(OB_SUCCESS, result_rows.get_row(0, row));
  // The original six-byte string with flag 2 is an invalid EXT descriptor,
  // not a present varchar. The real typed import must reject it without
  // publishing that result slot; keep this as the negative control.
  ASSERT_EQ(OB_SUCCESS, datum(3).set_string("previous", 8));
  const char *previous = datum(3).get_string().ptr();
  exprs_[3].get_eval_info(eval_).clear_evaluated_flag();
  EXPECT_EQ(OB_INVALID_ARGUMENT, (op_.*member(OutputTag{}))(0, &Op::Stores::cur_, nullptr));
  EXPECT_EQ(previous, datum(3).get_string().ptr());
  EXPECT_EQ(ObString::make_string("previous"), datum(3).get_string());
  EXPECT_FALSE(exprs_[3].get_eval_info(eval_).evaluated_);

  // Exercise the original borrow/NULL/flag oracle with a valid reference flag.
  const_cast<RARow *>(row)->stored_cells()[1].flag_ = ObDatumDesc::OUTROW;
  ASSERT_EQ(OB_SUCCESS, (op_.*member(OutputTag{}))(0, &Op::Stores::cur_, nullptr));
  EXPECT_EQ(INT64_MIN, datum(0).get_int()); EXPECT_TRUE(datum(2).is_null());
  EXPECT_EQ(row->stored_cells()[1].ptr_, datum(3).get_string().ptr());
  EXPECT_EQ(row->stored_cells()[1].len_, datum(3).length());
  EXPECT_EQ(Datum::OutRow, datum(3).reference_flags());
  common::ObDatum exported;
  alignas(8) char scratch[64];
  ASSERT_EQ(OB_SUCCESS, exprs_[3].get_datum_copy_plan().legacy_view(datum(3), exported, scratch, sizeof(scratch)));
  EXPECT_EQ(row->stored_cells()[1].flag_, exported.flag_);
  EXPECT_EQ("result", std::string(datum(3).get_string().ptr(), datum(3).length()));
}

TEST_F(FixedWindowCells, ra_batch_output_uses_each_row_index_and_advances_both_cursors)
{
  ASSERT_EQ(OB_SUCCESS, spec_.all_expr_.init(1));
  ASSERT_EQ(OB_SUCCESS, spec_.all_expr_.push_back(&exprs_[0]));
  for (int64_t col = 0; col < 4; ++col) {
    exprs_[col].batch_result_ = true; exprs_[col].batch_idx_mask_ = UINT64_MAX;
    exprs_[col].eval_flags_off_ = col * 512 + 128;
    new (frame_.data() + exprs_[col].datum_off_ + sizeof(Datum)) Datum;
  }
  WinFuncInfo first_info, second_info;
  first_info.expr_ = &exprs_[2]; second_info.expr_ = &exprs_[3];
  Op::NonAggrCellRowNumber first(first_info, op_), second(second_info, op_);
  BorrowedFunctions functions(op_.*member(FunctionsTag{}), first, second);
  Op::RowsStore input_rows(op_), result_rows(op_); ra_store(input_rows); ra_store(result_rows);
  int64_t low = INT64_MIN, high = INT64_MAX;
  ra_add(input_rows, {integer(low)}); ra_add(input_rows, {integer(high)});
  ra_add(result_rows, {integer(high), null_value()});
  ra_add(result_rows, {null_value(), integer(low)});
  low = high = 0;
  ObEvalCtx::BatchInfoScopeGuard batch(eval_); batch.set_batch_size(2); batch.set_batch_idx(0);
  ASSERT_EQ(OB_SUCCESS, (op_.*member(OutputBatchTag{}))(2, input_rows, result_rows, batch));
  EXPECT_EQ(INT64_MIN, exprs_[0].locate_batch_datums(eval_)[0].get_int());
  EXPECT_EQ(INT64_MAX, exprs_[0].locate_batch_datums(eval_)[1].get_int());
  EXPECT_EQ(INT64_MAX, exprs_[2].locate_batch_datums(eval_)[0].get_int());
  EXPECT_TRUE(exprs_[2].locate_batch_datums(eval_)[1].is_null());
  EXPECT_TRUE(exprs_[3].locate_batch_datums(eval_)[0].is_null());
  EXPECT_EQ(INT64_MIN, exprs_[3].locate_batch_datums(eval_)[1].get_int());
  EXPECT_EQ(2, input_rows.output_row_idx_); EXPECT_EQ(2, result_rows.output_row_idx_);
}

TEST_F(FixedWindowCells, aggregate_final_inline_cache_survives_frame_overwrite_and_restart)
{
  AggrInfoFixedArray infos(arena_);
  ObAggrInfo info(arena_); info.real_aggr_type_ = T_FUN_MAX; info.expr_ = &exprs_[3];
  ASSERT_EQ(OB_SUCCESS, info.param_exprs_.init(1));
  ASSERT_EQ(OB_SUCCESS, info.param_exprs_.push_back(&exprs_[3]));
  ASSERT_EQ(OB_SUCCESS, infos.init(1)); ASSERT_EQ(OB_SUCCESS, infos.push_back(info));
  WinFuncInfo wf; wf.aggr_info_.real_aggr_type_ = T_FUN_MAX; wf.aggr_info_.expr_ = &exprs_[3];
  Op::AggrCell cell(wf, op_, infos);
  ASSERT_EQ(OB_SUCCESS, cell.aggr_processor_.init_one_group());
  ObAggregateProcessor::GroupRow *group = nullptr;
  ASSERT_EQ(OB_SUCCESS, cell.aggr_processor_.get_group_row(0, group)); ASSERT_NE(nullptr, group);
  int64_t input = INT64_MAX;
  ASSERT_EQ(OB_SUCCESS, cell.aggr_processor_.clone_aggr_cell(group->aggr_cells_[0], integer(input), exprs_[3].get_sql_type()));
  ObDatum output;
  ASSERT_EQ(OB_SUCCESS, cell.final(output));
  EXPECT_TRUE(cell.got_result_); EXPECT_EQ(INT64_MAX, output.get_int());
  EXPECT_EQ(INT64_MAX, cell.result_.get_int()); EXPECT_EQ(0, cell.result_.length());
  EXPECT_EQ(nullptr, cell.result_buffer_); EXPECT_EQ(0, cell.result_capacity_);
  input = 0;
  ASSERT_EQ(OB_SUCCESS, cell.aggr_processor_.clone_aggr_cell(
      group->aggr_cells_[0], integer(input), exprs_[3].get_sql_type()));
  datum(3).set_int(17);
  ASSERT_EQ(OB_SUCCESS, cell.final(output)); EXPECT_EQ(INT64_MAX, output.get_int());
  cell.reset_for_restart();
  EXPECT_FALSE(cell.got_result_); EXPECT_TRUE(cell.result_.is_null());
  EXPECT_EQ(nullptr, cell.result_buffer_); EXPECT_EQ(0, cell.result_capacity_);
  ASSERT_EQ(OB_SUCCESS, cell.aggr_processor_.init_one_group());
  ASSERT_EQ(OB_SUCCESS, cell.final(output));
  EXPECT_TRUE(output.is_null()); EXPECT_TRUE(cell.result_.is_null());
  EXPECT_TRUE(cell.got_result_); EXPECT_EQ(nullptr, cell.result_buffer_);
  cell.reset_for_restart();
  ASSERT_EQ(OB_SUCCESS, cell.aggr_processor_.init_one_group());
  ASSERT_EQ(OB_SUCCESS, cell.aggr_processor_.get_group_row(0, group)); ASSERT_NE(nullptr, group);
  const char malformed[7] = {};
  ASSERT_EQ(OB_INVALID_ARGUMENT, cell.aggr_processor_.clone_aggr_cell(
      group->aggr_cells_[0], bytes(malformed, sizeof(malformed)), exprs_[3].get_sql_type()));
  EXPECT_TRUE(group->aggr_cells_[0].native_iter_result().is_null());
  ASSERT_EQ(OB_SUCCESS, cell.aggr_processor_.clone_aggr_cell(
      group->aggr_cells_[0], integer(input), exprs_[3].get_sql_type()));
  // Deliberately corrupt only the accumulator to retain the original final
  // error/no-publication assertion in addition to checked input rejection.
  ASSERT_EQ(OB_SUCCESS, const_cast<Datum &>(group->aggr_cells_[0].native_iter_result())
      .set_reference(malformed, sizeof(malformed)));
  EXPECT_EQ(OB_INVALID_ARGUMENT, cell.final(output));
  EXPECT_FALSE(cell.got_result_); EXPECT_TRUE(cell.result_.is_null());
  EXPECT_EQ(nullptr, cell.result_buffer_); EXPECT_EQ(0, cell.result_capacity_);
}

TEST_F(FixedWindowCells, aggregate_final_reference_cache_owns_first_and_repeated_fixed_row_views)
{
  set_type(3, ObVarcharType, CS_TYPE_UTF8MB4_BIN);
  AggrInfoFixedArray infos(arena_);
  ObAggrInfo info(arena_); info.real_aggr_type_ = T_FUN_MAX; info.expr_ = &exprs_[3];
  ASSERT_EQ(OB_SUCCESS, info.param_exprs_.init(1));
  ASSERT_EQ(OB_SUCCESS, info.param_exprs_.push_back(&exprs_[3]));
  ASSERT_EQ(OB_SUCCESS, infos.init(1)); ASSERT_EQ(OB_SUCCESS, infos.push_back(info));
  WinFuncInfo wf; wf.aggr_info_.real_aggr_type_ = T_FUN_MAX; wf.aggr_info_.expr_ = &exprs_[3];
  Op::AggrCell cell(wf, op_, infos);
  ASSERT_EQ(OB_SUCCESS, cell.aggr_processor_.init_one_group());
  ObAggregateProcessor::GroupRow *group = nullptr;
  ASSERT_EQ(OB_SUCCESS, cell.aggr_processor_.get_group_row(0, group)); ASSERT_NE(nullptr, group);
  std::string source = "retained-window";
  ASSERT_EQ(OB_SUCCESS, cell.aggr_processor_.clone_aggr_cell(group->aggr_cells_[0], text(source), exprs_[3].get_sql_type()));
  ObDatum output;
  ASSERT_EQ(OB_SUCCESS, cell.final(output));
  ASSERT_TRUE(cell.got_result_);
  const char *cached = cell.result_.get_string().ptr();
  EXPECT_EQ(cached, output.ptr_);
  EXPECT_NE(static_cast<const char *>(group->aggr_cells_[0].native_iter_result().get_reference()), cached);
  EXPECT_NE(datum(3).get_string().ptr(), cached);
  const int64_t capacity = cell.result_capacity_;
  source.assign(source.size(), 'x');
  MEMSET(const_cast<char *>(static_cast<const char *>(group->aggr_cells_[0].native_iter_result().get_reference())), 'y', output.len_);
  MEMSET(const_cast<char *>(datum(3).get_string().ptr()), 'z', datum(3).length());
  ASSERT_EQ(OB_SUCCESS, cell.final(output));
  EXPECT_EQ("retained-window", std::string(output.ptr_, output.len_));
  EXPECT_EQ(cached, output.ptr_);
  // The real fixed-row store synchronously owns the exported cache view.
  Op::RowsStore rows(op_); ra_store(rows); ra_add(rows, {output});
  cell.reset_for_restart();
  EXPECT_FALSE(cell.got_result_); EXPECT_TRUE(cell.result_.is_null());
  EXPECT_EQ(nullptr, cell.result_buffer_); EXPECT_EQ(0, cell.result_capacity_);
  const RARow *stored = nullptr; ASSERT_EQ(OB_SUCCESS, rows.get_row(0, stored));
  ASSERT_NE(nullptr, stored); EXPECT_EQ("retained-window",
      std::string(stored->stored_cells()[0].ptr_, stored->stored_cells()[0].len_));
  EXPECT_GE(capacity, 15);
}

TEST_F(FixedWindowCells, aggregate_final_number_reserves_reusable_span_and_null_does_not_invalidate_capacity)
{
  set_type(3, ObNumberType);
  AggrInfoFixedArray infos(arena_);
  ObAggrInfo info(arena_); info.real_aggr_type_ = T_FUN_MAX; info.expr_ = &exprs_[3];
  ASSERT_EQ(OB_SUCCESS, info.param_exprs_.init(1));
  ASSERT_EQ(OB_SUCCESS, info.param_exprs_.push_back(&exprs_[3]));
  ASSERT_EQ(OB_SUCCESS, infos.init(1)); ASSERT_EQ(OB_SUCCESS, infos.push_back(info));
  WinFuncInfo wf; wf.aggr_info_.real_aggr_type_ = T_FUN_MAX; wf.aggr_info_.expr_ = &exprs_[3];
  Op::AggrCell cell(wf, op_, infos);
  ASSERT_EQ(OB_SUCCESS, cell.aggr_processor_.init_one_group());
  ObAggregateProcessor::GroupRow *group = nullptr;
  ASSERT_EQ(OB_SUCCESS, cell.aggr_processor_.get_group_row(0, group)); ASSERT_NE(nullptr, group);
  number::ObNumber input_number;
  ObNumStackAllocator<> temporary;
  ASSERT_EQ(OB_SUCCESS, input_number.from(INT64_MAX, temporary));
  alignas(8) char source_bytes[number::ObNumber::MAX_BYTE_LEN];
  Datum source;
  ASSERT_EQ(OB_SUCCESS, source.set_number(input_number, source_bytes, sizeof(source_bytes)));
  alignas(8) char scratch[64]; ObDatum source_view;
  ASSERT_EQ(OB_SUCCESS, exprs_[3].get_datum_copy_plan().legacy_view(source, source_view, scratch, sizeof(scratch)));
  const std::string expected(source_view.ptr_, source_view.len_);
  ASSERT_EQ(OB_SUCCESS, cell.aggr_processor_.clone_aggr_cell(group->aggr_cells_[0], source_view, exprs_[3].get_sql_type()));
  ObDatum output;
  ASSERT_EQ(OB_SUCCESS, cell.final(output));
  char *cached = cell.result_buffer_; const int64_t capacity = cell.result_capacity_;
  EXPECT_GE(capacity, static_cast<int64_t>(number::ObNumber::MAX_BYTE_LEN));
  EXPECT_EQ(expected, std::string(output.ptr_, output.len_));
  MEMSET(source_bytes, 0xA5, sizeof(source_bytes));
  MEMSET(const_cast<char *>(static_cast<const char *>(group->aggr_cells_[0].native_iter_result().get_reference())), 0xA5, output.len_);
  datum(3).set_null();
  ASSERT_EQ(OB_SUCCESS, cell.final(output));
  EXPECT_EQ(expected, std::string(output.ptr_, output.len_));
  group->aggr_cells_[0].clear_iter_value(); cell.got_result_ = false;
  ASSERT_EQ(OB_SUCCESS, cell.final(output));
  EXPECT_TRUE(output.is_null()); EXPECT_TRUE(cell.result_.is_null());
  EXPECT_EQ(cached, cell.result_buffer_); EXPECT_EQ(capacity, cell.result_capacity_);
}

TEST_F(FixedWindowCells, actual_window_sum_overflow_inverse_final_and_restart_keep_number_owner)
{
  ASSERT_EQ(OB_SUCCESS, share::ObSysVariables::init_default_values());
  ObSQLSessionInfo session;
  ASSERT_EQ(OB_SUCCESS, session.test_init(1, 1, nullptr));
  ASSERT_EQ(OB_SUCCESS, session.load_default_sys_variable(false, false));
  struct SessionScope {
    ObExecContext &exec_; ObSQLSessionInfo &session_;
    ~SessionScope() { ObSQLSessionInfo::ExecCtxSessionRegister clear(session_, nullptr); exec_.set_my_session(nullptr); }
  } session_scope{exec_, session};
  exec_.set_my_session(&session);
  ObSQLSessionInfo::ExecCtxSessionRegister registration(session, &exec_);
  set_type(3, ObNumberType);
  AggrInfoFixedArray infos(arena_);
  ObAggrInfo info(arena_); info.real_aggr_type_ = T_FUN_SUM; info.expr_ = &exprs_[3];
  ASSERT_EQ(OB_SUCCESS, info.param_exprs_.init(1));
  ASSERT_EQ(OB_SUCCESS, info.param_exprs_.push_back(&exprs_[0]));
  ASSERT_EQ(OB_SUCCESS, infos.init(1)); ASSERT_EQ(OB_SUCCESS, infos.push_back(info));
  WinFuncInfo wf; wf.aggr_info_.real_aggr_type_ = T_FUN_SUM; wf.aggr_info_.expr_ = &exprs_[3];
  wf.remove_type_ = REMOVE_STATISTICS;
  Op::AggrCell cell(wf, op_, infos);
  cell.aggr_processor_.set_in_window_func();
  ASSERT_EQ(OB_SUCCESS, cell.aggr_processor_.init());
  const int64_t maximum = INT64_MAX, one = 1;
  OwnedRARow first({integer(maximum)}), next({integer(one)});
  datum(0).set_int(maximum);
  ASSERT_EQ(OB_SUCCESS, cell.trans(*first.row_));
  datum(0).set_int(one);
  ASSERT_EQ(OB_SUCCESS, cell.trans(*next.row_));
  ObDatum output;
  ASSERT_EQ(OB_SUCCESS, cell.final(output));
  EXPECT_EQ(0, number::ObNumber(output.get_number()).compare(uint64_t(1) << 63));
  const char *cached = cell.result_.get_string().ptr();
  datum(0).set_int(77); datum(3).set_null();
  ASSERT_EQ(OB_SUCCESS, cell.final(output));
  EXPECT_EQ(cached, cell.result_.get_string().ptr());
  EXPECT_EQ(0, number::ObNumber(output.get_number()).compare(uint64_t(1) << 63));
  datum(0).set_int(one);
  ASSERT_EQ(OB_SUCCESS, cell.inv_trans(*next.row_));
  ASSERT_EQ(OB_SUCCESS, cell.final(output));
  EXPECT_EQ(0, number::ObNumber(output.get_number()).compare(maximum));
  cell.reset_for_restart();
  EXPECT_FALSE(cell.got_result_); EXPECT_TRUE(cell.result_.is_null());
  datum(0).set_int(17);
  ASSERT_EQ(OB_SUCCESS, cell.trans(*first.row_));
  ASSERT_EQ(OB_SUCCESS, cell.final(output));
  EXPECT_EQ(0, number::ObNumber(output.get_number()).compare(int64_t(17)));
}

} // namespace

int main(int argc,char **argv)
{
  ::testing::InitGoogleTest(&argc,argv);
  if(ObCharset::init_charset()!=OB_SUCCESS){return 1;}
  return RUN_ALL_TESTS();
}
