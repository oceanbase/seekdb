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
#ifndef __SQL_ENG_P2P_RUNTIME_FILTER_DH_MSG_H__
#define __SQL_ENG_P2P_RUNTIME_FILTER_DH_MSG_H__
#include "lib/ob_define.h"
#include "lib/hash/ob_hashmap.h"
#include "lib/container/ob_array.h"
#include "common/datum/ob_datum.h"
#include "sql/engine/px/ob_px_bloom_filter.h"
#include "sql/engine/px/p2p_datahub/ob_p2p_dh_msg.h"
#include "sql/engine/px/p2p_datahub/ob_runtime_filter_query_range.h"


namespace oceanbase
{
namespace sql
{
class ObDynamicFilterExecutor;

class ObP2PDatahubMsgBase;

class ObRFBloomFilterMsg final : public ObP2PDatahubMsgBase
{
  OB_UNIS_VERSION_V(1);
public:
  ObRFBloomFilterMsg() : bloom_filter_() {}
  ~ObRFBloomFilterMsg() { destroy(); }
  virtual int assign(const ObP2PDatahubMsgBase &) final;
  virtual int merge(ObP2PDatahubMsgBase &) final;
  virtual int might_contain(const ObExpr &expr,
      ObEvalCtx &ctx,
      ObExprJoinFilter::ObExprJoinFilterContext &filter_ctx,
      ObDatum &res) override;
  virtual int might_contain_batch(
      const ObExpr &expr,
      ObEvalCtx &ctx,
      const ObBitVector &skip,
      const int64_t batch_size,
      ObExprJoinFilter::ObExprJoinFilterContext &filter_ctx) override;
  virtual int insert_by_row(
    const common::ObIArray<ObExpr *> &expr_array,
    const common::ObHashFuncs &hash_funcs,
    const ObExpr *calc_tablet_id_expr,
    ObEvalCtx &eval_ctx) override;
  virtual int insert_by_row_batch(
    const ObBatchRows *child_brs,
    const common::ObIArray<ObExpr *> &expr_array,
    const common::ObHashFuncs &hash_funcs,
    const ObExpr *calc_tablet_id_expr,
    ObEvalCtx &eval_ctx,
    uint64_t *batch_hash_values) override;
  virtual int reuse() override;
  virtual int deep_copy_msg(ObP2PDatahubMsgBase *&new_msg_ptr);
  virtual int destroy();
  inline void set_use_hash_join_seed(bool value) { use_hash_join_seed_ = value; }
  inline bool use_hash_join_seed() const { return use_hash_join_seed_; }
private:
  int calc_hash_value(
      const common::ObIArray<ObExpr *> &expr_array,
      const common::ObHashFuncs &hash_funcs,
      const ObExpr *calc_tablet_id_expr,
      ObEvalCtx &eval_ctx,
      uint64_t &hash_value, bool &ignore);
public:
  ObPxBloomFilter bloom_filter_;
  bool use_hash_join_seed_ {false};
};

class ObRFRangeFilterMsg : public ObP2PDatahubMsgBase
{
  OB_UNIS_VERSION_V(1);
public:
  struct MinMaxCellSize
  {
    OB_UNIS_VERSION_V(1);
  public:
    MinMaxCellSize() : min_datum_buf_size_(0), max_datum_buf_size_(0) {}
    virtual ~MinMaxCellSize() = default;
    // record the real datum buf for lower bound
    int64_t min_datum_buf_size_;
    // record the real datum buf for upper bound
    int64_t max_datum_buf_size_;
    TO_STRING_KV(K_(min_datum_buf_size), K_(max_datum_buf_size));
  };
public:
  ObRFRangeFilterMsg();
  virtual int assign(const ObP2PDatahubMsgBase &) final;
  virtual int merge(ObP2PDatahubMsgBase &) final;
  virtual int deep_copy_msg(ObP2PDatahubMsgBase *&new_msg_ptr);
  virtual int destroy() {
    lower_bounds_.reset();
    upper_bounds_.reset();
    cmp_funcs_.reset();
    need_null_cmp_flags_.reset();
    cells_size_.reset();
    query_range_info_.destroy();
    query_range_allocator_.reset();
    allocator_.reset();
    return OB_SUCCESS;
  }
  virtual int might_contain(const ObExpr &expr,
      ObEvalCtx &ctx,
      ObExprJoinFilter::ObExprJoinFilterContext &filter_ctx,
      ObDatum &res) override;
  virtual int might_contain_batch(
      const ObExpr &expr,
      ObEvalCtx &ctx,
      const ObBitVector &skip,
      const int64_t batch_size,
      ObExprJoinFilter::ObExprJoinFilterContext &filter_ctx) override;
  virtual int insert_by_row(
    const common::ObIArray<ObExpr *> &expr_array,
    const common::ObHashFuncs &hash_funcs,
    const ObExpr *calc_tablet_id_expr,
    ObEvalCtx &eval_ctx) override;
  virtual int insert_by_row_batch(
    const ObBatchRows *child_brs,
    const common::ObIArray<ObExpr *> &expr_array,
    const common::ObHashFuncs &hash_funcs,
    const ObExpr *calc_tablet_id_expr,
    ObEvalCtx &eval_ctx,
    uint64_t *batch_hash_values) override;
  virtual int reuse() override;
  int adjust_cell_size();
  void after_process() override;
  int try_extract_query_range(bool &has_extract, ObIArray<ObNewRange> &ranges,
                              bool need_deep_copy = false,
                              common::ObIAllocator *allocator = nullptr) override;
  inline int init_query_range_info(const ObPxQueryRangeInfo &query_range_info)
  {
    return query_range_info_.assign(query_range_info);
  }

  int prepare_storage_white_filter_data(ObDynamicFilterExecutor &dynamic_filter,
                                ObEvalCtx &eval_ctx,
                                ObRuntimeFilterParams &params,
                                bool &is_data_prepared) override;
private:
  int get_min(ObIArray<ObDatum> &vals,
              const common::ObDatumAccessContext *access_ctx);
  int get_max(ObIArray<ObDatum> &vals,
              const common::ObDatumAccessContext *access_ctx);
  int get_min(ObCmpFunc &func, ObDatum &l, ObDatum &r, int64_t &cell_size,
              const common::ObDatumAccessContext *access_ctx);
  int get_max(ObCmpFunc &func, ObDatum &l, ObDatum &r, int64_t &cell_size,
              const common::ObDatumAccessContext *access_ctx);
  int dynamic_copy_cell(const ObDatum &src, ObDatum &target, int64_t &cell_size);
  // only used in might_contain_batch,
  // without adding filter_count, total_count, check_count in filter_ctx
  int do_might_contain_batch(const ObExpr &expr,
      ObEvalCtx &ctx,
      const ObBitVector &skip,
      const int64_t batch_size,
      ObExprJoinFilter::ObExprJoinFilterContext &filter_ctx);
  int prepare_query_range();
  inline void reuse_query_range()
  {
    query_range_.reset();
    is_query_range_ready_ = false;
    
    query_range_allocator_.set_label("ObRangeMsgQR");
    query_range_allocator_.reset_remain_one_page();
  }

public:
  ObFixedArray<ObDatum, common::ObIAllocator> lower_bounds_;
  ObFixedArray<ObDatum, common::ObIAllocator> upper_bounds_;
  ObFixedArray<bool, common::ObIAllocator> need_null_cmp_flags_;
  ObFixedArray<MinMaxCellSize, common::ObIAllocator> cells_size_;
  ObCmpFuncs cmp_funcs_;
  // for extract query range
  ObPxQueryRangeInfo query_range_info_;
  ObNewRange query_range_; // not need to serialize
  bool is_query_range_ready_; // not need to serialize
  common::ObArenaAllocator query_range_allocator_;
  const common::ObDatumAccessContext *datum_access_ctx_; // request-scoped, not serialized
  // ---end---
  ObFixedArray<ObObjMeta, common::ObIAllocator> build_obj_metas_;
};

class ObRFInFilterMsg : public ObP2PDatahubMsgBase
{
  OB_UNIS_VERSION_V(1);
public:
  struct ObRFInFilterNode {
    ObRFInFilterNode()
        : cmp_funcs_(nullptr), hash_funcs_(nullptr), row_(nullptr),
          hash_val_(0), datum_access_ctx_(nullptr) {}
    ObRFInFilterNode(ObCmpFuncs *cmp_funcs, ObHashFuncs *hash_funcs,
          ObIArray<ObDatum> *row,
          const common::ObDatumAccessContext *datum_access_ctx,
          int64_t hash_val = 0)
        : cmp_funcs_(cmp_funcs), hash_funcs_(hash_funcs),
          row_(row), hash_val_(hash_val), datum_access_ctx_(datum_access_ctx) {}
    int hash(uint64_t &hash_ret) const;
    inline bool operator==(const ObRFInFilterNode &other) const;
    ObCmpFuncs *cmp_funcs_;
    ObHashFuncs *hash_funcs_;
    ObIArray<ObDatum> *row_;
    int64_t hash_val_;
    const common::ObDatumAccessContext *datum_access_ctx_;
  };
public:
  ObRFInFilterMsg() : ObP2PDatahubMsgBase(), rows_set_(),
      cmp_funcs_(allocator_), hash_funcs_for_insert_(allocator_),
      serial_rows_(), need_null_cmp_flags_(allocator_),
      cur_row_(allocator_), col_cnt_(0),
      max_in_num_(0), query_range_info_(allocator_),
      query_range_(), is_query_range_ready_(false), query_range_allocator_(),
      datum_access_ctx_(nullptr),
      build_obj_metas_(allocator_) {}
  virtual int assign(const ObP2PDatahubMsgBase &);
  virtual int merge(ObP2PDatahubMsgBase &) final;
  virtual int deep_copy_msg(ObP2PDatahubMsgBase *&new_msg_ptr);
  virtual int destroy();
  virtual int might_contain(const ObExpr &expr,
      ObEvalCtx &ctx,
      ObExprJoinFilter::ObExprJoinFilterContext &filter_ctx,
      ObDatum &res) override;
  virtual int might_contain_batch(
      const ObExpr &expr,
      ObEvalCtx &ctx,
      const ObBitVector &skip,
      const int64_t batch_size,
      ObExprJoinFilter::ObExprJoinFilterContext &filter_ctx) override;
  virtual int insert_by_row(
    const common::ObIArray<ObExpr *> &expr_array,
    const common::ObHashFuncs &hash_funcs,
    const ObExpr *calc_tablet_id_expr,
    ObEvalCtx &eval_ctx) override;
  virtual int insert_by_row_batch(
    const ObBatchRows *child_brs,
    const common::ObIArray<ObExpr *> &expr_array,
    const common::ObHashFuncs &hash_funcs,
    const ObExpr *calc_tablet_id_expr,
    ObEvalCtx &eval_ctx,
    uint64_t *batch_hash_values) override;
  virtual int reuse() override;
  void after_process() override;
  int try_extract_query_range(bool &has_extract, ObIArray<ObNewRange> &ranges,
                              bool need_deep_copy = false,
                              common::ObIAllocator *allocator = nullptr) override;
  inline int init_query_range_info(const ObPxQueryRangeInfo &query_range_info)
  {
    return query_range_info_.assign(query_range_info);
  }

  int prepare_storage_white_filter_data(ObDynamicFilterExecutor &dynamic_filter,
                                ObEvalCtx &eval_ctx,
                                ObRuntimeFilterParams &params,
                                bool &is_data_prepared) override;
private:
  int append_row();
  int insert_node();
  // only used in might_contain_batch,
  // without adding filter_count, total_count, check_count in filter_ctx
  int do_might_contain_batch(const ObExpr &expr,
      ObEvalCtx &ctx,
      const ObBitVector &skip,
      const int64_t batch_size,
      ObExprJoinFilter::ObExprJoinFilterContext &filter_ctx);
  int prepare_query_ranges();
  int process_query_ranges_with_deduplicate();
  int process_query_ranges_without_deduplicate();
  int generate_one_range(int row_idx);
  inline void reuse_query_range()
  {
    query_range_.reset();
    is_query_range_ready_ = false;
    
    query_range_allocator_.set_label("ObInMsgQR");
    query_range_allocator_.reset_remain_one_page();
  }

public:
  hash::ObHashSet<ObRFInFilterNode, hash::NoPthreadDefendMode> rows_set_;
  ObCmpFuncs cmp_funcs_;
  ObHashFuncs hash_funcs_for_insert_;
  ObSArray<ObFixedArray<ObDatum, common::ObIAllocator> *> serial_rows_;
  ObFixedArray<bool, common::ObIAllocator> need_null_cmp_flags_;
  ObFixedArray<ObDatum, common::ObIAllocator> cur_row_;
  int64_t col_cnt_;
  int64_t max_in_num_;
  // for extract query range
  ObPxQueryRangeInfo query_range_info_;
  ObSEArray<ObNewRange, 16> query_range_; // not need to serialize
  bool is_query_range_ready_; // not need to serialize
  common::ObArenaAllocator query_range_allocator_;
  const common::ObDatumAccessContext *datum_access_ctx_; // request-scoped, not serialized
  // ---end---
  ObFixedArray<ObObjMeta, common::ObIAllocator> build_obj_metas_;
};

}
}

#endif
