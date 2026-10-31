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
#include "common/datum/datum_copy_plan.h"
#include "common/datum/datum_sql_print.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/expr/ob_datum_cast.h"
#include "sql/engine/expr/ob_array_expr_utils.h"
#include "sql/engine/expr/ob_expr_column_conv.h"
#include "sql/engine/expr/ob_expr_util.h"
#include "sql/engine/expr/ob_obj_cast_runtime.h"
#include "share/datum/datum_value_cast.h"
#include "share/datum/typed_datum_compare.h"
#include "sql/resolver/expr/ob_raw_expr_util.h"
#include "common/enumset/ob_enum_set_meta.h"

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
    if (OB_SUCC(ret)) { ret = serialize_native_metadata(buf, buf_len, pos); }
  }

  return ret;
}

OB_DEF_DESERIALIZE(ObExpr)
{
  int ret = OB_SUCCESS;
  // Marked PX arrays explicitly encode ObEmptyExpr for unselected positions.
  // Only an actually empty body is a placeholder; a nonempty truncated Expr
  // must still fail while decoding its mandatory native metadata below.
  if (pos == data_len) { *this = ObExpr(); return ret; }
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

  if (is_batch_result()) {
    batch_idx_mask_ = UINT64_MAX;
  }
  OB_UNIS_DECODE(dyn_buf_header_offset_);
  OB_UNIS_DECODE(local_session_var_id_);
  if (OB_SUCC(ret)) {
    ret = deserialize_native_metadata(buf, data_len, pos, CURRENT_CONTEXT->get_arena_allocator());
  }
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
  const int64_t metadata_size = native_metadata_serialize_size();
  if (metadata_size < 0 || __builtin_add_overflow(len, metadata_size, &len)) { len = -1; }
  return len;
}

ObExpr::ObExpr()
    : magic_(MAGIC_NUM),
    type_(T_INVALID),
    datum_meta_(),
    sql_type_(nullptr),
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
    native_cmp_ops_(nullptr),
    native_cmp_specs_(nullptr),
    native_cmp_op_count_(0),
    batch_idx_mask_(0),
    extra_info_(NULL),
    local_session_var_id_(OB_INVALID_INDEX_INT64)
{
  is_called_in_sql_ = 1;
}

int ObExpr::bind_sql_type(const common::SqlType &type, common::ObIAllocator &owner)
{
  int ret = OB_SUCCESS;
  if (sql_type_ != nullptr && sql_type_->equals(type) && basic_funcs_ != nullptr) {
    // The private runtime caster may see the same scalar type repeatedly.
  } else {
    const common::SqlType *candidate_type = nullptr;
    common::DatumBasicFuncs *candidate_functions = nullptr;
    if (OB_FAIL(type.clone_to(owner, candidate_type))) {
    } else if ((candidate_functions = static_cast<common::DatumBasicFuncs *>(
        owner.alloc(sizeof(common::DatumBasicFuncs)))) == nullptr) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      new (candidate_functions) common::DatumBasicFuncs();
      if (OB_SUCC(ret = common::ObDatumFuncs::prepare_basic(*candidate_type, *candidate_functions))) {
        sql_type_ = candidate_type;
        basic_funcs_ = candidate_functions;
      }
    }
  }
  return ret;
}

int ObExpr::prepare_scalar_type(const common::SqlTypeMeta &meta, const common::ObAccuracy &accuracy,
                                common::ObIAllocator &owner)
{
  common::SqlType type;
  const int ret = common::SqlType::from_legacy(meta, accuracy, type);
  return ret == OB_SUCCESS ? bind_sql_type(type, owner) : ret;
}

int ObExpr::prepare_resolved_type(const ObRawExprResType &resolved, const ObSQLSessionInfo *session,
                                 common::ObIAllocator &owner)
{
  int ret = OB_SUCCESS;
  if (!resolved.is_enum_or_set() && !resolved.is_enumset_inner_type()
      && !resolved.is_collection_sql_type() && !obj_meta_.is_pl_extend_type()
      && !obj_meta_.is_ext_sql_array() && obj_meta_.get_type() != ObUserDefinedSQLType) {
    // CG has already normalized the execution representation (LOB header,
    // BIT width and scalar scale). The raw resolver metadata describes the SQL
    // declaration and must not undo those representation flags.
    ret = prepare_scalar_type(obj_meta_, resolved.get_accuracy(), owner);
  } else {
    common::ObArenaAllocator temporary("ExprTypePrep");
    const common::SqlType *type = nullptr;
    ret = ObExprUtil::resolve_raw_sql_type(resolved, session, nullptr, temporary, type);
    if (OB_SUCC(ret)) { ret = bind_sql_type(*type, owner); }
  }
  return ret;
}

int64_t ObExpr::native_metadata_serialize_size() const
{
  if (sql_type_ == nullptr || native_cmp_op_count_ < 0) { return -1; }
  int64_t size = sql_type_->get_serialize_size();
  if (size < 0 || __builtin_add_overflow(size, int64_t(sizeof(int64_t)), &size)) { return -1; }
  for (int64_t i = 0; i < native_cmp_op_count_; ++i) {
    const CmpSpec &spec = native_cmp_specs_[i];
    if (spec.left_type_ == nullptr || spec.right_type_ == nullptr) { return -1; }
    const int64_t left_size = spec.left_type_->get_serialize_size();
    const int64_t right_size = spec.right_type_->get_serialize_size();
    if (left_size < 0 || right_size < 0
        || __builtin_add_overflow(size, int64_t(sizeof(int8_t) + sizeof(int32_t)), &size)
        || __builtin_add_overflow(size, left_size, &size)
        || __builtin_add_overflow(size, right_size, &size)) { return -1; }
  }
  return size;
}

int ObExpr::serialize_native_metadata(char *buffer, int64_t length, int64_t &position) const
{
  int ret = OB_SUCCESS;
  const int64_t bytes = native_metadata_serialize_size();
  if (buffer == nullptr || position < 0 || length < position || bytes < 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (bytes > length - position) {
    ret = OB_BUF_NOT_ENOUGH;
  } else {
    int64_t cursor = position;
    if (OB_FAIL(sql_type_->serialize(buffer, length, cursor))) {
    } else if (OB_FAIL(serialization::encode_i64(buffer, length, cursor, native_cmp_op_count_))) {
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < native_cmp_op_count_; ++i) {
      const CmpSpec &spec = native_cmp_specs_[i];
      if (OB_FAIL(serialization::encode_i8(buffer, length, cursor, static_cast<int8_t>(spec.null_pos_)))) {
      } else if (OB_FAIL(serialization::encode_i32(buffer, length, cursor, static_cast<int32_t>(spec.collation_)))) {
      } else if (OB_FAIL(spec.left_type_->serialize(buffer, length, cursor))) {
      } else { ret = spec.right_type_->serialize(buffer, length, cursor); }
    }
    if (OB_SUCC(ret)) { position = cursor; }
  }
  return ret;
}

int ObExpr::deserialize_native_metadata(const char *buffer, int64_t length, int64_t &position,
                                        ObIAllocator &owner)
{
  int ret = OB_SUCCESS;
  int64_t cursor = position;
  int64_t count = 0;
  const SqlType *type = nullptr;
  ObExpr candidate;
  if (buffer == nullptr || cursor < 0 || length < cursor) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(SqlType::deserialize(buffer, length, cursor, owner, type))) {
  } else if (OB_FAIL(serialization::decode_i64(buffer, length, cursor, &count))) {
  } else if (count < 0 || count > (length - cursor) / (sizeof(int8_t) + sizeof(int32_t) + 2)) {
    ret = OB_DESERIALIZE_ERROR;
  } else if (OB_FAIL(candidate.bind_sql_type(*type, owner))) {
  } else if (OB_FAIL(candidate.prepare_cmp_ops(owner, count))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    int8_t null_pos = 0;
    int32_t collation = 0;
    const SqlType *left = nullptr;
    const SqlType *right = nullptr;
    if (OB_FAIL(serialization::decode_i8(buffer, length, cursor, &null_pos))) {
    } else if (OB_FAIL(serialization::decode_i32(buffer, length, cursor, &collation))) {
    } else if ((null_pos != NULL_FIRST && null_pos != NULL_LAST)
        || collation < CS_TYPE_INVALID || collation >= CS_TYPE_MAX) {
      ret = OB_DESERIALIZE_ERROR;
    } else if (OB_FAIL(SqlType::deserialize(buffer, length, cursor, owner, left))) {
    } else if (OB_FAIL(SqlType::deserialize(buffer, length, cursor, owner, right))) {
    } else {
      ret = candidate.configure_cmp_op(i, *left, *right,
          static_cast<ObCmpNullPos>(null_pos), static_cast<ObCollationType>(collation));
    }
  }
  if (OB_SUCC(ret)) {
    sql_type_ = candidate.sql_type_;
    basic_funcs_ = candidate.basic_funcs_;
    native_cmp_ops_ = candidate.native_cmp_ops_;
    native_cmp_specs_ = candidate.native_cmp_specs_;
    native_cmp_op_count_ = candidate.native_cmp_op_count_;
    position = cursor;
  }
  return ret;
}

int ObExpr::clone_prepared_metadata_from(const ObExpr &source, common::ObIAllocator &owner)
{
  int ret = OB_SUCCESS;
  ObExpr candidate;
  if (source.sql_type_ == nullptr) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(candidate.bind_sql_type(*source.sql_type_, owner))) {
  } else if (OB_FAIL(candidate.prepare_cmp_ops(owner, source.native_cmp_op_count_))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < source.native_cmp_op_count_; ++i) {
      const CmpSpec &specification = source.native_cmp_specs_[i];
      const common::SqlType *left = nullptr;
      const common::SqlType *right = nullptr;
      if (specification.left_type_ == nullptr || specification.right_type_ == nullptr) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(specification.left_type_->clone_to(owner, left))) {
      } else if (OB_FAIL(specification.right_type_->clone_to(owner, right))) {
      } else {
        ret = candidate.configure_cmp_op(i, *left, *right, specification.null_pos_, specification.collation_);
      }
    }
  }
  if (OB_SUCC(ret)) {
    sql_type_ = candidate.sql_type_;
    basic_funcs_ = candidate.basic_funcs_;
    native_cmp_ops_ = candidate.native_cmp_ops_;
    native_cmp_specs_ = candidate.native_cmp_specs_;
    native_cmp_op_count_ = candidate.native_cmp_op_count_;
  }
  return ret;
}

namespace
{
int clone_legacy_resource(const common::SqlType &type, const common::Datum &source,
                           common::ObIAllocator &owner, common::Datum &destination, void *)
{
  common::DatumCopyPlan plan(type);
  common::ObDatum legacy;
  alignas(8) char scratch[64];
  int ret = plan.legacy_view(source, legacy, scratch, sizeof(scratch));
  if (OB_SUCC(ret)) {
    common::DatumValue input(legacy, type.legacy_meta());
    common::DatumValue owned;
    if (OB_SUCC(ret = common::clone_typed_value(input, owned, owner))) {
      ret = plan.import_legacy(owned.datum_, destination);
    }
  }
  return ret;
}
}

int ObExpr::clone_native_value(const common::SqlType &type, const common::Datum &source,
                               common::ObIAllocator &owner, common::Datum &destination)
{
  common::DatumCopyPlan plan(type);
  return type.requires_resource_clone()
      ? plan.deep_copy_resource(source, destination, owner, clone_legacy_resource, nullptr)
      : plan.deep_copy(source, destination, owner);
}

int ObExpr::init_runtime_functions(common::ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  common::DatumBasicFuncs *functions = nullptr;
  if (sql_type_ == nullptr) {
    ret = OB_ERR_UNEXPECTED;
  } else if ((functions = static_cast<common::DatumBasicFuncs *>(
      allocator.alloc(sizeof(common::DatumBasicFuncs)))) == nullptr) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    new (functions) common::DatumBasicFuncs();
    if (OB_SUCC(ret = common::ObDatumFuncs::prepare_basic(*sql_type_, *functions))) {
      basic_funcs_ = functions;
    }
  }
  return ret;
}

int ObExpr::prepare_cmp_ops(common::ObIAllocator &allocator, int64_t count)
{
  int ret = OB_SUCCESS;
  common::DatumCmpOp *operations = nullptr;
  CmpSpec *specifications = nullptr;
  if (count < 0 || count > INT64_MAX / static_cast<int64_t>(sizeof(common::DatumCmpOp))
      || count > INT64_MAX / static_cast<int64_t>(sizeof(CmpSpec))) {
    ret = OB_INVALID_ARGUMENT;
  } else if (count > 0) {
    operations = static_cast<common::DatumCmpOp *>(allocator.alloc(count * sizeof(common::DatumCmpOp)));
    specifications = static_cast<CmpSpec *>(allocator.alloc(count * sizeof(CmpSpec)));
    if (operations == nullptr || specifications == nullptr) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      for (int64_t i = 0; i < count; ++i) {
        new (operations + i) common::DatumCmpOp();
        new (specifications + i) CmpSpec();
      }
    }
  }
  if (OB_SUCC(ret)) {
    native_cmp_ops_ = operations;
    native_cmp_specs_ = specifications;
    native_cmp_op_count_ = count;
  }
  return ret;
}

int ObExpr::configure_cmp_op(int64_t index, int64_t left_arg, int64_t right_arg,
                             common::ObCmpNullPos null_pos, common::ObCollationType collation)
{
  int ret = OB_SUCCESS;
  if (index < 0 || index >= native_cmp_op_count_ || left_arg < 0 || right_arg < 0
      || left_arg >= arg_cnt_ || right_arg >= arg_cnt_ || args_ == nullptr
      || args_[left_arg] == nullptr || args_[right_arg] == nullptr) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ret = configure_cmp_op(index, args_[left_arg]->get_sql_type(), args_[right_arg]->get_sql_type(),
                           null_pos, collation);
  }
  return ret;
}

int ObExpr::configure_cmp_op(int64_t index, const common::SqlType &left, const common::SqlType &right,
                             common::ObCmpNullPos null_pos, common::ObCollationType collation)
{
  int ret = OB_SUCCESS;
  if (index < 0 || index >= native_cmp_op_count_) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    common::DatumCmpOp candidate;
    if (OB_SUCC(ret = common::ObDatumFuncs::prepare_cmp(left, right, null_pos, collation, candidate))) {
      native_cmp_ops_[index] = candidate;
      native_cmp_specs_[index].left_type_ = &left;
      native_cmp_specs_[index].right_type_ = &right;
      native_cmp_specs_[index].null_pos_ = null_pos;
      native_cmp_specs_[index].collation_ = collation;
    }
  }
  return ret;
}

const common::DatumCmpOp &ObExpr::get_cmp_op(int64_t index) const
{
  OB_ASSERT(index >= 0 && index < native_cmp_op_count_);
  return native_cmp_ops_[index];
}



char *ObExpr::alloc_str_res_mem(ObEvalCtx &ctx, const int64_t size, const int64_t idx) const
{
  char *mem = NULL;
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!ObDynReserveBuf::supported(datum_meta_.type_))) {
    LOG_ERROR("unexpected alloc string result memory called", K(size), K(*this));
  } else if (dyn_buf_header_offset_ == 0) {
    // A dynamically rebound private/value expression may have started as an
    // inline scalar and therefore owns no dynamic header in its fixed frame.
    // Allocate in its result arena instead of interpreting Datum bytes as one.
    if (size >= 0 && size <= UINT32_MAX) { mem = static_cast<char *>(ctx.alloc_expr_res(size)); }
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
    common::Datum *&datum) const
{
  // performance critical, do not check %frame_idx_ and %frame again. (checked in CG)
  int ret = common::OB_SUCCESS;
  char *frame = ctx.frames_[frame_idx_];
  OB_ASSERT(NULL != frame);
  datum = (Datum *)(frame + datum_off_);
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
      datum = reinterpret_cast<Datum *>(frame + datum_off_) + ctx.get_batch_idx();
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

int capture_datum_parameter(const ObExpr &expr, ObEvalCtx &ctx, DatumParameter &value)
{
  const DatumCopyPlan plan = expr.get_datum_copy_plan();
  const Datum &source = expr.locate_expr_datum(ctx);
  common::ObDatum legacy;
  int ret = plan.borrow_legacy(source, legacy);
  if (ret == OB_NOT_SUPPORTED) {
    int64_t bytes = 0;
    ret = plan.legacy_payload_size(source, bytes);
    if (OB_SUCC(ret)) {
      char *buffer = bytes == 0 ? nullptr
          : static_cast<char *>(ctx.exec_ctx_.get_allocator().alloc(bytes));
      if (bytes > 0 && buffer == nullptr) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        int64_t position = 0;
        ret = plan.export_legacy(source, legacy, buffer, bytes, position);
      }
    }
  }
  if (OB_SUCC(ret)) {
    DatumParameter view;
    view.set_datum(legacy);
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
  return ret;
}

int bind_datum_parameter(const DatumParameter &source, DatumParameter &destination,
                          ObIAllocator *)
{
  destination = source;
  return OB_SUCCESS;
}

static int clone_datum_value_with_accuracy(const common::ObDatum &source, const SqlTypeMeta &meta,
                                           const ObAccuracy &accuracy, common::ObDatum &destination,
                                           ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  common::ObDatum owned;
  if (meta.is_ext_sql_array() && !source.is_null()) {
    DatumParameter input, copy;
    input.set_datum(source);
    input.set_type_meta(meta);
    input.set_accuracy(accuracy);
    if (OB_FAIL(DatumParameterArray::clone_parameter(input, copy, allocator))) {
    } else {
      owned = copy.datum_;
    }
  } else {
    const DatumCopyPlan plan(meta, accuracy);
    ret = plan.deep_copy(source, owned, allocator);
  }
  if (OB_SUCC(ret)) {
    destination = owned;
  }
  return ret;
}

int clone_datum_value(const common::ObDatum &source, const SqlTypeMeta &meta, common::ObDatum &destination,
                      ObIAllocator &allocator)
{
  return clone_datum_value_with_accuracy(source, meta, ObAccuracy(), destination, allocator);
}

int clone_datum_value(const DatumParameter &source, DatumParameter &destination,
                      ObIAllocator &allocator)
{
  common::ObDatum owned;
  const int ret = clone_datum_value_with_accuracy(source.datum_, source.get_meta(),
      source.get_accuracy(), owned, allocator);
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
  const DatumCopyPlan plan = e_.get_datum_copy_plan();
  common::ObDatum legacy;
  alignas(8) char scratch[64];
  int ret = plan.borrow_legacy(d_, legacy);
  if (ret == OB_NOT_SUPPORTED) {
    int64_t cursor = 0;
    ret = plan.export_legacy(d_, legacy, scratch, sizeof(scratch), cursor);
  }
  if (OB_SUCC(ret)) {
    const DatumValue value(legacy, e_.obj_meta_);
    ret = print_datum_debug_json(value, buf, buf_len, pos);
  }
  if (OB_FAIL(ret)) {
    databuff_printf(buf, buf_len, pos, "{datum_print_error:%d}", ret);
  }
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
    Datum *datum = NULL;
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

// Called only before a fresh batch is evaluated. Skipped slots start as NULL;
// already evaluated values are never cleared by this initialization.
void ObExpr::clear_datum_descriptors(char *frame, const int64_t size) const
{
  Datum *datums = reinterpret_cast<Datum *>(frame + datum_off_);
  for (int64_t i = 0; i < size; ++i) { datums[i].set_null(); }
}

int ObExpr::eval_one_datum_of_batch(ObEvalCtx &ctx, common::Datum *&datum) const
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
  datum = reinterpret_cast<Datum *>(frame + datum_off_) + ctx.get_batch_idx();
  if (OB_SUCC(ret) && need_evaluate) {
    if (OB_UNLIKELY(need_stack_check_) && OB_FAIL(check_stack_overflow())) {
      SQL_LOG(WARN, "failed to check stack overflow", K(ret));
    } else {
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
    if (!info->evaluated_) {
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
        Datum *datum = reinterpret_cast<Datum *>(frame + datum_off_);
        Datum *datum_end = datum + size;
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
  Datum *datum = reinterpret_cast<Datum *>(frame + expr.datum_off_);
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

int eval_question_mark_func(const ObExpr &expr, ObEvalCtx &ctx, Datum &expr_datum)
{
  int ret = OB_SUCCESS;
  const ObPhysicalPlanCtx *plan_ctx = ctx.exec_ctx_.get_physical_plan_ctx();
  if (plan_ctx == nullptr) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const Datum *value = plan_ctx->get_native_parameter(expr.extra_);
    const SqlType *type = plan_ctx->get_native_parameter_type(expr.extra_);
    if (value == nullptr || type == nullptr) {
      ret = OB_INVALID_ARGUMENT;
    } else if (!value->is_null() && type->kind() != expr.datum_meta_.type_) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      Datum candidate = *value;
      if (is_lob_storage(type->kind())) {
        ret = ob_adjust_lob_datum(ctx.exec_ctx_, *value, type->legacy_meta(),
            expr.obj_meta_, ctx.exec_ctx_.get_allocator(), candidate);
      }
      if (OB_SUCC(ret)) { expr_datum = candidate; }
    }
  }
  return ret;
}

int eval_assign_question_mark_func(const ObExpr &expr, ObEvalCtx &ctx, Datum &expr_datum)
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
        ret = expr.get_datum_copy_plan().import_legacy(converted.datum_, expr_datum);
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
