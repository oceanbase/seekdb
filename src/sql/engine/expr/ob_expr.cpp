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
#include "common/datum/datum_parameter_codec.h"
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
#include "sql/pl/ob_pl.h"
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
  // Native lane descriptions belong to a local plan owner and are never wire
  // pointers. The encoded offsets always describe the generic alternative.
  native_i64_layout_ = nullptr;
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
    native_i64_layout_(nullptr),
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
                                 common::ObIAllocator &owner,
                                 share::schema::ObSchemaGetterGuard *schema_guard,
                                 const pl::ObPLINS *pl_declarations)
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
    ret = ObExprUtil::resolve_raw_sql_type(resolved, session, schema_guard, temporary, type, pl_declarations);
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
    common::DatumValue input, owned;
    if (OB_FAIL(input.assign_legacy_borrowed(legacy, type.legacy_meta()))) {
    } else if (OB_FAIL(common::clone_typed_value(input, owned, owner))) {
    } else if (OB_FAIL(owned.export_legacy(legacy, scratch, sizeof(scratch)))) {
    } else {
      ret = plan.import_legacy(legacy, destination);
      if (OB_SUCC(ret) && destination.is_present() && owned.datum_.is_inline_extended()) {
        destination = owned.datum_; // Never return a Reference to local scratch.
      }
    }
  }
  return ret;
}
}

int ObExpr::clone_native_value(const common::SqlType &type, const common::Datum &source,
                               common::ObIAllocator &owner, common::Datum &destination)
{
  common::DatumCopyPlan plan(type);
  if (type.legacy_meta().is_ext_sql_array()) {
    NativeTypedDatum borrowed, owned;
    borrowed.datum_ = source; borrowed.type_ = &type;
    const int ret = clone_native_parameter_value(borrowed, owner, owned);
    if (ret == OB_SUCCESS) { destination = owned.datum_; }
    return ret;
  }
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

int ObExpr::get_native_i64_mode(ObEvalCtx &ctx, bool &native) const
{
  if (native_i64_layout_ == nullptr) {
    native = false;
    return OB_SUCCESS;
  }
  if (ctx.frames_ == nullptr || ctx.frames_[frame_idx_] == nullptr
      || native_i64_layout_->capacity == 0 || native_parameter_alias_) {
    return OB_ERR_UNEXPECTED;
  }
  const auto &header = *reinterpret_cast<const NativeI64FrameHeader *>(ctx.frames_[frame_idx_]);
  if (header.magic != NativeI64FrameHeader::MAGIC
      || header.version != NativeI64FrameHeader::VERSION
      || header.compact > 1 || header.reserved != 0) {
    return OB_ERR_UNEXPECTED;
  }
  native = header.compact != 0;
  return OB_SUCCESS;
}

int ObExpr::get_native_i64_lane(ObEvalCtx &ctx, NativeI64Lane &lane) const
{
  bool native = false;
  int ret = get_native_i64_mode(ctx, native);
  if (OB_SUCC(ret) && !native) { ret = OB_STATE_NOT_MATCH; }
  if (OB_SUCC(ret)) {
    char *frame = ctx.frames_[frame_idx_];
    const NativeI64ExprLayout &layout = *native_i64_layout_;
    lane = {reinterpret_cast<int64_t *>(frame + layout.value_off),
            reinterpret_cast<uint8_t *>(frame + layout.state_off), layout.capacity, is_batch_result()};
  }
  return ret;
}

namespace
{
// An EQ operand is either a borrowed scan lane or a single read of the compact
// parameter owner. No generic descriptor or projection is created here.
struct NativeI64Operand
{
  NativeI64Lane lane_{};
  int64_t scalar_ = 0;
  Datum::State scalar_state_ = Datum::State::Null;
  bool parameter_ = false;

  int prepare(const ObExpr &expr, ObEvalCtx &ctx)
  {
    int ret = OB_SUCCESS;
    if (expr.native_parameter_alias_) {
      const ObPhysicalPlanCtx *plan = ctx.exec_ctx_.get_physical_plan_ctx();
      if (expr.type_ != T_QUESTIONMARK || expr.is_batch_result() || expr.eval_func_ != nullptr
          || plan == nullptr
          || !plan->read_native_bigint_parameter(expr.extra_, scalar_, scalar_state_)) {
        ret = OB_ERR_UNEXPECTED;
      } else { parameter_ = true; }
    } else if (expr.type_ != T_REF_COLUMN || expr.sql_type_ == nullptr
               || expr.get_sql_type().kind() != ObIntType || expr.arg_cnt_ != 0) {
      ret = OB_ERR_UNEXPECTED;
    } else { ret = expr.get_native_i64_lane(ctx, lane_); }
    return ret;
  }

  int read(int64_t row, int64_t &value, Datum::State &state) const
  {
    const int64_t index = lane_.batch ? row : 0;
    if (row < 0 || (!parameter_ && index >= lane_.capacity)) { return OB_INVALID_ARGUMENT; }
    const Datum::State actual = parameter_ ? scalar_state_ : static_cast<Datum::State>(lane_.states[index]);
    if (actual != Datum::State::Null && actual != Datum::State::Present) { return OB_ERR_UNEXPECTED; }
    if (actual == Datum::State::Present) { value = parameter_ ? scalar_ : lane_.values[index]; }
    state = actual;
    return OB_SUCCESS;
  }
};

int read_native_eq(const NativeI64Operand &left, const NativeI64Operand &right,
                   int64_t row, int64_t &value, Datum::State &state)
{
  int64_t l = 0, r = 0;
  Datum::State ls = Datum::State::Null, rs = Datum::State::Null;
  int ret = left.read(row, l, ls);
  if (OB_SUCC(ret)) { ret = right.read(row, r, rs); }
  if (OB_SUCC(ret)) {
    state = ls == Datum::State::Null || rs == Datum::State::Null
        ? Datum::State::Null : Datum::State::Present;
    if (state == Datum::State::Present) { value = l == r ? 1 : 0; }
  }
  return ret;
}
}

int ObExpr::eval_i64(ObEvalCtx &ctx, int64_t &value, Datum::State &state) const
{
  int ret = OB_SUCCESS;
  int64_t candidate = 0;
  Datum::State candidate_state = Datum::State::Null;
  bool native = false;
  if (native_parameter_alias_) {
    NativeI64Operand operand;
    if (OB_FAIL(operand.prepare(*this, ctx))) {
    } else { ret = operand.read(0, candidate, candidate_state); }
  } else if (OB_FAIL(get_native_i64_mode(ctx, native))) {
  } else if (!native) {
    Datum *datum = nullptr;
    if (sql_type_ == nullptr || !ob_is_int_tc(get_sql_type().kind())) { ret = OB_INVALID_ARGUMENT; }
    else if (OB_FAIL(eval(ctx, datum))) {
    } else if (datum == nullptr || (!datum->is_null() && !datum->is_present())) { ret = OB_ERR_UNEXPECTED; }
    else {
      candidate_state = datum->state();
      if (candidate_state == Datum::State::Present) { candidate = datum->get_int(); }
    }
  } else {
    NativeI64Lane lane{};
    if (OB_FAIL(get_native_i64_lane(ctx, lane))) {
    } else {
      const int64_t row = is_batch_result() ? ctx.get_batch_idx() : 0;
      if (row < 0 || row >= lane.capacity) { ret = OB_INVALID_ARGUMENT; }
      else if (type_ == T_REF_COLUMN) {
        candidate_state = static_cast<Datum::State>(lane.states[row]);
        if (candidate_state == Datum::State::Present) { candidate = lane.values[row]; }
        else if (candidate_state != Datum::State::Null) { ret = OB_ERR_UNEXPECTED; }
      } else if (type_ != T_OP_EQ || arg_cnt_ != 2 || args_ == nullptr) { ret = OB_ERR_UNEXPECTED; }
      else {
        ObEvalInfo &info = get_eval_info(ctx);
        ObBitVector *flags = is_batch_result() ? &get_evaluated_flags(ctx) : nullptr;
        if (!info.projected_ && (!info.evaluated_ || (flags != nullptr && !flags->at(row)))) {
          NativeI64Operand left, right;
          if (OB_ISNULL(args_[0]) || OB_ISNULL(args_[1])) { ret = OB_ERR_UNEXPECTED; }
          else if (OB_FAIL(left.prepare(*args_[0], ctx))) {
          } else if (OB_FAIL(right.prepare(*args_[1], ctx))) {
          } else if (OB_FAIL(read_native_eq(left, right, row, candidate, candidate_state))) {
          } else {
            const int64_t size = is_batch_result()
                ? (ctx.get_batch_size() > 0 ? ctx.get_batch_size() : lane.capacity) : 1;
            if (size > lane.capacity) { ret = OB_INVALID_ARGUMENT; }
            else {
              if (!info.evaluated_) {
                if (flags != nullptr) { flags->reset(size); }
                MEMSET(lane.states, static_cast<uint8_t>(Datum::State::Null), size);
                info.cnt_ = size; info.point_to_frame_ = true; info.notnull_ = false;
              }
              if (candidate_state == Datum::State::Present) { lane.values[row] = candidate; }
              lane.states[row] = static_cast<uint8_t>(candidate_state);
              if (flags != nullptr) { flags->set(row); }
              info.evaluated_ = true;
            }
          }
        } else {
          candidate_state = static_cast<Datum::State>(lane.states[row]);
          if (candidate_state == Datum::State::Present) { candidate = lane.values[row]; }
          else if (candidate_state != Datum::State::Null) { ret = OB_ERR_UNEXPECTED; }
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (candidate_state == Datum::State::Present) { value = candidate; }
    state = candidate_state;
  }
  return ret;
}

int ObExpr::do_eval_native_i64_batch(ObEvalCtx &ctx, const ObBitVector &skip, int64_t size) const
{
  int ret = OB_SUCCESS;
  NativeI64Lane lane{};
  if (size < 0) { ret = OB_INVALID_ARGUMENT; }
  else if (OB_FAIL(get_native_i64_lane(ctx, lane))) {
  } else if (!is_batch_result()) {
    int64_t value = 0; Datum::State state = Datum::State::Null;
    ret = eval_i64(ctx, value, state);
  } else if (size > lane.capacity) { ret = OB_INVALID_ARGUMENT; }
  else if (type_ == T_REF_COLUMN || get_eval_info(ctx).projected_) {
    // Scan producers publish these values and their flags directly.
  } else if (type_ != T_OP_EQ || arg_cnt_ != 2 || args_ == nullptr
             || args_[0] == nullptr || args_[1] == nullptr) { ret = OB_ERR_UNEXPECTED; }
  else {
    NativeI64Operand left, right;
    if (OB_FAIL(left.prepare(*args_[0], ctx))) {
    } else if (OB_FAIL(right.prepare(*args_[1], ctx))) {
    } else {
      ObEvalInfo &info = get_eval_info(ctx);
      ObBitVector &flags = get_evaluated_flags(ctx);
      if (!info.evaluated_) {
        flags.reset(size);
        MEMSET(lane.states, static_cast<uint8_t>(Datum::State::Null), size);
        info.evaluated_ = true; info.point_to_frame_ = true; info.notnull_ = false; info.cnt_ = size;
      }
      for (int64_t row = 0; OB_SUCC(ret) && row < size; ++row) {
        if (!skip.at(row) && !flags.at(row)) {
          int64_t value = 0; Datum::State state = Datum::State::Null;
          if (OB_FAIL(read_native_eq(left, right, row, value, state))) {
          } else {
            if (state == Datum::State::Present) { lane.values[row] = value; }
            lane.states[row] = static_cast<uint8_t>(state);
            flags.set(row);
          }
        }
      }
      if (OB_FAIL(ret)) {
        MEMSET(lane.states, static_cast<uint8_t>(Datum::State::Null), size);
        flags.reset(size); info.evaluated_ = false;
      }
    }
  }
  return ret;
}

int ObExpr::get_generic_frame_value_capacity(uint32_t &out) const
{
  uint32_t capacity = 1;
  if (is_batch_result()) {
    if (pvt_skip_off_ <= datum_off_
        || (pvt_skip_off_ - datum_off_) % sizeof(Datum) != 0) {
      return OB_INVALID_ARGUMENT;
    }
    capacity = static_cast<uint32_t>((pvt_skip_off_ - datum_off_) / sizeof(Datum));
  }
  out = capacity;
  return OB_SUCCESS;
}

int ObExpr::get_frame_value_capacity(uint32_t &out) const
{
  if (native_i64_layout_ != nullptr) {
    if (native_i64_layout_->capacity == 0) { return OB_ERR_UNEXPECTED; }
    out = native_i64_layout_->capacity;
    return OB_SUCCESS;
  }
  return get_generic_frame_value_capacity(out);
}

int ObExpr::read_value(ObEvalCtx &ctx, int64_t row, Datum &caller_value) const
{
  if (row < 0) { return OB_INVALID_ARGUMENT; }
  if (native_parameter_alias_) {
    const ObPhysicalPlanCtx *plan = ctx.exec_ctx_.get_physical_plan_ctx();
    if (type_ != T_QUESTIONMARK || is_batch_result() || eval_func_ != nullptr
        || plan == nullptr || !plan->native_parameters_ready()) { return OB_INVALID_ARGUMENT; }
    return plan->read_native_parameter(extra_, caller_value);
  }
  bool native = false;
  int ret = get_native_i64_mode(ctx, native);
  Datum candidate;
  if (OB_SUCC(ret) && native) {
    NativeI64Lane lane{};
    if (OB_FAIL(get_native_i64_lane(ctx, lane))) {
    } else {
      const int64_t index = lane.batch ? row : 0;
      if (index >= lane.capacity) { ret = OB_INVALID_ARGUMENT; }
      else if (lane.states[index] == static_cast<uint8_t>(Datum::State::Null)) { candidate.set_null(); }
      else if (lane.states[index] == static_cast<uint8_t>(Datum::State::Present)) { candidate.set_int(lane.values[index]); }
      else { ret = OB_ERR_UNEXPECTED; }
    }
  } else if (OB_SUCC(ret)) {
    uint32_t capacity = 0;
    if (OB_FAIL(get_frame_value_capacity(capacity))) {
    } else if (is_batch_result() && row >= capacity) { ret = OB_INVALID_ARGUMENT; }
    else { candidate = locate_expr_datum(ctx, row); }
  }
  if (OB_SUCC(ret)) { caller_value = candidate; }
  return ret;
}

int ObExpr::eval_value(ObEvalCtx &ctx, Datum &caller_value) const
{
  if (native_parameter_alias_) { return read_value(ctx, 0, caller_value); }
  bool native = false;
  int ret = get_native_i64_mode(ctx, native);
  Datum candidate;
  if (OB_SUCC(ret) && native) {
    int64_t value = 0; Datum::State state = Datum::State::Null;
    if (OB_FAIL(eval_i64(ctx, value, state))) {
    } else if (state == Datum::State::Null) { candidate.set_null(); }
    else { candidate.set_int(value); }
  } else if (OB_SUCC(ret)) {
    Datum *value = nullptr;
    if (OB_FAIL(eval(ctx, value))) {
    } else if (value == nullptr) { ret = OB_ERR_UNEXPECTED; }
    else { candidate = *value; }
  }
  if (OB_SUCC(ret)) { caller_value = candidate; }
  return ret;
}

int ObExpr::eval_native_parameter(ObEvalCtx &ctx, Datum *&datum) const
{
  ObPhysicalPlanCtx *plan = ctx.exec_ctx_.get_physical_plan_ctx();
  if (!native_parameter_alias_ || type_ != T_QUESTIONMARK || is_batch_result()
      || eval_func_ != nullptr || plan == nullptr || !plan->native_parameters_ready()) {
    return OB_INVALID_ARGUMENT;
  }
  return plan->materialize_parameter_for_expr(extra_, datum);
}

int ObExpr::prepare_native_parameter_write(ObEvalCtx &ctx, int64_t parameter_index) const
{
  ObPhysicalPlanCtx *plan = ctx.exec_ctx_.get_physical_plan_ctx();
  int64_t page = 0, slot = 0;
  if (plan == nullptr || parameter_index < 0 || extra_ != static_cast<uint64_t>(parameter_index)) {
    return OB_INVALID_ARGUMENT;
  }
  ScalarFrameLayout::locate_parameter(plan->get_original_param_cnt(), parameter_index, page, slot);
  if (datum_off_ != ScalarFrameLayout::datum_offset(slot)
      || eval_info_off_ != ScalarFrameLayout::eval_info_offset(slot)) { return OB_INVALID_ARGUMENT; }
  // CG/PX validates the logical frame identity. A copied EvalCtx directory may
  // retain an older page after rebind, so the current owner/index is authoritative.
  Datum *value = nullptr;
  return eval_native_parameter(ctx, value);
}

Datum &ObExpr::locate_native_parameter(ObEvalCtx &ctx) const
{
  ObPhysicalPlanCtx *plan = ctx.exec_ctx_.get_physical_plan_ctx();
  Datum *value = plan == nullptr ? nullptr : plan->locate_materialized_parameter(extra_);
  // This non-fallible read never allocates. The actual generic consumer must
  // have evaluated first; dynamic/PL writers are not native parameter aliases.
  OB_ASSERT(native_parameter_alias_ && value != nullptr);
  return *value;
}

ObEvalInfo &ObExpr::native_parameter_eval_info(ObEvalCtx &ctx) const
{
  ObPhysicalPlanCtx *plan = ctx.exec_ctx_.get_physical_plan_ctx();
  ObEvalInfo *info = plan == nullptr ? nullptr : plan->get_parameter_eval_info(extra_);
  OB_ASSERT(native_parameter_alias_ && info != nullptr);
  return *info;
}

int ObExpr::assign_native_parameter(ObEvalCtx &ctx, const Datum &value) const
{
  ObPhysicalPlanCtx *plan = ctx.exec_ctx_.get_physical_plan_ctx();
  if (plan == nullptr || !native_parameter_alias_) { return OB_INVALID_ARGUMENT; }
  const SqlType *type = plan->get_native_parameter_type(extra_);
  const DatumParameterMetadata *metadata = plan->get_parameter_metadata(extra_);
  if (type == nullptr || metadata == nullptr) { return OB_INVALID_ARGUMENT; }
  return plan->set_runtime_parameter(extra_, value, *type, *metadata);
}

int capture_datum_parameter(const ObExpr &expr, ObEvalCtx &ctx,
    NativeTypedDatum &value, DatumParameterMetadata &attributes)
{
  if (expr.native_parameter_alias_) {
    const ObPhysicalPlanCtx *plan = ctx.exec_ctx_.get_physical_plan_ctx();
    return plan == nullptr ? OB_INVALID_ARGUMENT : plan->capture_native_parameter(expr.extra_, value, attributes);
  }
  const SqlType &type = expr.get_sql_type();
  NativeTypedDatum candidate;
  candidate.type_ = &type;
  int64_t span = 0;
  int ret = OB_SUCCESS;
  if (expr.native_i64_layout_ != nullptr) {
    ret = expr.read_value(ctx, ctx.get_batch_idx(), candidate.datum_);
  } else { candidate.datum_ = expr.locate_expr_datum(ctx); }
  if (OB_SUCC(ret)) { ret = DatumCopyPlan(type).carrier_span_size(candidate.datum_, span); }
  if (OB_SUCC(ret)) {
    DatumParameterMetadata metadata;
    metadata.type_meta_ = expr.obj_meta_;
    if (type.legacy_meta().is_pl_extend_type()) {
      // Declaration identity uses the whole word, including for typed NULL.
      metadata.accuracy_ = type.accuracy();
    } else {
      metadata.accuracy_.set_scale(expr.datum_meta_.scale_);
      metadata.accuracy_.set_precision(expr.datum_meta_.precision_);
      metadata.accuracy_.set_length(expr.max_length_);
      if (metadata.type_meta_.is_string_or_lob_locator_type()) {
        metadata.accuracy_.set_length_semantics(expr.datum_meta_.length_semantics_);
      } else if (metadata.type_meta_.is_numeric_type() || metadata.type_meta_.is_temporal_type()) {
        metadata.type_meta_.set_scale(expr.datum_meta_.scale_);
      }
    }
    metadata.param_meta_ = metadata.type_meta_;
    if (candidate.datum_.is_min() || candidate.datum_.is_max() || candidate.datum_.is_nop()) {
      metadata.type_meta_.set_ext();
    }
    value = candidate;
    attributes = metadata;
  }
  return ret;
}

int capture_datum_parameter(const ObExpr &expr, ObEvalCtx &ctx, DatumParameter &value)
{
  NativeTypedDatum borrowed;
  DatumParameterMetadata attributes;
  int ret = capture_datum_parameter(expr, ctx, borrowed, attributes);
  alignas(8) char scratch[64];
  ObDatum legacy;
  DatumParameter candidate;
  if (OB_SUCC(ret) && OB_FAIL(DatumCopyPlan(*borrowed.type_).legacy_view(
      borrowed.datum_, legacy, scratch, sizeof(scratch)))) {
  } else if (OB_SUCC(ret)) {
    // Inline/state/normalized Decimal exports may point into this stack buffer.
    // Materialize only such reference bytes; ordinary references keep the
    // original frame owner, and generic ordinary scalars import inline.
    const uintptr_t address = reinterpret_cast<uintptr_t>(legacy.ptr_);
    const uintptr_t begin = reinterpret_cast<uintptr_t>(scratch);
    if (address >= begin && address < begin + sizeof(scratch)) {
      ret = candidate.assign_legacy_owned(legacy, attributes.type_meta_, ctx.exec_ctx_.get_allocator());
    } else { ret = candidate.assign_legacy_borrowed(legacy, attributes.type_meta_); }
    if (OB_SUCC(ret)) { candidate.set_metadata(attributes); value = candidate; }
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
    if (OB_FAIL(input.assign_legacy_borrowed(source, meta))) {
    } else {
      input.set_accuracy(accuracy);
      if (OB_FAIL(DatumParameterArray::clone_parameter(input, copy, allocator))) {
      } else { ret = copy.copy_legacy(owned, allocator); }
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
  DatumParameter owned;
  int ret = source.is_ext_sql_array() && !source.is_null()
      ? DatumParameterArray::clone_parameter(source, owned, allocator)
      : owned.deep_copy(source, allocator);
  // This value-only PL boundary preserves the destination's declaration and
  // attributes; only the current value/current physical metadata are replaced.
  if (OB_SUCC(ret)) { destination.assign_value(owned); }
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
    DatumValue value;
    SqlTypeMeta physical_meta = e_.obj_meta_;
    if (d_.is_min() || d_.is_max() || d_.is_nop()) { physical_meta.set_ext(); }
    if (OB_SUCC(ret = value.assign_legacy_borrowed(legacy, physical_meta))) {
      ret = print_datum_debug_json(value, buf, buf_len, pos);
    }
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
    Datum value;
    Datum *datum = nullptr;
    const int ret = e_.native_i64_layout_ == nullptr ? e_.eval(c_, datum) : e_.eval_value(c_, value);
    if (ret == OB_SUCCESS && e_.native_i64_layout_ != nullptr) { datum = &value; }
    if (ret != OB_SUCCESS || datum == nullptr) {
      databuff_printf(buf, buf_len, pos, "{expr_eval_error:%d}",
          ret != OB_SUCCESS ? ret : OB_ERR_UNEXPECTED);
    } else {
      pos = ObToStringDatum(e_, *datum).to_string(buf, buf_len);
    }
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
  } else if (!plan_ctx->native_parameters_ready()) {
    // PL variables remain in their live PL owner, outside SQL parameter pages.
    // Do not admit an arbitrary SQL construction directory through this path.
    ObSQLSessionInfo *session = ctx.exec_ctx_.get_my_session();
    pl::ObPLContext *pl_context = session == nullptr ? nullptr : session->get_pl_context();
    pl::ObPLExecState *state = pl_context == nullptr ? nullptr : pl_context->get_current_state();
    if (state == nullptr || &state->get_physical_plan_ctx() != plan_ctx
        || state->get_exec_ctx().exec_ctx_ != &ctx.exec_ctx_
        || expr.extra_ < 0 || expr.extra_ >= state->get_params().count()) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      const DatumParameter &value = state->get_params().at(expr.extra_);
      if (!value.is_null() && value.get_type() != expr.datum_meta_.type_) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        alignas(8) char scratch[64];
        ObDatum legacy;
        Datum candidate;
        if (OB_FAIL(value.export_legacy(legacy, scratch, sizeof(scratch)))) {
        } else if (OB_FAIL(expr.get_datum_copy_plan().import_legacy(legacy, candidate))) {
        } else if (is_lob_storage(value.get_type())
            && OB_FAIL(ob_adjust_lob_datum(ctx.exec_ctx_, candidate, value.get_meta(),
                expr.obj_meta_, ctx.exec_ctx_.get_allocator(), candidate))) {
        } else if (OB_FAIL(expr.deep_copy_datum(ctx, candidate))) {
        } else {
          expr_datum = expr.locate_expr_datum(ctx);
        }
      }
    }
  } else {
    Datum captured;
    const Datum *value = &captured;
    const SqlType *type = plan_ctx->get_native_parameter_type(expr.extra_);
    if (type == nullptr) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(plan_ctx->read_native_parameter(expr.extra_, captured))) {
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
    const ObPhysicalPlanCtx &parameters = *ctx.exec_ctx_.get_physical_plan_ctx();
    Datum captured;
    const Datum *parameter = &captured;
    const SqlType *parameter_type = parameters.get_native_parameter_type(expr.extra_);
    const DatumParameterMetadata *attributes = parameters.get_parameter_metadata(expr.extra_);
    if (parameter_type == nullptr || attributes == nullptr) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(parameters.read_native_parameter(expr.extra_, captured))) {
    } else if (OB_FAIL(ObSQLUtils::get_default_cast_mode(
                       session->get_stmt_type(), session, cast_mode))) {
    } else {
      const common::SqlTypeMeta dst_meta = expr.obj_meta_;
      ObIAllocator &allocator = ctx.exec_ctx_.get_allocator();
      const ObDataTypeCastParams dtc_params = ObBasicSessionInfo::create_dtc_params(session);
      cast_mode = (cast_mode | CM_COLUMN_CONVERT);
      if (ctx.exec_ctx_.get_physical_plan_ctx()->is_ignore_stmt()) {
        cast_mode = cast_mode | CM_WARN_ON_FAIL | CM_CHARSET_CONVERT_IGNORE_ERR;
      }
      if (dst_meta.is_collection_sql_type()) {
        // A collection assignment needs the executed column declaration, not
        // only ObCollectionSQLType. The common scalar caster has no such port.
        alignas(8) char source_scratch[64], result_scratch[64];
        ObDatum source_view, result_view;
        DatumParameter source, converted;
        ObExprResType destination;
        static_cast<SqlTypeMeta &>(destination) = dst_meta;
        destination.set_accuracy(expr.get_sql_type().accuracy());
        destination.set_length(expr.max_length_);
        ObDatumCaster caster;
        if (OB_FAIL(DatumCopyPlan(*parameter_type).legacy_view(
            *parameter, source_view, source_scratch, sizeof(source_scratch)))) {
        } else if (OB_FAIL(source.assign_legacy_borrowed(source_view, attributes->type_meta_))) {
        } else {
          source.set_metadata(*attributes);
          if (OB_FAIL(caster.init(ctx.exec_ctx_, false))) {
          } else if (OB_FAIL(caster.to_value(source, destination, cast_mode, allocator, converted))) {
          } else if (OB_FAIL(converted.export_legacy(result_view, result_scratch, sizeof(result_scratch)))) {
          } else { ret = expr.get_datum_copy_plan().import_legacy(result_view, expr_datum); }
        }
        return ret;
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
      alignas(8) char source_scratch[64], result_scratch[64];
      ObDatum source_view, result_view;
      DatumValue source, converted;
      if (OB_FAIL(DatumCopyPlan(*parameter_type).legacy_view(
          *parameter, source_view, source_scratch, sizeof(source_scratch)))) {
      } else if (OB_FAIL(source.assign_legacy_borrowed(source_view, attributes->type_meta_))) {
      } else if (OB_FAIL(DatumValueCaster::to_type(dst_meta.get_type(), cast_ctx,
                                             source, allocator, converted))) {
      } else if (OB_FAIL(converted.export_legacy(result_view, result_scratch, sizeof(result_scratch)))) {
      } else { ret = expr.get_datum_copy_plan().import_legacy(result_view, expr_datum); }
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
