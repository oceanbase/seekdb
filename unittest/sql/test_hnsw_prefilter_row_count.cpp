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

#include <gtest/gtest.h>
#include "data_plane/access/ob_table_scan_param.h"
#include "query/vector/ob_vector_index_adaptor.h"

namespace oceanbase
{
namespace sql
{
using namespace storage;
using namespace share;
}
}

#include "sql/das/iter/ob_das_hnsw_scan_iter.h"
#include "sql/das/iter/ob_das_scan_iter.h"
#include "sql/das/ob_das_scan_op.h"
#include "sql/das/ob_das_vec_define.h"
#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
namespace sql
{
using namespace common;

class TestHnswPrefilterRowCount : public ::testing::Test
{
protected:
  // Replace only storage reads. Both production pre-filter loops, rowkey
  // conversion, lookup batching, and adaptive retry checks run unchanged.
  class ScanIter final : public ObDASScanIter
  {
  public:
    ScanIter() : remaining_(0), terminal_ret_(OB_ITER_END), lookup_param_(nullptr) {}
    int64_t remaining_;
    int terminal_ret_;
    storage::ObTableScanParam *lookup_param_;
    void clear_evaluated_flag() override {}
    int rescan() override
    {
      remaining_ = lookup_param_->key_ranges_.count();
      return OB_SUCCESS;
    }
  protected:
    int inner_init(ObDASIterParam &) override { return OB_SUCCESS; }
    int inner_reuse() override
    {
      remaining_ = 0;
      if (lookup_param_ != nullptr) {
        lookup_param_->key_ranges_.reuse();
      }
      return OB_SUCCESS;
    }
    int inner_release() override { return OB_SUCCESS; }
    int inner_get_next_row() override
    {
      if (remaining_ > 0) {
        --remaining_;
        return OB_SUCCESS;
      }
      return terminal_ret_;
    }
    int inner_get_next_rows(int64_t &count, int64_t capacity) override
    {
      count = MIN(remaining_, capacity);
      remaining_ -= count;
      return remaining_ == 0 ? terminal_ret_ : OB_SUCCESS;
    }
  };

  struct ScanParam : ObDASIterParam
  {
    bool is_valid() const override { return true; }
  };

  TestHnswPrefilterRowCount()
    : allocator_("HnswPreTest"), exec_ctx_(allocator_), eval_ctx_(exec_ctx_),
      ctdef_(allocator_), rowkey_ctdef_(allocator_),
      source_(*new (allocator_.alloc(sizeof(ScanIter))) ScanIter()),
      lookup_(*new (allocator_.alloc(sizeof(ScanIter))) ScanIter()),
      iter_(*new (allocator_.alloc(sizeof(ObDASHNSWScanIter))) ObDASHNSWScanIter()),
      result_ctx_(0, &allocator_, &allocator_), adaptor_(nullptr)
  {}

  void SetUp() override
  {
    ScanParam param;
    ASSERT_EQ(OB_SUCCESS, source_.init(param));
    ASSERT_EQ(OB_SUCCESS, lookup_.init(param));
    frames_[0] = reinterpret_cast<char *>(datums_);
    eval_ctx_.frames_ = frames_;
    eval_ctx_.set_max_batch_size(0);
    for (int64_t i = 0; i < BATCH_SIZE; ++i) {
      datums_[i].ptr_ = reinterpret_cast<char *>(&values_[i]);
      datums_[i].set_int(i + 1);
    }
    expr_.frame_idx_ = 0;
    expr_.datum_off_ = 0;
    expr_.obj_meta_.set_int();
    expr_.obj_datum_map_ = ObDatum::get_obj_datum_map_type(ObIntType);
    ASSERT_EQ(OB_SUCCESS, rowkey_ctdef_.rowkey_exprs_.init(1));
    ASSERT_EQ(OB_SUCCESS, rowkey_ctdef_.rowkey_exprs_.push_back(&expr_));
    rowkey_ctdef_.ir_scan_type_ = ObTSCIRScanType::OB_VEC_ROWKEY_VID_SCAN;
    rowkey_ctdef_.ref_table_id_ = 1001;
    rowkey_rtdef_.eval_ctx_ = &eval_ctx_;
    for (int64_t i = 0; i < CHILD_COUNT; ++i) {
      ctdefs_[i] = &rowkey_ctdef_;
      rtdefs_[i] = &rowkey_rtdef_;
    }
    ctdef_.children_ = ctdefs_;
    ctdef_.children_cnt_ = CHILD_COUNT;
    ctdef_.inv_scan_vec_id_col_ = &expr_;
    rtdef_.ctdef_ = &ctdef_;
    rtdef_.children_ = rtdefs_;
    rtdef_.children_cnt_ = CHILD_COUNT;
    rtdef_.eval_ctx_ = &eval_ctx_;
    iter_.vec_aux_ctdef_ = &ctdef_;
    iter_.vec_aux_rtdef_ = &rtdef_;
    iter_.inv_idx_scan_iter_ = &source_;
    iter_.rowkey_vid_iter_ = &lookup_;
    iter_.rowkey_vid_iter_first_scan_ = false;
    lookup_.lookup_param_ = &iter_.rowkey_vid_scan_param_;
    iter_.adaptive_ctx_.row_count_ = 137000;
    lib::ContextParam context_param;
    context_param.set_mem_attr("HnswPreTest", ObCtxIds::DEFAULT_CTX_ID);
    ASSERT_EQ(OB_SUCCESS, CURRENT_CONTEXT->CREATE_CONTEXT(iter_.mem_context_, context_param));
    adaptor_ = new ObPluginVectorIndexAdaptor(&allocator_, iter_.mem_context_);
    ASSERT_EQ(OB_SUCCESS, result_ctx_.init_prefilter(0, 100000));
  }

  void TearDown() override
  {
    delete adaptor_;
    adaptor_ = nullptr;
    // DAS iterators use arena allocation and explicit release, like the
    // production iterator factory; the arena reclaims their object storage.
    EXPECT_EQ(OB_SUCCESS, iter_.release());
  }

  int scan(const bool primary, const bool vectorized, const int64_t rows,
           const int terminal_ret = OB_ITER_END, const int64_t preceding_rows = 0,
           const bool can_retry = false)
  {
    iter_.adaptive_ctx_.pre_scan_row_cnt_ = preceding_rows;
    iter_.adaptive_ctx_.is_primary_index_ = primary;
    iter_.can_retry_ = can_retry;
    eval_ctx_.set_max_batch_size(vectorized ? BATCH_SIZE : 0);
    int64_t vids_buffer[20000];
    int64_t *vids = vids_buffer;
    int brute_count = 0;
    int ret = OB_SUCCESS;
    if (primary) {
      lookup_.remaining_ = rows;
      lookup_.terminal_ret_ = terminal_ret;
      ret = iter_.process_adaptor_state_pre_filter_with_rowkey(
          &result_ctx_, adaptor_, vids, brute_count, vectorized);
    } else {
      source_.remaining_ = rows;
      source_.terminal_ret_ = terminal_ret;
      ret = iter_.get_vid_from_idx_filter(
          &result_ctx_, adaptor_, vids, brute_count, vectorized, BATCH_SIZE);
    }
    return ret;
  }

  int64_t scanned_rows() const { return iter_.adaptive_ctx_.pre_scan_row_cnt_; }

  static constexpr int64_t BATCH_SIZE = 1000;
  static constexpr int64_t CHILD_COUNT = 8;
  ObArenaAllocator allocator_;
  ObExecContext exec_ctx_;
  ObEvalCtx eval_ctx_;
  ObExpr expr_;
  ObDatum datums_[BATCH_SIZE];
  int64_t values_[BATCH_SIZE];
  char *frames_[1];
  ObDASVecAuxScanCtDef ctdef_;
  ObDASVecAuxScanRtDef rtdef_;
  ObDASScanCtDef rowkey_ctdef_;
  ObDASScanRtDef rowkey_rtdef_;
  ObDASBaseCtDef *ctdefs_[CHILD_COUNT];
  ObDASBaseRtDef *rtdefs_[CHILD_COUNT];
  ScanIter &source_;
  ScanIter &lookup_;
  ObDASHNSWScanIter &iter_;
  ObVectorQueryAdaptorResultContext result_ctx_;
  ObPluginVectorIndexAdaptor *adaptor_;
};

TEST_F(TestHnswPrefilterRowCount, scalar_primary_counts_returned_rows)
{
  for (const int64_t rows : {0, 1, 999, 1000, 1001}) {
    SCOPED_TRACE(rows);
    EXPECT_EQ(OB_SUCCESS, scan(true, false, rows));
    EXPECT_EQ(rows, scanned_rows());
  }
}

TEST_F(TestHnswPrefilterRowCount, scalar_index_counts_returned_rows)
{
  for (const int64_t rows : {0, 1, 999, 1000, 1001}) {
    SCOPED_TRACE(rows);
    EXPECT_EQ(OB_ITER_END, scan(false, false, rows));
    EXPECT_EQ(rows, scanned_rows());
  }
}

TEST_F(TestHnswPrefilterRowCount, vectorized_paths_count_partial_and_empty_batches)
{
  for (const bool primary : {false, true}) {
    for (const int64_t rows : {0, 1, 999, 1000, 1001}) {
      SCOPED_TRACE(primary);
      SCOPED_TRACE(rows);
      const int ret = scan(primary, true, rows);
      EXPECT_TRUE(ret == OB_SUCCESS || ret == OB_ITER_END);
      EXPECT_EQ(rows, scanned_rows());
    }
  }
}

TEST_F(TestHnswPrefilterRowCount, scalar_paths_do_not_count_failed_reads)
{
  for (const bool primary : {false, true}) {
    for (const int64_t rows : {0, 1}) {
      SCOPED_TRACE(primary);
      EXPECT_EQ(OB_EAGAIN, scan(primary, false, rows, OB_EAGAIN));
      EXPECT_EQ(rows, scanned_rows());
    }
  }
}

TEST_F(TestHnswPrefilterRowCount, eof_does_not_cross_brute_force_retry_boundary)
{
  // Continue after an earlier 20,000-row scan. EOF must not invent a new
  // batch, cross the 20,000-row budget and 15% selectivity limit, or retry.
  EXPECT_EQ(OB_ITER_END, scan(false, false, 0, OB_ITER_END, 20000, true));
  EXPECT_EQ(20000, scanned_rows());
  EXPECT_EQ(OB_SUCCESS, scan(true, false, 0, OB_ITER_END, 20000, true));
  EXPECT_EQ(20000, scanned_rows());
}

TEST_F(TestHnswPrefilterRowCount, genuine_high_selectivity_still_retries)
{
  EXPECT_EQ(OB_VECTOR_INDEX_ADAPTIVE_NEED_RETRY,
            scan(false, false, 0, OB_ITER_END, 21000, true));
  EXPECT_EQ(21000, scanned_rows());
}

} // namespace sql
} // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
