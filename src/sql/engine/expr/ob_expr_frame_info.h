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

#ifndef OCEANBASE_SQL_ENGINE_OB_FRAME_INFO_
#define OCEANBASE_SQL_ENGINE_OB_FRAME_INFO_
#include "lib/utility/ob_print_utils.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/container/ob_fixed_array.h"
#include "sql/engine/expr/ob_expr.h"
#include "common/row/ob_row.h"
#include "common/datum/datum_value.h"
#include "common/datum/datum_parameter_metadata.h"

namespace oceanbase
{
namespace sql
{
class ObPhysicalPlanCtx;
struct NativeParameterGroup;
class ObDynExtReuseAlloc;
class ObExprOperatorCtx;

struct ObFrameInfo
{
  OB_UNIS_VERSION_V(1);
public:
  ObFrameInfo() : expr_cnt_(0),
                  frame_idx_(0),
                  frame_size_(0),
                  zero_init_pos_(0),
                  zero_init_size_(0)
  {}
  virtual ~ObFrameInfo() = default;

  ObFrameInfo(uint64_t expr_cnt, uint32_t frame_idx, uint64_t frame_size,
              uint32_t zero_init_pos, uint32_t zero_init_size)
    : expr_cnt_(expr_cnt),
      frame_idx_(frame_idx),
      frame_size_(frame_size),
      zero_init_pos_(zero_init_pos),
      zero_init_size_(zero_init_size)
  {}

  TO_STRING_KV(K_(expr_cnt), K_(frame_idx), K_(frame_size), K_(zero_init_pos),
               K_(zero_init_size));

public:
  uint64_t expr_cnt_;       // current number of expr in the frame
  uint32_t frame_idx_;      // current frame index among all frames
  uint64_t frame_size_;     // current frame size
  uint32_t zero_init_pos_;
  uint32_t zero_init_size_;
};

struct NativeI64PointLayout
{
  static constexpr int64_t KEY_COUNT = 3;
  static constexpr int64_t LANE_COUNT = 6;
  struct Frame
  {
    uint32_t index;
    uint32_t generic_size;
    uint32_t compact_size;
  };
  int64_t parameter_indexes[KEY_COUNT];
  uint32_t expr_indexes[LANE_COUNT];
  NativeI64ExprLayout lanes[LANE_COUNT];
  Frame frames[LANE_COUNT];
  uint32_t frame_count;
};
static_assert(sizeof(NativeI64PointLayout) == 272, "complete local point layout metadata");

struct ObExprFrameInfo
{
  OB_UNIS_VERSION_V(1);
public:
  static const int64_t EXPR_CNT_PER_FRAME =
      ScalarFrameLayout::slots_per_frame();

  ObExprFrameInfo(common::ObIAllocator &allocator) :
      _rt_exprs_(0, common::ModulePageAllocator(allocator)),
      need_ctx_cnt_(0),
      rt_exprs_(_rt_exprs_),
      const_frame_ptrs_(allocator),
      const_frame_(allocator),
      param_frame_(allocator),
      dynamic_frame_(allocator),
      datum_frame_(allocator),
      ser_expr_marks_(allocator),
      native_i64_point_layout_(nullptr),
      native_i64_layout_validated_(false),
      allocator_(allocator)
  {
  }

  ObExprFrameInfo(common::ObIAllocator &allocator, common::ObArray<ObExpr> &rt_exprs) :
      _rt_exprs_(0, common::ModulePageAllocator(allocator)),
      need_ctx_cnt_(0),
      rt_exprs_(rt_exprs),
      const_frame_ptrs_(allocator),
      const_frame_(allocator),
      param_frame_(allocator),
      dynamic_frame_(allocator),
      datum_frame_(allocator),
      ser_expr_marks_(allocator),
      native_i64_point_layout_(nullptr),
      native_i64_layout_validated_(false),
      allocator_(allocator)
  {
  }

  virtual int assign(const ObExprFrameInfo &other, common::ObIAllocator &allocator);
  // Pre-allocate memory needed during execution, including frame memory and expr_ctx memory
  // @param exec_ctx execution context, initialize its members: frames_, frame_cnt_, expr_ctx_arr_
  int pre_alloc_exec_memory(ObExecContext &exec_ctx, ObIAllocator *allocator = NULL) const;
  // Allocate frame memory, and store all frame pointers in the frames array in order of each frame idx
  // @param [in] exec_allocator execution allocator
  // @param [out] frame_cnt the number of all frames
  // @param [out] frames list of all frame pointers
  int alloc_frame(common::ObIAllocator &exec_allocator,
                  const ObIArray<char *> &param_frame_ptrs,
                  uint64_t &frame_cnt,
                  char **&frames, bool compact_i64 = false) const;

  int choose_native_i64_layout(const ObExecContext &exec_ctx, bool &compact) const;

  // Native I64 layout pointers, the frame directory, and parameter aliases are
  // immutable plan metadata. Validate them when a plan publishes or clones
  // that metadata; execution only checks the mark and runtime parameter pages.
  int validate_native_i64_layout() const;
  int validate_and_mark_native_i64_layout();

  int get_expr_idx_in_frame(ObExpr *expr, int64_t &expr_idx) const;

  bool is_mark_serialize() const { return &_rt_exprs_ != &rt_exprs_; }

  TO_STRING_KV(K_(need_ctx_cnt),
               "rt_expr_cnt", rt_exprs_.count(),
               K_(const_frame_ptrs),
               K_(const_frame),
               K_(param_frame),
               K_(dynamic_frame),
               K_(datum_frame),
               K_(ser_expr_marks),
               K_(native_i64_layout_validated));


private:
  // Only entered after choose_native_i64_layout() has checked the immutable
  // plan mark, or alloc_frame() has defensively validated an external caller.
  // Execution mode selection and frame initialization remain per execution.
  int alloc_frame_after_layout_validation(common::ObIAllocator &exec_allocator,
                  const ObIArray<char *> &param_frame_ptrs,
                  uint64_t &frame_cnt, char **&frames, bool compact_i64) const;
  common::ObArray<ObExpr> _rt_exprs_;

public:
  // All physical expressions, the number of expression-level contexts needed, exec_ctx allocates specific memory size usage
  int64_t need_ctx_cnt_;
  // Expression cg generates all physical expressions
  common::ObArray<ObExpr> &rt_exprs_;
  // All const frame pointers
  common::ObFixedArray<char *, common::ObIAllocator> const_frame_ptrs_;
  // All const frame information
  common::ObFixedArray<ObFrameInfo, common::ObIAllocator> const_frame_;
  // All param frame information
  common::ObFixedArray<ObFrameInfo, common::ObIAllocator> param_frame_;
  // All dynamic frame information
  common::ObFixedArray<ObFrameInfo, common::ObIAllocator> dynamic_frame_;
  // All datum frame information
  common::ObFixedArray<ObFrameInfo, common::ObIAllocator> datum_frame_;

  // mark need serialize exrps in mark serialization
  common::ObFixedArray<bool, common::ObIAllocator> ser_expr_marks_;
  // Immutable plan metadata only; no alternate execution pages are retained.
  const NativeI64PointLayout *native_i64_point_layout_;
  // Set only after the complete layout has passed validation at a plan
  // construction or clone boundary. A null layout does not require the mark.
  bool native_i64_layout_validated_;
  common::ObIAllocator &allocator_;
};

// Concrete Query frame protocol. Runtime frames contain native values, but
// their memory representation is never a wire format. Parameter pages are
// decoded separately because partial PX plans omit unreferenced parameter
// expressions and may shrink their frame metadata.
class NativeFrameCodec
{
public:
  static int group_size(bool plan_part, const ObIArray<ObFrameInfo> &group,
      const ObExprFrameInfo &expressions, char *const *frames, int64_t frame_count,
      bool no_data, int64_t &size);
  static int serialize_group(char *buf, int64_t length, int64_t &pos, bool plan_part,
      const ObIArray<ObFrameInfo> &group, const ObExprFrameInfo &expressions,
      char *const *frames, int64_t frame_count, bool no_data);
  static int deserialize_group(const char *buf, int64_t length, int64_t &pos,
      ObIAllocator &owner, bool plan_part, const ObIArray<ObFrameInfo> &group,
      const ObExprFrameInfo &expressions, ObIArray<char *> *pointers,
      char **frames, int64_t frame_count, bool no_data);

  static int metadata_size(const ObIArray<ObFrameInfo> &group, int64_t &size);
  static int serialize_metadata(const ObIArray<ObFrameInfo> &group,
      char *buf, int64_t length, int64_t &pos);
  static int deserialize_metadata(const char *buf, int64_t length, int64_t &pos,
      ObIArray<ObFrameInfo> &group, int64_t frame_count);
  static int parameter_size(const ObPhysicalPlanCtx &plan, int64_t &size);
  static int serialize_parameters(const ObPhysicalPlanCtx &plan,
      char *buf, int64_t length, int64_t &pos);
  static int deserialize_parameters(const char *buf, int64_t length, int64_t &pos,
      ObIAllocator &owner, ObIArray<char *> &pages,
      ObIArray<const common::SqlType *> &types, int64_t &original_count,
      ObIArray<common::DatumParameterMetadata> *metadata = nullptr,
      ObIArray<NativeParameterGroup> *groups = nullptr, int32_t *wire_version = nullptr);
  static int bind_parameter_pages(const ObIArray<ObFrameInfo> &group,
      const ObIArray<char *> &pages, int64_t parameter_count, int64_t original_count,
      char **frames, int64_t frame_count, const ObExprFrameInfo *expressions = nullptr, bool restore_private_aliases = false);
  // Uses actual per-Expr offsets for native arrays/EvalInfo, including batch
  // frames whose wire deliberately contains no execution values.
  static int initialize_frame(const ObFrameInfo &info, const ObExprFrameInfo &expressions,
      char *frame);
  static int clone_constant_frames(const ObExprFrameInfo &source,
      ObIAllocator &owner, ObIArray<char *> &destination);
};

// Empty body for unused expressions in marked PX arrays. Its envelope version
// must match ObExpr because receivers decode every array position as ObExpr.
struct ObEmptyExpr
{
  OB_UNIS_VERSION(2);
public:
  static const ObEmptyExpr &instance()
  {
    static const ObEmptyExpr expr_;
    return expr_;
  }
};

struct ObPreCalcExprFrameInfo
  : public ObExprFrameInfo, public common::ObDLinkBase<ObPreCalcExprFrameInfo>
{
  ObPreCalcExprFrameInfo(common::ObIAllocator &allocator):
    ObExprFrameInfo(allocator),
    pre_calc_rt_exprs_(allocator)
  {
  }

  int assign(const ObPreCalcExprFrameInfo &other, common::ObIAllocator &allocator);
  int eval(ObExecContext &exec_ctx, common::ObIArray<DatumParameter> &res_datum_params);
  int eval_expect_err(ObExecContext &exec_ctx, bool &all_eval_err);
  INHERIT_TO_STRING_KV("frame_info", ObExprFrameInfo, K_(pre_calc_rt_exprs));
private:
  int do_normal_eval(ObExecContext &exec_ctx, common::ObIArray<DatumParameter> &res_datum_params);
  int do_batch_stmt_eval(ObExecContext &exec_ctx, common::ObIArray<DatumParameter> &res_datum_params);
  void clear_datum_evaluted_flags(char **frames);
public:
  common::ObFixedArray<ObExpr *, common::ObIAllocator> pre_calc_rt_exprs_;
};

struct ObTempExprCtx : public ObEvalCtx
{
  ObTempExprCtx(ObExecContext &exec_ctx)
    : ObEvalCtx(exec_ctx), frame_cnt_(0), expr_op_ctx_store_(NULL), expr_op_size_(0)
  {}
  ~ObTempExprCtx();
public:
  uint64_t frame_cnt_;
  ObExprOperatorCtx **expr_op_ctx_store_;
  uint64_t expr_op_size_;
};

struct RowIdxColumnPair
{
  OB_UNIS_VERSION_V(1);
public:
  RowIdxColumnPair()
    : idx_(OB_INVALID_INDEX), expr_pos_(OB_INVALID_INDEX)
  {}
  RowIdxColumnPair(int64_t idx, int64_t expr_pos)
    : idx_(idx), expr_pos_(expr_pos)
  {}
  virtual ~RowIdxColumnPair() {}
  TO_STRING_KV(K(idx_), KP(expr_pos_));

  int64_t idx_;  // obj idx in row
  int64_t expr_pos_; // column expr pos in rt_exprs_
};

struct ObTempExpr: public ObExprFrameInfo
{
  OB_UNIS_VERSION_V(1);
public:
  ObTempExpr(ObIAllocator &allocator)
    : ObExprFrameInfo(allocator),
      expr_idx_(OB_INVALID_INDEX),
      idx_col_arr_(allocator)
  {
  }
  // Input-free range/DDL expressions return an allocator-owned Datum. Copy
  // before releasing the temporary context, also when its cache is disabled.
  int eval_datum(ObExecContext &exec_ctx, common::ObIAllocator &result_allocator,
                  common::Datum &result) const;
  int eval_datum(ObExecContext &exec_ctx, common::ObIAllocator &result_allocator,
                  common::ObDatum &result) const;
  // Borrow input values for this evaluation; copy the result into the caller
  // owner before releasing a noncached temporary expression context.
  int eval_datum(ObExecContext &exec_ctx, const common::ObIArray<common::DatumValue> &row,
                  common::ObIAllocator &result_allocator, common::Datum &result) const;
  int eval_datum(ObExecContext &exec_ctx, const common::ObIArray<common::DatumValue> &row,
                  common::ObIAllocator &result_allocator, common::ObDatum &result) const;
  int deep_copy(ObIAllocator &allocator, ObTempExpr *&dst) const;
  INHERIT_TO_STRING_KV("temp expr", ObExprFrameInfo, K_(expr_idx), K(idx_col_arr_));

private:
  int row_to_frame(const common::ObIArrayWrap<common::DatumValue> &row,
                    ObTempExprCtx &temp_expr_ctx) const;

public:
  int64_t expr_idx_; // temp_expr idx in rt_exprs_
  ObFixedArray<RowIdxColumnPair, common::ObIAllocator> idx_col_arr_;
};

} // end namespace sql
} // end namespace oceanbase
#endif /* OCEANBASE_SQL_ENGINE_OB_FRAME_INFO_*/
