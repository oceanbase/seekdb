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

#define USING_LOG_PREFIX SQL_ENG

#include "ob_expr.h"
#include "common/datum/datum_sql_print.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/expr/ob_datum_cast.h"
#include "sql/engine/expr/ob_array_expr_utils.h"
#include "sql/engine/expr/ob_expr_column_conv.h"
#include "sql/engine/expr/ob_obj_cast_runtime.h"
#include "share/datum/datum_value_cast.h"

namespace oceanbase
{
using namespace common;
namespace sql
{
// index number for args_ in ObExpr
const int64_t ARGS_IDX_ZERO  = 0;
const int64_t ARGS_IDX_ONE   = 1;
const int64_t ARGS_IDX_TWO   = 2;
const int64_t ARGS_IDX_THREE = 3;
const int64_t ARGS_IDX_FOUE  = 4;
const int64_t ARGS_IDX_FIVE  = 5;

// ObPrecision and ObLengthSemantics are union field in ObDatumMeta,
// need to be same size to make sure serialization works.
STATIC_ASSERT(sizeof(ObPrecision) == sizeof(ObLengthSemantics),
              "size of ObPrecision and ObLengthSemantics mismatch");

OB_SERIALIZE_MEMBER(ObDatumMeta, type_, cs_type_, scale_, precision_);


ObEvalCtx::ObEvalCtx(ObExecContext &exec_ctx, ObIAllocator *allocator)
  : frames_(exec_ctx.get_frames()),
    max_batch_size_(0),
    exec_ctx_(exec_ctx),
    tmp_alloc_(exec_ctx.get_eval_tmp_allocator()),
    datum_caster_(NULL),
    tmp_alloc_used_(exec_ctx.get_tmp_alloc_used()),
    batch_idx_(0),
    batch_size_(0),
    expr_res_alloc_((dynamic_cast<ObArenaAllocator*>(allocator) != NULL) ?
                      (*(dynamic_cast<ObArenaAllocator*>(allocator))) : exec_ctx.get_eval_res_allocator())
{
}

ObEvalCtx::ObEvalCtx(ObEvalCtx &eval_ctx)
  : frames_(eval_ctx.frames_),
    max_batch_size_(eval_ctx.max_batch_size_),
    exec_ctx_(eval_ctx.exec_ctx_),
    tmp_alloc_(eval_ctx.tmp_alloc_),
    datum_caster_(NULL),
    tmp_alloc_used_(eval_ctx.tmp_alloc_used_),
    batch_idx_(eval_ctx.get_batch_idx()),
    batch_size_(eval_ctx.get_batch_size()),
    expr_res_alloc_(eval_ctx.expr_res_alloc_)
{
}

ObEvalCtx::~ObEvalCtx()
{
  if (NULL != datum_caster_) {
    datum_caster_->~ObDatumCaster();
    exec_ctx_.get_allocator().free(datum_caster_);
    datum_caster_ = NULL;
  }
}

int ObEvalCtx::init_datum_caster()
{
  int ret = OB_SUCCESS;
  if (NULL == datum_caster_) {
    ObDatumCaster *datum_caster = NULL;
    void *buf = NULL;
    if (OB_ISNULL(buf = exec_ctx_.get_allocator().alloc(sizeof(ObDatumCaster)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (FALSE_IT(datum_caster = new(buf) ObDatumCaster())) {
    } else if (OB_FAIL(datum_caster->init(exec_ctx_))) {
      datum_caster->~ObDatumCaster();
      exec_ctx_.get_allocator().free(datum_caster);
    } else {
      datum_caster_ = datum_caster;
    }
  }
  return ret;
}

int ObEvalCtx::get_datum_access_ctx(
    const common::ObDatumAccessContext *&datum_access_ctx)
{
  return exec_ctx_.get_datum_access_ctx(datum_access_ctx);
}

DEF_TO_STRING(ObEvalInfo)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(evaluated), K_(projected), K_(notnull), K_(point_to_frame), K_(cnt));
  J_OBJ_END();
  return pos;
}

OB_DEF_SERIALIZE(ObExpr)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE,
              type_,
              datum_meta_,
              obj_meta_,
              max_length_,
              obj_datum_map_,
              ser_eval_func_,
              serialization::make_ser_carray(ser_inner_functions_, inner_func_cnt_),
              serialization::make_ser_carray(args_, arg_cnt_),
              serialization::make_ser_carray(parents_, parent_cnt_),
              frame_idx_,
              datum_off_,
              res_buf_off_,
              res_buf_len_,
              expr_ctx_id_,
              extra_);

  LST_DO_CODE(OB_UNIS_ENCODE,
              eval_info_off_,
              flag_,
              ser_eval_batch_func_,
              eval_flags_off_,
              pvt_skip_off_);

  if (OB_SUCC(ret)) {
    ObExprOperatorType type = T_INVALID;
    if (nullptr != extra_info_) {
      type = extra_info_->type_;
    }
    // Add a type before extra_info to determine whether extra_info is empty
    OB_UNIS_ENCODE(type)
    if (OB_FAIL(ret)) {
    } else if (T_INVALID != type) {
      OB_UNIS_ENCODE(*extra_info_);
    }
    OB_UNIS_ENCODE(dyn_buf_header_offset_)
    OB_UNIS_ENCODE(local_session_var_id_);
  }

  return ret;
}

OB_DEF_DESERIALIZE(ObExpr)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE,
              type_,
              datum_meta_,
              obj_meta_,
              max_length_,
              obj_datum_map_,
              ser_eval_func_,
              serialization::make_ser_carray(ser_inner_functions_, inner_func_cnt_),
              serialization::make_ser_carray(args_, arg_cnt_),
              serialization::make_ser_carray(parents_, parent_cnt_),
              frame_idx_,
              datum_off_,
              res_buf_off_,
              res_buf_len_,
              expr_ctx_id_,
              extra_);

  LST_DO_CODE(OB_UNIS_DECODE,
              eval_info_off_,
              flag_,
              ser_eval_batch_func_,
              eval_flags_off_,
              pvt_skip_off_);
  if (OB_SUCC(ret)) {
    ObExprOperatorType type = T_INVALID;
    // Add a type before extra_info to determine whether extra_info is empty
    OB_UNIS_DECODE(type);
    if (OB_FAIL(ret)) {
    } else if (T_INVALID != type) {
      OZ (ObExprExtraInfoFactory::alloc(CURRENT_CONTEXT->get_arena_allocator(),
                                        type, extra_info_));
      CK (OB_NOT_NULL(extra_info_));
      OB_UNIS_DECODE(*extra_info_);
    }
  }

  if (OB_SUCC(ret)) {
    basic_funcs_ = ObDatumFuncs::get_basic_func(datum_meta_.type_, datum_meta_.cs_type_,
                                                datum_meta_.scale_,
                                                obj_meta_.has_lob_header(), datum_meta_.precision_);
    CK(NULL != basic_funcs_);
  }
  if (is_batch_result()) {
    batch_idx_mask_ = UINT64_MAX;
  }
  OB_UNIS_DECODE(dyn_buf_header_offset_);
  OB_UNIS_DECODE(local_session_var_id_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObExpr)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              type_,
              datum_meta_,
              obj_meta_,
              max_length_,
              obj_datum_map_,
              ser_eval_func_,
              serialization::make_ser_carray(ser_inner_functions_, inner_func_cnt_),
              serialization::make_ser_carray(args_, arg_cnt_),
              serialization::make_ser_carray(parents_, parent_cnt_),
              frame_idx_,
              datum_off_,
              res_buf_off_,
              res_buf_len_,
              expr_ctx_id_,
              extra_);

  LST_DO_CODE(OB_UNIS_ADD_LEN,
              eval_info_off_,
              flag_,
              ser_eval_batch_func_,
              eval_flags_off_,
              pvt_skip_off_);

  ObExprOperatorType type = T_INVALID;
  if (nullptr != extra_info_) {
    type = extra_info_->type_;
  }
  OB_UNIS_ADD_LEN(type);
  if (T_INVALID != type) {
    OB_UNIS_ADD_LEN(*extra_info_);
  }
  OB_UNIS_ADD_LEN(dyn_buf_header_offset_);
  OB_UNIS_ADD_LEN(local_session_var_id_);
  return len;
}

ObExpr::ObExpr()
    : magic_(MAGIC_NUM),
    type_(T_INVALID),
    datum_meta_(),
    obj_meta_(),
    max_length_(UINT32_MAX),
    obj_datum_map_(OBJ_DATUM_NULL),
    flag_(0),
    eval_func_(NULL),
    eval_batch_func_(NULL),
    inner_functions_(NULL),
    inner_func_cnt_(0),
    args_(NULL),
    arg_cnt_(0),
    parents_(NULL),
    parent_cnt_(0),
    frame_idx_(0),
    datum_off_(0),
    eval_info_off_(0),
    dyn_buf_header_offset_(0),
    res_buf_off_(0),
    res_buf_len_(0),
    eval_flags_off_(0),
    pvt_skip_off_(0),
    expr_ctx_id_(INVALID_EXP_CTX_ID),
    extra_(0),
    basic_funcs_(NULL),
    batch_idx_mask_(0),
    extra_info_(NULL),
    local_session_var_id_(OB_INVALID_INDEX_INT64)
{
  is_called_in_sql_ = 1;
}

char *ObExpr::alloc_str_res_mem(ObEvalCtx &ctx, const int64_t size, const int64_t idx) const
{
  char *mem = NULL;
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!ObDynReserveBuf::supported(datum_meta_.type_))) {
    LOG_ERROR("unexpected alloc string result memory called", K(size), K(*this));
  } else {
    ObDynReserveBuf *drb = reinterpret_cast<ObDynReserveBuf *>(
        ctx.frames_[frame_idx_] + dyn_buf_header_offset_ + sizeof(ObDynReserveBuf) * idx);
    if (OB_LIKELY(drb->len_ >= size)) {
      mem = drb->mem_;
    } else {
      const int64_t alloc_size = next_pow2(size);
      if (OB_UNLIKELY(alloc_size > UINT32_MAX)) {
        ret = OB_INVALID_ARGUMENT;
      } else if (OB_ISNULL(mem = static_cast<char *>(ctx.alloc_expr_res(alloc_size)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        // When extend memory, the old memory can not free, because the old memory may
        // still be referenced. see: ob_datum_cast.cpp::common_copy_string
        if (0 == drb->len_) {
          drb->magic_ = ObDynReserveBuf::MAGIC_NUM;
        }
        drb->len_ = alloc_size;
        drb->mem_ = mem;
      }
    }
  }
  return mem;
};


int ObExpr::eval_enumset(ObEvalCtx &ctx,
    const common::ObIArray<common::ObString> &str_values, const uint64_t cast_mode,
    common::ObDatum *&datum) const
{
  // performance critical, do not check %frame_idx_ and %frame again. (checked in CG)
  int ret = common::OB_SUCCESS;
  char *frame = ctx.frames_[frame_idx_];
  OB_ASSERT(NULL != frame);
  datum = (ObDatum *)(frame + datum_off_);
  ObEvalInfo *eval_info = (ObEvalInfo *)(frame + eval_info_off_);
  const common::SqlTypeClass in_tc = args_[0]->obj_meta_.get_type_class();
  EvalEnumSetFunc eval_func;

  // do nothing for const/column reference expr or already evaluated expr
  if (is_batch_result()) {
    // Evaluate one datum within a batch.
    bool need_evaluate = false;
    if (eval_info->projected_) {
      datum = datum + ctx.get_batch_idx();
    } else {
      ObBitVector* evaluated_flags = to_bit_vector(frame + eval_flags_off_);
      if (!eval_info->evaluated_) {
        evaluated_flags->reset(ctx.get_batch_size());
        reset_datums_ptr(frame, ctx.get_batch_size());
        // See `clear_datum_descriptors` below for the rationale.
        clear_datum_descriptors(frame, ctx.get_batch_size());
        eval_info->evaluated_ = true;
        eval_info->cnt_ = ctx.get_batch_size();
        eval_info->point_to_frame_ = true;
        eval_info->notnull_ = true;
      }
      if (!evaluated_flags->at(ctx.get_batch_idx())) {
        need_evaluate = true;
      }
      datum = reinterpret_cast<ObDatum *>(frame + datum_off_) + ctx.get_batch_idx();
      if (need_evaluate) {
        if (OB_FAIL(ObDatumCast::get_enumset_cast_function(in_tc, obj_meta_.get_type(), eval_func))) {
        } else {
          ret = eval_func(*this, str_values, cast_mode, ctx, *datum);
        }
        if (OB_SUCC(ret)) {
          evaluated_flags->set(ctx.get_batch_idx());
        } else {
          datum->set_null();
        }
        if (datum->is_null()) {
          eval_info->notnull_ = false;
        }
      }
    }
  } else {
    if (!eval_info->evaluated_) {
      if (datum->ptr_ != frame + res_buf_off_) {
        datum->ptr_ = frame + res_buf_off_;
      }
      if (OB_FAIL(ObDatumCast::get_enumset_cast_function(in_tc, obj_meta_.get_type(), eval_func))) {
      } else {
        ret = eval_func(*this, str_values, cast_mode, ctx, *datum);
      }

      if (OB_LIKELY(common::OB_SUCCESS == ret)) {
        eval_info->evaluated_ = true;
      } else {
        datum->set_null();
      }
    }
  }
  return ret;
}


void *ObExprStrResAlloc::alloc(const int64_t size)
{
  void *mem = expr_.get_str_res_mem(ctx_, off_ + size);
  if (NULL != mem) {
    mem = static_cast<char *>(mem) + off_;
    off_ += size;
  }
  return mem;
}

void capture_datum_parameter(const ObExpr &expr, ObEvalCtx &ctx, DatumParameter &value)
{
  DatumParameter view;
  view.set_datum(expr.locate_expr_datum(ctx));
  SqlTypeMeta meta = expr.obj_meta_;
  ObAccuracy accuracy;
  accuracy.set_scale(expr.datum_meta_.scale_);
  accuracy.set_precision(expr.datum_meta_.precision_);
  accuracy.set_length(expr.max_length_);
  if (meta.is_string_or_lob_locator_type()) {
    accuracy.set_length_semantics(expr.datum_meta_.length_semantics_);
  } else if (meta.is_numeric_type() || meta.is_temporal_type()) {
    meta.set_scale(expr.datum_meta_.scale_);
  }
  view.set_type_meta(meta);
  view.set_accuracy(accuracy);
  view.set_param_meta();
  value = view;
}

int bind_datum_parameter(const DatumParameter &source, DatumParameter &destination,
                          ObIAllocator *)
{
  destination = source;
  return OB_SUCCESS;
}

int clone_datum_value(const ObDatum &source, const SqlTypeMeta &meta, ObDatum &destination,
                      ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  ObDatum owned;
  if (meta.is_ext_sql_array() && !source.is_null()) {
    DatumParameter input, copy;
    input.set_datum(source);
    input.set_type_meta(meta);
    if (OB_FAIL(DatumParameterArray::clone_parameter(input, copy, allocator))) {
    } else {
      owned = copy.datum_;
    }
  } else {
    ret = owned.deep_copy(source, allocator);
  }
  if (OB_SUCC(ret)) {
    destination = owned;
  }
  return ret;
}

int clone_datum_value(const DatumParameter &source, DatumParameter &destination,
                      ObIAllocator &allocator)
{
  ObDatum owned;
  const int ret = clone_datum_value(source.datum_, source.get_meta(), owned, allocator);
  if (ret == OB_SUCCESS) {
    destination.datum_ = owned;
    destination.set_type_meta(source.get_meta());
  }
  return ret;
}

int clone_datum_parameter(const DatumParameter &source, DatumParameter &destination,
                           ObIAllocator &allocator)
{
  return DatumParameterArray::clone_parameter(source, destination, allocator);
}

// Array and string payloads remain native, owned by the parameter's arena.
int assign_datum_parameter(const DatumParameter &source, DatumParameter &destination)
{
  destination = source;
  return OB_SUCCESS;
}

DEF_TO_STRING(ObToStringDatum)
{
  int64_t pos = 0;
  const DatumValue value(d_, e_.obj_meta_);
  int ret = print_datum_debug_json(value, buf, buf_len, pos);
  UNUSED(ret);
  return pos;
}

DEF_TO_STRING(ObToStringExpr)
{
  int64_t pos = 0;
  if (c_.is_vectorized() && c_.get_batch_size() <= 0) {
    // vectorize execution but batch_size not specified, do nothing
    J_OBJ_START();
    J_KV("Expr print ERROR", "vectorize execution but batch_size not specified");
    J_OBJ_END();
  } else if (c_.is_vectorized() && c_.get_batch_size() > 0 &&
             c_.get_batch_idx() >= c_.get_batch_size()) {
    // vectorize execution but batch_idx is not set correctly, do nothing
    J_OBJ_START();
    J_KV("Expr print ERROR", "vectorize execution but batch_idx is not set correctly");
    J_COMMA();
    J_KV("batch_idx", c_.get_batch_idx());
    J_OBJ_END();
  } else {
    ObDatum *datum = NULL;
    int ret = e_.eval(c_, datum);
    UNUSED(ret);
    pos = ObToStringDatum(e_, *datum).to_string(buf, buf_len);
  }
  return pos;
}

DEF_TO_STRING(ObToStringExprRow)
{
  int64_t pos = 0;
  J_ARRAY_START();
  for (int64_t i = 0; i < exprs_.count(); i++) {
    ObExpr *expr = exprs_.at(i);
    if (OB_LIKELY(expr != NULL)) {
      J_OBJ_START();
      J_KV(KP(expr));
      J_COMMA();
      pos += ObToStringExpr(c_, *expr).to_string(buf + pos, buf_len - pos);
      J_OBJ_END();
    } else {
      J_OBJ_START();
      J_OBJ_END();
    }
    if (i != exprs_.count() - 1) {
      J_COMMA();
    }
  }
  J_ARRAY_END();
  return pos;
}

void ObExpr::reset_datums_ptr(char *frame, const int64_t size) const
{
  // TODO bin.lb: on reset ptr from info->cnt_ to size if info->point_to_frame_ ?
  ObDatum *datum = reinterpret_cast<ObDatum *>(frame + datum_off_);
  ObDatum *datum_end = datum + size;
  char *ptr = frame + res_buf_off_;
  for (; datum < datum_end; datum += 1) {
    if (datum->ptr_ != ptr) {
      datum->ptr_ = ptr;
    }
    ptr += res_buf_len_;
  }
}

// Zero out the datum descriptor part (len_/null_/flag_) on first evaluation.
// The datums array inside an expression frame is intentionally excluded from
// the frame's zero-init region (see ObStaticEngineExprCG::create_tmp_frameinfo).
// Although readers should gate access on evaluated flags and skip bits, some
// legacy batch paths can touch slots that were not written by the current
// batch. Clearing pack_ here makes those slots a safe empty descriptor while
// keeping ptr_ anchored to the expression's reserved buffer.
//
// Do this only after reset_datums_ptr() on the !info->evaluated_ branches.
// Batch/scalar evaluators overwrite pack_ for evaluated slots; clearing it at
// any later point would destroy valid results.
void ObExpr::clear_datum_descriptors(char *frame, const int64_t size) const
{
  ObDatum *datum = reinterpret_cast<ObDatum *>(frame + datum_off_);
  for (int64_t i = 0; i < size; i++) {
    datum[i].pack_ = 0;
  }
}

void ObExpr::reset_datum_ptr(char *frame, const int64_t size, const int64_t idx) const
{
  ObDatum *datum = reinterpret_cast<ObDatum *>(frame + datum_off_);
  datum += idx;
  char *ptr = frame + res_buf_off_ + (batch_idx_mask_ & idx) * res_buf_len_;
  if (datum->ptr_ != ptr && idx < size) {
    datum->ptr_ = ptr;
  }
}

int ObExpr::eval_one_datum_of_batch(ObEvalCtx &ctx, common::ObDatum *&datum) const
{
#define INNER_BATCH_SIZE() MAX(1, ctx.get_batch_size())
  int ret = OB_SUCCESS;
  char *frame = ctx.frames_[frame_idx_];
  ObEvalInfo *info = reinterpret_cast<ObEvalInfo *>(frame + eval_info_off_);
  bool need_evaluate = false;

  if (info->projected_ || NULL == eval_func_) {
    // do nothing
  } else if (!info->evaluated_) {
    need_evaluate = true;
    to_bit_vector(frame + eval_flags_off_)->reset(ctx.get_batch_size());
    reset_datums_ptr(frame, ctx.get_batch_size());
    // See `clear_datum_descriptors` above for the rationale.
    clear_datum_descriptors(frame, INNER_BATCH_SIZE());
    info->evaluated_ = true;
    info->cnt_ = ctx.get_batch_size();
    info->point_to_frame_ = true;
    info->notnull_ = true;
  } else {
    ObBitVector *evaluated_flags = to_bit_vector(frame + eval_flags_off_);
    if (!evaluated_flags->at(ctx.get_batch_idx())) {
      need_evaluate = true;
    }
  }
  datum = reinterpret_cast<ObDatum *>(frame + datum_off_) + ctx.get_batch_idx();
  if (OB_SUCC(ret) && need_evaluate) {
    if (OB_UNLIKELY(need_stack_check_) && OB_FAIL(check_stack_overflow())) {
      SQL_LOG(WARN, "failed to check stack overflow", K(ret));
    } else {
      reset_datum_ptr(frame, INNER_BATCH_SIZE(), ctx.get_batch_idx());
      ret = eval_func_(*this, ctx, *datum);
      if (OB_SUCC(ret)) {
        ObBitVector *evaluated_flags = to_bit_vector(frame + eval_flags_off_);
        evaluated_flags->set(ctx.get_batch_idx());
      } else {
        datum->set_null();
      }
      if (datum->is_null()) {
        info->notnull_ = false;
      }
    }
  }

  return ret;
#undef INNER_BATCH_SIZE
}

int ObExpr::do_eval_batch(ObEvalCtx &ctx,
                          const ObBitVector &skip,
                          const int64_t size) const
{
  int ret = OB_SUCCESS;
  char *frame = ctx.frames_[frame_idx_];
  ObEvalInfo *info = reinterpret_cast<ObEvalInfo *>(frame + eval_info_off_);
  bool need_evaluate = false;
  if (info->projected_ || NULL == eval_batch_func_) {
    // expr values is projected by child or has no evaluate func, do nothing.
  } else if (!info->evaluated_) {
    need_evaluate = true;
    to_bit_vector(frame + eval_flags_off_)->reset(size);
  } else {
    // check all datum evaluated
    const ObBitVector *evaluated_vec = to_bit_vector(frame + eval_flags_off_);
    need_evaluate = !ObBitVector::bit_op_zero(
        skip, *evaluated_vec, size,
        [](const uint64_t l, const uint64_t r) { return ~(l | r); });
  }

  if (need_evaluate) {
    // Set ObDatum::ptr_ point to reserved buffer in the first evaluation.
    // FIXME bin.lb: maybe we can optimize this by ObEvalInfo::point_to_frame_
    if (!info->evaluated_) {
      reset_datums_ptr(frame, size);
      // See `clear_datum_descriptors` above for the rationale.
      clear_datum_descriptors(frame, size);
      info->notnull_ = false;
      info->point_to_frame_ = true;
      info->evaluated_ = true;
      info->cnt_ = size;
    }
    if (OB_UNLIKELY(need_stack_check_) && OB_FAIL(check_stack_overflow())) {
      SQL_LOG(WARN, "failed to check stack overflow", K(ret));
    } else {
      ret = (*eval_batch_func_)(*this, ctx, skip, size);
      if (OB_SUCC(ret)) {
      } else {
        ObDatum *datum = reinterpret_cast<ObDatum *>(frame + datum_off_);
        ObDatum *datum_end = datum + size;
        for (; datum < datum_end; datum += 1) {
          datum->set_null();
        }
      }
    }
  }
  return ret;
}

int expr_default_eval_batch_func(const ObExpr &expr,
                                 ObEvalCtx &ctx,
                                 const ObBitVector &skip,
                                 const EvalBound &bound)
{
  int ret = OB_SUCCESS;

  char *frame = ctx.frames_[expr.frame_idx_];
  ObBitVector *evaluated_flags = to_bit_vector(frame + expr.eval_flags_off_);
  ObDatum *datum = reinterpret_cast<ObDatum *>(frame + expr.datum_off_);
  ObEvalCtx::BatchInfoScopeGuard batch_info_guard(ctx);
  batch_info_guard.set_batch_size(bound.batch_size());
  bool got_null = false;
  for (int64_t i = bound.start(); OB_SUCC(ret) && i < bound.end(); i++) {
    if (!skip.contain(i) && !evaluated_flags->contain(i)) {
      // set current evaluate index
      batch_info_guard.set_batch_idx(i);
      ret = expr.eval_func_(expr, ctx, datum[i]);
      evaluated_flags->set(i);
      if (datum[i].is_null()) {
        got_null = true;
      }

      if (OB_FAIL(ret) && ctx.exec_ctx_.get_my_session()->is_diagnosis_enabled()) {
        // overwrite ret on diagnosis node
        if (OB_FAIL(ctx.exec_ctx_.get_diagnosis_manager().add_warning_info(ret, i))) {
        } else if (OB_FAIL(ObExprColumnConv::calc_column_name_for_diagnosis(expr, ctx,
                                                          ctx.exec_ctx_.get_diagnosis_manager()))) {
        } else {
          // set null to avoid accessing invalid data before setting skip
          // in ObTableScanOp::do_diagnosis
          datum[i].set_null();
          got_null = true;
        }
      }
    }
  }
  // reset evaluate index
  if (got_null) {
    expr.get_eval_info(ctx).notnull_ = false;
  }
  return ret;
}

int expr_default_eval_batch_func(const ObExpr &expr,
                                 ObEvalCtx &ctx,
                                 const ObBitVector &skip,
                                 const int64_t size)
{
  return expr_default_eval_batch_func(expr, ctx, skip, EvalBound(size));
}

int eval_question_mark_func(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ctx.exec_ctx_.get_physical_plan_ctx())) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const auto &param_store = ctx.exec_ctx_.get_physical_plan_ctx()->get_param_store();
    if (expr.extra_ >= param_store.count()) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      const DatumParameter &v = param_store.at(expr.extra_);
      if (v.get_type() != expr.datum_meta_.type_ && !v.is_null()) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        expr_datum = v.datum_;
        if (is_lob_storage(v.get_type())) {
          ret = ob_adjust_lob_datum(ctx.exec_ctx_, v.datum_, v.get_meta(),
                                    expr.obj_meta_, ctx.exec_ctx_.get_allocator(), expr_datum);
        }
      }
    }
  }
  return ret;
}

int eval_assign_question_mark_func(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  const ObSQLSessionInfo *session = ctx.exec_ctx_.get_my_session();
  if (OB_ISNULL(session) || OB_ISNULL(ctx.exec_ctx_.get_physical_plan_ctx())) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObCastMode cast_mode = CM_NONE;
    const auto &param_store = ctx.exec_ctx_.get_physical_plan_ctx()->get_param_store();
    if (expr.extra_ >= param_store.count()) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(ObSQLUtils::get_default_cast_mode(
                       session->get_stmt_type(), session, cast_mode))) {
    } else {
      const DatumParameter &parameter = param_store.at(expr.extra_);
      const common::SqlTypeMeta dst_meta = expr.obj_meta_;
      ObIAllocator &allocator = ctx.exec_ctx_.get_allocator();
      const ObDataTypeCastParams dtc_params = ObBasicSessionInfo::create_dtc_params(session);
      cast_mode = (cast_mode | CM_COLUMN_CONVERT);
      if (ctx.exec_ctx_.get_physical_plan_ctx()->is_ignore_stmt()) {
        cast_mode = cast_mode | CM_WARN_ON_FAIL | CM_CHARSET_CONVERT_IGNORE_ERR;
      }
      ObCastCtx cast_ctx(&allocator, &dtc_params, cast_mode, dst_meta.get_collation_type());
      ObAccuracy res_acc;
      if (dst_meta.is_decimal_int()) {
        res_acc.scale_ = expr.datum_meta_.scale_;
        res_acc.precision_ = expr.datum_meta_.precision_;
        cast_ctx.res_accuracy_ = &res_acc;
      }
      if (ob_is_string_tc(dst_meta.get_type())) {
        cast_ctx.dest_max_length_ = expr.max_length_;
      }
      ObSqlDatumCastRuntime cast_runtime(&ctx.exec_ctx_);
      cast_ctx.runtime_ = &cast_runtime;
      session->configure_obj_cast(
          cast_ctx,
          ctx.exec_ctx_.get_srs_provider(),
          ctx.exec_ctx_.get_lob_read_service());
      const DatumValue source(parameter.datum_, parameter.get_meta());
      DatumValue converted;
      if (OB_FAIL(DatumValueCaster::to_type(dst_meta.get_type(), cast_ctx,
                                             source, allocator, converted))) {
      } else {
        expr_datum = converted.datum_;
      }
    }
  }
  return ret;
}

int ToStringExprRowVec::meta_to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  J_OBJ_START();
  BUF_PRINTF("skip: ");
  J_ARRAY_START();
  if (NULL == skip_) {
    BUF_PRINTF("null");
  } else {
    for (int64_t i = 0; i < batch_size_; i++) {
      BUF_PRINTF("%d", skip_->at(i));
      if (i != batch_size_ - 1) {
        J_COMMA();
      }
    }
  }
  J_ARRAY_END();
  J_COMMA();
  BUF_PRINTF("exprs: ");
  J_ARRAY_START();
  for (int64_t i = 0; i < exprs_.count(); i++) {
    const ObExpr *expr = exprs_.at(i);
    J_OBJ_START();
    if (OB_LIKELY(NULL != expr)) {
      J_KV(KP(expr));
    }
    J_OBJ_END();
    if (i != exprs_.count() - 1) {
      J_COMMA();
    }
  }
  J_ARRAY_END();
  J_OBJ_END();
  return pos;
}

DEF_TO_STRING(ToStringExprRowVec)
{
  int64_t pos = 0;
  const int64_t index = ctx_.get_batch_idx();
  J_ARRAY_START();
  for (int64_t i = 0; i < exprs_.count(); i++) {
    const ObExpr *expr = exprs_.at(i);
    if (OB_LIKELY(NULL != expr)) {
      if (NULL != skip_ && skip_->at(index)) {
        J_OBJ_START();
        BUF_PRINTF("skipped");
        J_OBJ_END();
      } else {
        pos += ObToStringExpr(ctx_, *expr).to_string(buf + pos, buf_len - pos);
      }
    } else {
      J_OBJ_START();
      J_OBJ_END();
    }
    if (i != exprs_.count() - 1) {
      J_COMMA();
    }
  }
  J_ARRAY_END();
  return pos;
}
} // end namespace sql
} // end namespace oceanbase
