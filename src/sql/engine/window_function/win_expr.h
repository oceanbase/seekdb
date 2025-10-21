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

#ifndef OCEANBASE_WINDOW_FUNCTION_EXPR_H_
#define OCEANBASE_WINDOW_FUNCTION_EXPR_H_

#include "share/ob_define.h"
#include "share/aggregate/processor.h"
// This file contains many template functions that were originally defined in CPP, [because of the effect of UNITY merging compilation units, they compiled, but the implementation of template code needs to be defined in header files], therefore, turning off UNITY caused observer to fail compilation
// To solve the compilation problem after closing UNITY, move it to the header file
// But this function uses OZ, CK macros, these two macros internal log print used LOG_WARN, requirement must define USING_LOG_PREFIX
// Since this is a header file, this will lead to very tricky problems:
// 1. If USING_LOG_PREFIX is not defined before this header file, it must be redefined (but defining the macro in the header file will cause pollution)
// 2. If USING_LOG_PREFIX is newly defined in this file, it needs to be cleaned up to prevent pollution from spreading to other .h and cpp files
// Therefore here we check if USING_LOG_PREFIX is already defined, if it is defined then we abandon redefining it (this means logs are not always printed with the "SQL_ENG" identifier), and also define a special identifier
// If a special identifier is found, execute macro cleanup actions during preprocessing
// The entire logic is quite tricky, is to minimize code logic modification, code owner needs to refactor this logic later
#ifndef USING_LOG_PREFIX
#define MARK_MACRO_DEFINED_BY_WIN_EXPR_H
#define USING_LOG_PREFIX SQL_ENG
#endif
namespace oceanbase
{
namespace sql
{
class ObCompactRow;
class WinFuncColExpr;

namespace winfunc
{
using namespace share;
class RowStore;

// copy from` ob_aggregate_processor.h`
struct RemovalInfo
{
  RemovalInfo()
    : max_min_index_(-1),
      is_index_change_(false),
      is_inv_aggr_(false),
      null_cnt_(0),
      is_out_of_range_(false)
  {
  }
  ~RemovalInfo() {}
  void reset() {
    max_min_index_ = -1;
    is_index_change_ = false;
    is_inv_aggr_ = false;
    null_cnt_ = 0;
    is_out_of_range_ = false;
  }
  void max_min_update(const int64_t max_min_index) {
    if (is_index_change_) {
      max_min_index_ = max_min_index;
      is_index_change_ = false;
    }
  }
  TO_STRING_KV(K_(max_min_index), K_(is_index_change), K_(is_inv_aggr));
  int64_t max_min_index_; // extreme index position
  bool is_index_change_;  // whether the extreme value index position changes
  bool is_inv_aggr_;      // whether the aggregate function support single line inverse
  int64_t null_cnt_;      // count of null in frame for calculating sum
  bool is_out_of_range_;  // whether out of range when calculateing
};

// copy from `ObWindowFunctionOp::Frame`
struct Frame
{
  Frame(const int64_t head = -1, const int64_t tail = -1, bool is_accum_frame = false) :
    head_(head), tail_(tail), is_accum_frame_(is_accum_frame), skip_cnt_(0)
  {}
  Frame(const Frame &other) :
    head_(other.head_), tail_(other.tail_), is_accum_frame_(other.is_accum_frame_),
    skip_cnt_(other.skip_cnt_)
  {}
  bool operator==(const Frame &other) const
  {
    return same_frame(*this, other);
  }
  static bool valid_frame(const Frame &part_frame, const Frame &frame)
  {
    return frame.head_ < frame.tail_ && frame.head_ < part_frame.tail_
           && frame.tail_ > part_frame.head_;
  }
  static bool same_frame(const Frame &left, const Frame &right)
  {
    return left.head_ == right.head_ && left.tail_ == right.tail_;
  }
  static void prune_frame(const Frame &part_frame, Frame &frame)
  {
    // it's caller's responsibility for invoking valid_frame() first
    if (frame.head_ < part_frame.head_) { frame.head_ = part_frame.head_; }
    if (frame.tail_ > part_frame.tail_) { frame.tail_ = part_frame.tail_; }
  }
  static bool need_restart_aggr(const bool can_inv, const Frame &last_valid_frame,
                                const Frame &new_frame, const aggregate::RemovalInfo &removal_info,
                                const uint64_t &remove_type)
  {
    bool need = false;
    if (-1 == last_valid_frame.head_ || -1 == last_valid_frame.tail_) {
      need = true;
    } else {
      const int64_t inc_cost = std::abs(last_valid_frame.head_ - new_frame.head_)
                               + std::abs(last_valid_frame.tail_ - new_frame.tail_);
      const int64_t restart_cost = new_frame.tail_ - new_frame.head_;
      if (inc_cost > restart_cost) {
        need = true;
      } else if (!can_inv) {
        // has sliding-out row
        if (new_frame.head_ > last_valid_frame.head_ || new_frame.tail_ < last_valid_frame.tail_) {
          need = true;
        }
      } else if (common::REMOVE_EXTRENUM == remove_type) {
        // max_min index miss from calculation range
        if (removal_info.max_min_index_ < new_frame.head_
            || removal_info.max_min_index_ > new_frame.tail_) {
          need = true;
        }
      }
    }
    return need;
  }
  bool is_valid() const
  {
    return head_ < tail_
           && head_ != -1 && head_ != INT64_MAX
           && tail_ != -1 && tail_ != INT64_MAX;
  }
  bool is_empty() const
  {
    return tail_ <= head_;
  }
  void reset()
  {
    head_ = tail_ = -1;
    skip_cnt_ = 0;
  }
  TO_STRING_KV(K(head_), K(tail_), K(is_accum_frame_), K(skip_cnt_));

  int64_t head_;
  int64_t tail_; // !!! not included
  bool is_accum_frame_;
  int64_t skip_cnt_; // skipped rows in this frame
};

struct WinExprEvalCtx
{
  WinExprEvalCtx(RowStore &input_rows, WinFuncColExpr &win_col, const int64_t tenant_id) :
    input_rows_(input_rows), win_col_(win_col),
    allocator_(ObModIds::OB_SQL_WINDOW_LOCAL, OB_MALLOC_NORMAL_BLOCK_SIZE, tenant_id,
               ObCtxIds::WORK_AREA),
    extra_(nullptr)
  {}

  char *reserved_buf(int32_t len)
  {
    return (char *)allocator_.alloc(len);
  }
  ~WinExprEvalCtx()
  {
    extra_ = nullptr;
    allocator_.reset();
  }
  RowStore &input_rows_;
  sql::WinFuncColExpr &win_col_;
  // used for tmp memory allocating during partition process.
  common::ObArenaAllocator allocator_;
  void *extra_; // maybe useless
};

class IWinExpr
{
public:
  virtual int process_window(WinExprEvalCtx &ctx, const Frame &frame, const int64_t row_idx,
                             char *res, bool &is_null) = 0;
  virtual int collect_part_results(WinExprEvalCtx &ctx, const int64_t row_start,
                                   const int64_t row_end, const ObBitVector &skip) = 0;
  virtual int accum_process_window(WinExprEvalCtx &ctx, const Frame &cur_frame,
                                   const Frame &prev_frame, const int64_t row_idx, char *res,
                                   bool &is_null) = 0;
  virtual int process_partition(WinExprEvalCtx &ctx, const int64_t part_start,
                                const int64_t part_end, const int64_t row_start,
                                const int64_t row_end, const ObBitVector &skip) = 0;
  // used to generate extra ctx for expr evaluation
  virtual int generate_extra(ObIAllocator &allocator, void *&extra) = 0;

  virtual bool is_aggregate_expr() const = 0;
  virtual void destroy() = 0;
};


template<typename Derived>
class WinExprWrapper: public IWinExpr
{
public:
  virtual int process_partition(WinExprEvalCtx &ctx, const int64_t part_start,
                                const int64_t part_end, const int64_t row_start,
                                const int64_t row_end, const ObBitVector &skip) override;
  virtual int generate_extra(ObIAllocator &allocator, void *&extra) override
  {
    return OB_NOT_IMPLEMENT;
  }
  virtual void destroy() override
  { // do nothing
    return;
  }
protected:
  int copy_aggr_row(WinExprEvalCtx &ctx, const char *src_row, char *dst_row);
private:
  int update_frame(WinExprEvalCtx &ctx, const Frame &prev_frame, Frame &new_frame,
                   const int64_t idx, const int64_t row_start, bool &whole_frame,
                   bool &valid_frame);
};

// TODO: adjust inheritance
class NonAggrWinExpr: public WinExprWrapper<NonAggrWinExpr>
{
protected:
  struct ParamStatus
  {
    ParamStatus() : flags_(0), int_val_(0)
    {}
    union
    {
      struct
      {
        uint32_t calculated_ : 1;
        uint32_t is_null_ : 1;
        uint32_t reserved_ : 30;
      };
      uint32_t flags_;
    };
    int64_t int_val_;
  };
  int eval_param_int_value(ObExpr *param, ObEvalCtx &ctx, const bool need_check_valid,
                           const bool need_nmb, ParamStatus &status);

public:
  virtual int accum_process_window(WinExprEvalCtx &ctx, const Frame &cur_frame,
                                   const Frame &prev_frame, const int64_t row_idx,
                                   char *res, bool &is_null) override final
  {
    int ret = OB_NOT_IMPLEMENT;
    return ret;
  }
  virtual bool is_aggregate_expr() const override final { return false; }

  virtual int collect_part_results(WinExprEvalCtx &ctx, const int64_t row_start,
                                   const int64_t row_end, const ObBitVector &skip) override final;
};

template<ObItemType rank_op>
class RankLikeExpr final: public NonAggrWinExpr
{
public:
  RankLikeExpr():NonAggrWinExpr(), rank_of_prev_row_(0) {}

  virtual int process_window(WinExprEvalCtx &ctx, const Frame &frame, const int64_t row_idx,
                   char *res, bool &is_null) override;
  virtual int generate_extra(ObIAllocator &allocator, void *&extra) override;
private:
  int64_t rank_of_prev_row_;
};

class RowNumber final: public NonAggrWinExpr
{
public:
  virtual int process_window(WinExprEvalCtx &ctx, const Frame &frame, const int64_t row_idx,
                             char *res, bool &is_null) override;
  virtual int generate_extra(ObIAllocator &allocator, void *&extra) override;
};

class Ntile final: public NonAggrWinExpr
{
public:
  virtual int process_window(WinExprEvalCtx &ctx, const Frame &frame, const int64_t row_idx,
                             char *res, bool &is_null) override;
  virtual int generate_extra(ObIAllocator &allocator, void *&extra) override;
};

class NthValue final: public NonAggrWinExpr
{
public:
  virtual int process_window(WinExprEvalCtx &ctx, const Frame &frame, const int64_t row_idx,
                             char *res, bool &is_null) override;
  virtual int generate_extra(ObIAllocator &allocator, void *&extra) override;
};

class LeadOrLag final: public NonAggrWinExpr
{
public:
  virtual int process_window(WinExprEvalCtx &ctx, const Frame &frame, const int64_t row_idx,
                             char *res, bool &is_null) override;
  virtual int generate_extra(ObIAllocator &allocator, void *&extra) override;
};

class CumeDist final: public NonAggrWinExpr
{
public:
  virtual int process_window(WinExprEvalCtx &ctx, const Frame &frame, const int64_t row_idx,
                             char *res, bool &is_null) override;
  virtual int generate_extra(ObIAllocator &allocator, void *&extra) override;
};

class AggrExpr final: public WinExprWrapper<AggrExpr>
{
public:
  AggrExpr(): aggr_processor_(nullptr), last_valid_frame_(), last_aggr_row_(nullptr) {}
  int process_window(WinExprEvalCtx &ctx, const Frame &frame, const int64_t row_idx,
                     char *res, bool &is_null) override;

  int accum_process_window(WinExprEvalCtx &ctx, const Frame &cur_frame, const Frame &prev_frame,
                           const int64_t row_idx, char *res, bool &is_null) override;
  bool is_aggregate_expr() const override { return true; }
  virtual int collect_part_results(WinExprEvalCtx &ctx, const int64_t row_start,
                                   const int64_t row_end, const ObBitVector &skip) override;
  virtual int generate_extra(ObIAllocator &allocator, void *&extra) override
  {
    return OB_SUCCESS;
  }

  static int set_result_for_invalid_frame(WinExprEvalCtx &ctx, char *agg_row);

  virtual void destroy() override;

private:
  int calc_pushdown_skips(WinExprEvalCtx &ctx, const int64_t batch_size, sql::ObBitVector &skip, bool &all_active);

  template <typename ColumnFmt>
  int set_payload(WinExprEvalCtx &ctx, ColumnFmt *columns, const int64_t idx,
                  const char *payload, int32_t len);

public:
  aggregate::Processor *aggr_processor_;
  Frame last_valid_frame_;
  aggregate::RemovalInfo last_removal_info_;
  char *last_aggr_row_;
};

int cmp_prev_row(WinExprEvalCtx &ctx, const int64_t cur_idx, int &cmp_ret);

ObObjType RankLikeExpr_process_window_helper(WinExprEvalCtx &ctx);
template <ObItemType rank_op>
int RankLikeExpr<rank_op>::process_window(WinExprEvalCtx &ctx, const Frame &frame,
                                          const int64_t row_idx, char *res, bool &is_null)
{
  int ret = OB_SUCCESS;
  bool equal_with_prev_row = false;
  is_null = false;
  if (row_idx != frame.head_) {
    int cmp_ret = 0;
    if (OB_FAIL(cmp_prev_row(ctx, row_idx, cmp_ret))) {
      LOG_WARN("compare previous row failed", K(ret));
    } else {
      equal_with_prev_row = (cmp_ret == 0);
    }
  } else {
    // reset rank
    rank_of_prev_row_ = 0;
  }
  if (OB_SUCC(ret)) {
    int64_t rank = -1;
    if (equal_with_prev_row) {
      rank = rank_of_prev_row_;
    } else if (rank_op == T_WIN_FUN_RANK || rank_op == T_WIN_FUN_PERCENT_RANK) {
      rank = row_idx - frame.head_ + 1;
    } else if (rank_op == T_WIN_FUN_DENSE_RANK) {
      rank = rank_of_prev_row_ + 1;
    }
    LOG_DEBUG("calculate rank result", K(rank_op), K(rank), K(frame));
    if (rank_op == T_WIN_FUN_PERCENT_RANK) {
      // if (ob_is_number_tc(ctx.win_col_.wf_info_.expr_->datum_meta_.type_)) {
      if (ob_is_number_tc(RankLikeExpr_process_window_helper(ctx))) {
        // in mysql mode, percent rank may return double
        if (0 == frame.tail_ - frame.head_ - 1) {
          number::ObNumber zero_nmb;
          zero_nmb.set_zero();
          MEMCPY(res, &(zero_nmb.d_), sizeof(ObNumberDesc));
        } else {
          number::ObNumber numerator;
          number::ObNumber denominator;
          number::ObNumber res_nmb;
          ObNumStackAllocator<3> tmp_alloc;
          if (OB_FAIL(numerator.from(rank - 1, tmp_alloc))) {
            LOG_WARN("failed to build number from int64_t", K(ret));
          } else if (OB_FAIL(denominator.from(frame.tail_ - frame.head_ - 1, tmp_alloc))) {
            LOG_WARN("failed to build number from int64_t", K(ret));
          } else if (OB_FAIL(numerator.div(denominator, res_nmb, tmp_alloc))) {
            LOG_WARN("failed to div number", K(ret));
          } else {
            number::ObCompactNumber *res_cnum = reinterpret_cast<number::ObCompactNumber *>(res);
            res_cnum->desc_ = res_nmb.d_;
            MEMCPY(&(res_cnum->digits_[0]), res_nmb.get_digits(), sizeof(uint32_t) * res_nmb.d_.len_);
          }
        }
      } else if (ObDoubleType == RankLikeExpr_process_window_helper(ctx)) {
        if (0 == frame.tail_ - frame.head_ - 1) {
          *reinterpret_cast<double *>(res) = 0;
        } else {
          double numerator = static_cast<double>(rank - 1);
          double denominator= static_cast<double>(frame.tail_ - frame.head_ - 1);
          *reinterpret_cast<double *>(res) = (numerator / denominator);
        }
      }
    } else {
      *reinterpret_cast<int64_t *>(res) = rank;
    }
    if (OB_SUCC(ret)) {
      rank_of_prev_row_ = rank;
    }
  }
  return ret;
}

template<ObItemType rank_op>
int RankLikeExpr<rank_op>::generate_extra(ObIAllocator &allocator, void *&extra)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(extra = allocator.alloc(sizeof(int64_t)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate memory failed", K(ret));
  } else {
    *reinterpret_cast<int64_t *>(extra) = 0;
  }
  return ret;
}

} // end winfunc
} // end sql
} // end oceanbase
#ifdef MARK_MACRO_DEFINED_BY_WIN_EXPR_H
#undef USING_LOG_PREFIX
#endif
#endif // OCEANBASE_WINDOW_FUNCTION_EXPR_H_
