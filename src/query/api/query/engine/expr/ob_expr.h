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

#ifndef OCEANBASE_EXPR_OB_EXPR_H_
#define OCEANBASE_EXPR_OB_EXPR_H_

#include "lib/container/ob_2d_array.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/oblog/ob_log.h"
#include "common/datum/ob_datum.h"
#include "common/datum/datum.h"
#include "common/datum/datum_copy_plan.h"
#include "common/datum/datum_parameter.h"
#include "common/datum/native_typed_datum.h"
#include "common/datum/datum_param_store.h"
#include "share/datum/ob_datum_funcs.h"
#include "query/engine/ob_serializable_function.h"
#include "query/parser/ob_item_type.h"
#include "lib/utility/ob_common_utility.h"
#include "share/vector/ob_eval_bound.h"

namespace oceanbase
{
namespace pl { class ObPLINS; }
namespace share { namespace schema { class ObSchemaGetterGuard; } }
namespace common
{
 class DatumParameter;
 class DatumCmpOp;
 class DatumHashOp;
 struct DatumBasicFuncs;
}

namespace storage
{
  // forward declaration for friends
  class ObVectorStore;
}
namespace sql
{

using ObExprHashFuncType = common::DatumHashOp;
using ObExprCmpFuncType = common::DatumCmpOp;
using ObBatchDatumHashFunc = common::DatumHashOp;
using NullSafeRowCmpFunc = common::NullSafeRowCmpFunc;
typedef int (*RowCmpFunc) (const common::SqlTypeMeta &l_meta, const common::SqlTypeMeta &r_meta,
                           const void *l_data, const int32_t l_len,
                           const void *r_data, const int32_t r_len,
                           int &cmp_ret,
                           const common::ObDatumAccessContext *access_ctx);

class ObExecContext;
class ObRawExprResType;
class ObSQLSessionInfo;
struct ObIExprExtraInfo;
class ObDatumCaster;
using common::Datum;
// Query's batch view uses native values. Storage keeps its explicit legacy view.
struct DatumVector
{
  common::Datum *at(int64_t index) const { return datums_ + (mask_ & index); }
  void set_batch(bool batch) { mask_ = batch ? UINT64_MAX : 0; }
  bool is_batch() const { return mask_ == UINT64_MAX; }
  TO_STRING_KV(KP(datums_), K(mask_));
  common::Datum *datums_ = nullptr;
  uint64_t mask_ = 0;
};
class ObSubQueryIterator;

typedef ObItemType ObExprOperatorType;

struct ObDatumMeta
{
  OB_UNIS_VERSION(1);
public:
  ObDatumMeta() : type_(common::ObNullType), cs_type_(common::CS_TYPE_INVALID)
                  , scale_(-1), precision_(-1)
  {
  }
  ObDatumMeta(const common::SqlValueType type,
              const common::ObCollationType cs_type,
              const int8_t scale)
    : type_(type), cs_type_(cs_type), scale_(scale), precision_(-1)
  {
  }
  ObDatumMeta(const common::SqlValueType type,
              const common::ObCollationType cs_type,
              const int8_t scale,
              const common::ObPrecision prec)
    : type_(type), cs_type_(cs_type), scale_(scale), precision_(prec)
  {
  }

  void reset() { new (this) ObDatumMeta(); }
  TO_STRING_KV(K_(type), K_(cs_type), K_(scale), K_(precision));

  common::SqlValueType type_;
  common::ObCollationType cs_type_;
  int8_t scale_;
  union {
    common::ObPrecision precision_;
    common::ObLengthSemantics length_semantics_;
  };
  OB_INLINE bool is_clob() const
  {
    return false;
  }
  OB_INLINE bool is_varchar() const
  {
    return ((static_cast<uint8_t>(common::ObVarcharType) == type_)
            && (common::CS_TYPE_BINARY != cs_type_));
  }
  OB_INLINE bool is_char() const
  {
    return ((static_cast<uint8_t>(common::ObCharType) == type_)
            && (common::CS_TYPE_BINARY != cs_type_));
  }
  OB_INLINE bool is_binary() const
  {
    return ((static_cast<uint8_t>(common::ObCharType) == type_)
        && (common::CS_TYPE_BINARY == cs_type_));
  }
  OB_INLINE bool is_ext_sql_array() const
  {
    // In the legacy value layout, scale and metadata extension flags share a union,
    //for extend type meta, scale_ means extend flag
    return ((static_cast<uint8_t>(common::ObExtendType) == type_)
            && (common::T_EXT_SQL_ARRAY == static_cast<uint8_t>(scale_)));
  }
  OB_INLINE common::SqlValueType get_type() const { return type_; }
  OB_INLINE uint16_t get_subschema_id() const { return static_cast<uint16_t>(cs_type_); }
  // ObCollectionSQLType reused cs_type as part of sub schema id, always return CS_TYPE_BINARY.
  OB_INLINE ObCollationType get_cs_type() const { return type_ == ObCollectionSQLType ? CS_TYPE_BINARY : cs_type_; }
};

// Expression evaluate result info
struct ObEvalInfo
{
  OB_INLINE void clear_evaluated_flag()
  {
    if (evaluated_ || projected_) {
      evaluated_ = 0;
      projected_ = 0;
    }
  }
  DECLARE_TO_STRING;

	union {
		struct {
			// is already evaluated
			uint16_t evaluated_:1;
			// is projected
			uint16_t projected_:1;
			// all datums are not null.
			// may not set even all datums are not null,
      // e.g.: the null values are filtered by skip bitmap.
			uint16_t notnull_:1;
			// pointer is point to reserved buffer in frame.
			uint16_t point_to_frame_:1;
		};
		uint16_t flag_;
	};
	// result count (set to batch_size in batch_eval)
	uint16_t cnt_;
};

// Generic scalar expression slots and their logical parameter-frame numbering.
// ParameterValuePage owns compact values independently of these legacy offsets.
// Batch columns hold contiguous Datum arrays with one EvalInfo per Expr.
struct ScalarFrameLayout
{
  static constexpr int64_t slot_size()
  {
    return (sizeof(Datum) + sizeof(ObEvalInfo) + alignof(Datum) - 1)
        / alignof(Datum) * alignof(Datum);
  }
  static constexpr int64_t datum_offset(int64_t slot) { return slot * slot_size(); }
  static constexpr int64_t eval_info_offset(int64_t slot)
  { return datum_offset(slot) + sizeof(Datum); }
  static constexpr int64_t slots_per_frame() { return common::MAX_FRAME_SIZE / slot_size(); }
  static constexpr int64_t frame_count(int64_t count)
  { return (count + slots_per_frame() - 1) / slots_per_frame(); }

  // Original parameters and appended parameters occupy separate page ranges,
  // even when the final original page has unused slots. Indices are validated
  // by the callers; frame_idx is relative to the parameter-frame prefix.
  static void locate_parameter(int64_t original_count, int64_t param_idx,
                               int64_t &frame_idx, int64_t &slot_idx)
  {
    const int64_t local_idx = param_idx < original_count ? param_idx : param_idx - original_count;
    frame_idx = local_idx / slots_per_frame();
    slot_idx = local_idx % slots_per_frame();
    if (original_count > 0 && param_idx >= original_count) {
      frame_idx += frame_count(original_count);
    }
  }
};

// expression evaluate context
struct ObEvalCtx
{
  friend struct ObExpr;
  friend class ObOperator;
  friend class ObDASScanOp;
  friend class ObSubPlanFilterOp;
  friend class ObSubQueryIterator;
  friend class oceanbase::storage::ObVectorStore;
  friend class ObDatumCaster;
  class TempAllocGuard
  {
  public:
    TempAllocGuard(ObEvalCtx &eval_ctx) : ctx_(eval_ctx), used_flag_(eval_ctx.tmp_alloc_used_)
    {
      alloc_ = &ctx_.get_reset_tmp_alloc();
      ctx_.tmp_alloc_used_ = true;
    }
    ~TempAllocGuard() { ctx_.tmp_alloc_used_ = used_flag_; }
    common::ObArenaAllocator &get_allocator() { return *alloc_; }
  private:
    ObEvalCtx &ctx_;
    common::ObArenaAllocator *alloc_;
    bool used_flag_;
  };
  class BatchInfoScopeGuard
  {
  public:
    explicit BatchInfoScopeGuard(ObEvalCtx &eval_ctx)
    {
      batch_idx_ptr_ = &eval_ctx.batch_idx_;
      batch_size_ptr_ = &eval_ctx.batch_size_;
      max_batch_size_ptr_ = &eval_ctx.max_batch_size_;
      batch_idx_default_val_ = eval_ctx.batch_idx_;
      batch_size_default_val_ = eval_ctx.batch_size_;
      max_batch_size_default_val_ = eval_ctx.max_batch_size_;
    }
    ~BatchInfoScopeGuard()
    {
       *batch_idx_ptr_ = batch_idx_default_val_;
       *batch_size_ptr_ = batch_size_default_val_;
       *max_batch_size_ptr_ = max_batch_size_default_val_;
    }
    inline void set_batch_idx(int64_t v) { *batch_idx_ptr_ = v; }
    inline void set_batch_size(int64_t v) { *batch_size_ptr_ = v; }
private:
    friend class ObExpr;
    inline void set_max_batch_size(int64_t v) { *max_batch_size_ptr_ = v; }

  private:
    int64_t *batch_idx_ptr_ = nullptr;
    int64_t *batch_size_ptr_ = nullptr;
    int64_t *max_batch_size_ptr_ = nullptr;
    int64_t batch_idx_default_val_ = 0;
    int64_t batch_size_default_val_ = 0;
    int64_t max_batch_size_default_val_ = 0;
  };
  explicit ObEvalCtx(ObExecContext &exec_ctx, ObIAllocator *allocator = NULL);
  explicit ObEvalCtx(ObEvalCtx &eval_ctx);
  virtual ~ObEvalCtx();

  OB_INLINE int64_t get_batch_idx() { return batch_idx_; }
  OB_INLINE int64_t get_batch_size() { return batch_size_; }
  OB_INLINE bool is_vectorized() const { return 0 != max_batch_size_; }
  OB_INLINE ObArenaAllocator &get_expr_res_alloc() { return expr_res_alloc_; }
  OB_INLINE void reuse(const int64_t batch_size)
  {
    batch_idx_ = 0;
    batch_size_ = batch_size;
  }

  OB_INLINE void set_max_batch_size(const int64_t max_batch_size)
  {
    max_batch_size_ = max_batch_size;
  }
  int init_datum_caster();
  int get_datum_access_ctx(
      const common::ObDatumAccessContext *&datum_access_ctx);

  TO_STRING_KV(K_(batch_idx), K_(batch_size), K_(max_batch_size), KP(frames_));
private:
  // Allocate expression result memory.
  void *alloc_expr_res(const int64_t size) { return expr_res_alloc_.alloc(size); }

  // set current row evaluating in the batch
  OB_INLINE void set_batch_idx(const int64_t batch_idx)
  {
    // NOTICE: NEVER USE this routine. Use ObEvalCtx::BatchInfoScopeGuard to
    // protect and update batch_idx_. The reason this API is still here is
    // storage layer WON'T use ObEvalCtx::BatchInfoScopeGuard
    batch_idx_ = batch_idx;
  }
  // Note: use set_batch_size() for performance reason.
  // For other path, use ObEvalCtx::BatchInfoScopeGuard instead to protect and
  // update batch_size_.
  OB_INLINE void set_batch_size(const int64_t batch_size)
  {
    batch_size_ = batch_size;
  }
  //change to private interface, please use TempAllocGuard::get_allocator() instead
  common::ObArenaAllocator &get_reset_tmp_alloc()
  {
#ifndef NDEBUG
    if (!tmp_alloc_used_) {
      tmp_alloc_.reset();
    }
#else
    if (!tmp_alloc_used_ && tmp_alloc_.used() > common::OB_MALLOC_MIDDLE_BLOCK_SIZE) {
      tmp_alloc_.reset_remain_one_page();
    }
#endif
    return tmp_alloc_;
  }

public:
  char **frames_;
  // Used for das, the semantics is the same as max_batch_size_ in spec
  int64_t max_batch_size_;
  ObExecContext &exec_ctx_;
  // Temporary allocator for expression evaluating, may be reset immediately after ObExpr::eval().
  // Can not use allocator for expression result. (ObExpr::get_str_res_mem() is used for result).
  common::ObArenaAllocator &tmp_alloc_;
  ObDatumCaster *datum_caster_;
  bool &tmp_alloc_used_;
private:
  int64_t batch_idx_;
  int64_t batch_size_;
  // Expression result allocator, never reset.
  common::ObArenaAllocator &expr_res_alloc_;
};




// Expression frames reserve a 16-byte header on every target, including wasm32.
struct alignas(8) ObDynReserveBuf
{
  static const uint32_t MAGIC_NUM = 0xD928e5bf;
  static bool supported(const common::SqlValueType &type)
  {
    const common::SqlTypeClass tc = common::sql_type_class(type);
    return common::ObStringTC == tc || common::ObTextTC == tc
           || common::ObRawTC == tc
           || common::ObRowIDTC == tc
           || common::ObLobTC == tc
           || common::ObJsonTC == tc
           || common::ObGeometryTC == tc
           || common::ObUserDefinedSQLTC == tc
           || common::ObCollectionSQLTC == tc
           || common::ObEnumSetInnerTC == tc;
  }

  ObDynReserveBuf() = default;

  uint32_t magic_;
  uint32_t len_;
  char *mem_;
};
static_assert(16 == sizeof(ObDynReserveBuf), "ObDynReserveBuf size can not be changed");


typedef common::ObFixedArray<common::ObString, common::ObIAllocator> ObStrValues;

#define BATCH_EVAL_FUNC_ARG_DECL const ObExpr &expr, ObEvalCtx &ctx, \
  const ObBitVector &skip, const int64_t size
#define BATCH_EVAL_FUNC_ARG_LIST expr, ctx, skip, size

#define EVAL_FUNC_ARG_DECL const ObExpr &expr, ObEvalCtx &ctx, Datum &expr_datum
#define EVAL_FUNC_ARG_LIST expr, ctx, expr_datum



// default evaluate batch function which call eval() for every datum of batch.
extern int expr_default_eval_batch_func(BATCH_EVAL_FUNC_ARG_DECL);

// index number for args_ in ObExpr
extern const int64_t ARGS_IDX_ZERO;
extern const int64_t ARGS_IDX_ONE;
extern const int64_t ARGS_IDX_TWO;
extern const int64_t ARGS_IDX_THREE;
extern const int64_t ARGS_IDX_FOUR;
extern const int64_t ARGS_IDX_FIVE;

// Plan-owned offsets for the compact alternative. The ordinary ObExpr offsets
// continue to describe its real Datum16 fallback. Neither owns execution data.
struct NativeI64ExprLayout
{
  uint32_t value_off;
  uint32_t state_off;
  uint32_t eval_info_off;
  uint32_t eval_flags_off;
  uint32_t pvt_skip_off;
  uint32_t capacity;
};

struct NativeI64Lane
{
  int64_t *values;
  uint8_t *states;
  uint32_t capacity;
  bool batch;
};

// Only an Expr with an explicit prepared descriptor may inspect this header.
// Ordinary frame payload bytes are never probed for a magic value.
struct alignas(8) NativeI64FrameHeader
{
  static constexpr uint32_t MAGIC = 0x49363446;
  static constexpr uint16_t VERSION = 1;
  uint32_t magic;
  uint16_t version;
  uint8_t compact;
  uint8_t reserved;
};
static_assert(sizeof(NativeI64FrameHeader) == 8, "candidate frame header ABI");
static_assert(sizeof(NativeI64ExprLayout) == 24, "prepared I64 lane offsets");

struct ObExpr
{
  OB_UNIS_VERSION(2);
public:
  friend class ObOperator;

  const static uint32_t INVALID_EXP_CTX_ID = UINT32_MAX;

  ObExpr();
  // Rebuild derived function tables after datum_meta_ or obj_meta_ changes.
  int init_runtime_functions(common::ObIAllocator &allocator);
  int bind_sql_type(const common::SqlType &type, common::ObIAllocator &owner);
  int prepare_scalar_type(const common::SqlTypeMeta &meta, const common::ObAccuracy &accuracy,
                           common::ObIAllocator &owner);
  int prepare_resolved_type(const ObRawExprResType &resolved, const ObSQLSessionInfo *session,
                            common::ObIAllocator &owner,
                            share::schema::ObSchemaGetterGuard *schema_guard = nullptr,
                            const pl::ObPLINS *pl_declarations = nullptr);
  int clone_prepared_metadata_from(const ObExpr &source, common::ObIAllocator &owner);
  int serialize_native_metadata(char *buffer, int64_t length, int64_t &position) const;
  int deserialize_native_metadata(const char *buffer, int64_t length, int64_t &position,
                                   common::ObIAllocator &owner);
  int64_t native_metadata_serialize_size() const;
  static int clone_native_value(const common::SqlType &type, const common::Datum &source,
                                common::ObIAllocator &owner, common::Datum &destination);

  struct CmpSpec
  {
    const common::SqlType *left_type_ = nullptr;
    const common::SqlType *right_type_ = nullptr;
    common::ObCmpNullPos null_pos_ = common::NULL_LAST;
    common::ObCollationType collation_ = common::CS_TYPE_INVALID;
  };
  int prepare_cmp_ops(common::ObIAllocator &allocator, int64_t count);
  int configure_cmp_op(int64_t index, int64_t left_arg, int64_t right_arg,
                       common::ObCmpNullPos null_pos, common::ObCollationType collation);
  int configure_cmp_op(int64_t index, const common::SqlType &left, const common::SqlType &right,
                       common::ObCmpNullPos null_pos, common::ObCollationType collation);
  const common::DatumCmpOp &get_cmp_op(int64_t index = 0) const;
  int64_t get_cmp_op_count() const { return native_cmp_op_count_; }

  const common::SqlType &get_sql_type() const
  {
    OB_ASSERT(sql_type_ != nullptr);
    return *sql_type_;
  }
  common::DatumCopyPlan get_datum_copy_plan() const
  {
    return common::DatumCopyPlan(get_sql_type());
  }

  // Prepare the existing legacy copy metadata, preserving its accuracy union.
  // The returned plan owns these metadata bytes and borrows no SqlType. Full
  // enum/collection/UDT metadata still belongs to resolved or schema owners.
  common::DatumCopyPlan prepare_legacy_copy_plan() const
  {
    common::ObAccuracy accuracy;
    if (obj_meta_.is_string_or_lob_locator_type()) {
      accuracy.set_length(max_length_);
      accuracy.set_length_semantics(datum_meta_.length_semantics_);
    } else {
      accuracy.set_precision(datum_meta_.precision_);
      accuracy.set_scale(datum_meta_.scale_);
    }
    return common::DatumCopyPlan(obj_meta_, accuracy);
  }

  OB_INLINE int eval(ObEvalCtx &ctx, common::Datum *&datum) const;
  // Checked mode reads preserve their output on failure. A malformed candidate
  // header is an error, never a request to interpret compact bytes as Datum.
  int get_native_i64_mode(ObEvalCtx &ctx, bool &native) const;
  bool uses_native_i64(ObEvalCtx &ctx) const
  {
    bool native = false;
    const int ret = get_native_i64_mode(ctx, native);
    OB_ASSERT(ret == common::OB_SUCCESS);
    return native;
  }
  int get_native_i64_lane(ObEvalCtx &ctx, NativeI64Lane &lane) const;
  // Prepared value-slot bounds, independent of a consumer operator's batch size.
  // These metadata-only queries preserve out on failure. A runtime caller must
  // first validate candidate mode/header with get_native_i64_mode().
  int get_frame_value_capacity(uint32_t &out) const;
  // Always the generic Datum16 offsets, even if a native descriptor exists.
  // Frame allocation and wire validation use this same extent calculation.
  int get_generic_frame_value_capacity(uint32_t &out) const;
  int eval_i64(ObEvalCtx &ctx, int64_t &value, common::Datum::State &state) const;
  int read_value(ObEvalCtx &ctx, int64_t row, common::Datum &caller_value) const;
  int eval_value(ObEvalCtx &ctx, common::Datum &caller_value) const;
  int eval_native_parameter(ObEvalCtx &ctx, common::Datum *&datum) const;
  common::Datum &locate_native_parameter(ObEvalCtx &ctx) const;
  ObEvalInfo &native_parameter_eval_info(ObEvalCtx &ctx) const;
  int assign_native_parameter(ObEvalCtx &ctx, const common::Datum &value) const;
  int prepare_native_parameter_write(ObEvalCtx &ctx, int64_t parameter_index) const;
  int eval_enumset(ObEvalCtx &ctx,
                   const common::ObIArray<common::ObString> &str_values,
                   const uint64_t cast_mode,
                   common::Datum *&datum) const;

  OB_INLINE int eval_batch(ObEvalCtx &ctx,
                           const ObBitVector &skip,
                           const int64_t size) const;

  void reset() { new (this) ObExpr(); }
  Datum &locate_expr_datum(ObEvalCtx &ctx) const
  {
    if (native_parameter_alias_) { return locate_native_parameter(ctx); }
    OB_ASSERT(native_i64_layout_ == nullptr || !uses_native_i64(ctx));
    // performance critical, do not check pointer validity.
    return reinterpret_cast<Datum *>(ctx.frames_[frame_idx_] + datum_off_)[get_datum_idx(ctx)];
  }

  Datum &locate_expr_datum(ObEvalCtx &ctx, const int64_t batch_idx) const
  {
    if (native_parameter_alias_) { return locate_native_parameter(ctx); }
    OB_ASSERT(native_i64_layout_ == nullptr || !uses_native_i64(ctx));
    int64_t idx = batch_idx_mask_ & batch_idx;
    return reinterpret_cast<Datum *>(ctx.frames_[frame_idx_] + datum_off_)[idx];
  }

  DatumVector locate_expr_datumvector(ObEvalCtx &ctx) const
  {
    OB_ASSERT(native_i64_layout_ == nullptr || !uses_native_i64(ctx));
    DatumVector datumsvector;
    datumsvector.set_batch(is_batch_result());
    datumsvector.datums_ = native_parameter_alias_ ? &locate_native_parameter(ctx)
        : reinterpret_cast<Datum *>(ctx.frames_[frame_idx_] + datum_off_);
    return datumsvector;
  }

  Datum *locate_batch_datums(ObEvalCtx &ctx) const
  {
    OB_ASSERT(native_i64_layout_ == nullptr || !uses_native_i64(ctx));
    return native_parameter_alias_ ? &locate_native_parameter(ctx)
        : reinterpret_cast<Datum *>(ctx.frames_[frame_idx_] + datum_off_);
  }

  // batch version of locate_param_datum.  Array size is batch_size_ in ObEvalCtx
  OB_INLINE DatumVector locate_param_datumvector(ObEvalCtx &ctx, int param_index) const
  {
      return args_[param_index]->locate_expr_datumvector(ctx);
  }

  ObBitVector &get_evaluated_flags(ObEvalCtx &ctx) const
  {
    const uint32_t off = native_i64_layout_ != nullptr && uses_native_i64(ctx)
        ? native_i64_layout_->eval_flags_off : eval_flags_off_;
    return *to_bit_vector(ctx.frames_[frame_idx_] + off);
  }

  ObBitVector &get_pvt_skip(ObEvalCtx &ctx) const
  {
    const uint32_t off = native_i64_layout_ != nullptr && uses_native_i64(ctx)
        ? native_i64_layout_->pvt_skip_off : pvt_skip_off_;
    return *to_bit_vector(ctx.frames_[frame_idx_] + off);
  }

  ObEvalInfo &get_eval_info(ObEvalCtx &ctx) const
  {
    if (native_parameter_alias_) { return native_parameter_eval_info(ctx); }
    const uint32_t off = native_i64_layout_ != nullptr && uses_native_i64(ctx)
        ? native_i64_layout_->eval_info_off : eval_info_off_;
    return *reinterpret_cast<ObEvalInfo *>(ctx.frames_[frame_idx_] + off);
  }

  OB_INLINE char *get_rev_buf(ObEvalCtx &ctx) const
  {
    return ctx.frames_[frame_idx_] + res_buf_off_;
  }

  // Locate the native slot. Its producer publishes the complete value.
  OB_INLINE Datum &locate_datum_for_write(ObEvalCtx &ctx) const;

  // Locate the native batch array; no payload pointer repair is needed.
  inline Datum *locate_datums_for_update(ObEvalCtx &ctx, const int64_t size) const;


  OB_INLINE Datum &locate_param_datum(ObEvalCtx &ctx, int param_index) const
  {
      return args_[param_index]->locate_expr_datum(ctx);
  }

  // Acquire owned output bytes before publishing a reference. Inline producers
  // never call this method; no Datum is changed until its writer succeeds.
  char *get_reference_res_mem(ObEvalCtx &ctx, int64_t size, int64_t idx = -1) const
  {
    char *result = nullptr;
    if (size >= 0 && size <= UINT32_MAX
        && get_sql_type().canonical_representation() == common::DatumRepresentation::Reference) {
      const int64_t row = batch_idx_mask_ & (idx < 0 ? get_datum_idx(ctx) : idx);
      if (size <= res_buf_len_) {
        result = ctx.frames_[frame_idx_] + res_buf_off_ + res_buf_len_ * row;
      } else if (ObDynReserveBuf::supported(datum_meta_.type_)) {
        result = alloc_str_res_mem(ctx, size, row);
      } else {
        result = static_cast<char *>(ctx.alloc_expr_res(size));
      }
    }
    return result;
  }

  // Get result memory for string type.
  // Dynamic allocated memory is allocated if reserved buffer if not enough.
  char *get_str_res_mem(ObEvalCtx &ctx, const int64_t size) const
  {
    return get_str_res_mem(ctx, size, get_datum_idx(ctx));
  }

  char *get_str_res_mem(ObEvalCtx &ctx, const int64_t size, const int64_t datum_idx) const
  {
    //TODO shengle handle continuous format memory
    return OB_LIKELY(size <= res_buf_len_)
        ? ctx.frames_[frame_idx_] + res_buf_off_ + res_buf_len_ * (batch_idx_mask_ & datum_idx)
        : alloc_str_res_mem(ctx, size, datum_idx);
  }

  void cur_str_resvered_buf(ObEvalCtx &ctx, const int64_t datum_idx, char *&buffer, int64_t &len) const
  {
    buffer = nullptr;
    len = 0;
    int64_t idx = batch_idx_mask_ & datum_idx;
    if (OB_UNLIKELY(!ObDynReserveBuf::supported(datum_meta_.type_))) {
      SQL_ENG_LOG_RET(ERROR, common::OB_ERR_UNEXPECTED, "unexpected alloc string result memory called", K(*this));
    } else if (dyn_buf_header_offset_ == 0) {
      // An inline expression may be rebound to a reference result. Its frame
      // contains no ObDynReserveBuf; offset zero is not a valid header.
      if (res_buf_len_ > 0) {
        len = res_buf_len_;
        buffer = ctx.frames_[frame_idx_] + res_buf_off_ + res_buf_len_ * idx;
      }
    } else {
      ObDynReserveBuf *drb = reinterpret_cast<ObDynReserveBuf *>(
        ctx.frames_[frame_idx_] + dyn_buf_header_offset_ + sizeof(ObDynReserveBuf) * idx);
      if (OB_LIKELY(drb->len_ >= res_buf_len_)) {
        buffer = drb->mem_;
        len = drb->len_;
      } else {
        len = res_buf_len_;
        buffer = ctx.frames_[frame_idx_] + res_buf_off_ + res_buf_len_ * idx;
      }
    }
  }

  // Get pre allocate result memory buffer, only used for string result
  void cur_str_resvered_buf(ObEvalCtx &ctx, char *&buffer, int64_t &len) const
  {
    cur_str_resvered_buf(ctx, get_datum_idx(ctx), buffer, len);
  }

  bool is_variable_res_buf() const
  {
    return OBJ_DATUM_STRING == obj_datum_map_;
  };

  inline bool is_const_expr() const { return is_static_const_ || is_dynamic_const_; }

  inline bool is_vector_sort_expr() const {
    return type_ == T_FUN_SYS_L2_DISTANCE ||
           type_ == T_FUN_SYS_L2_SQUARED ||
           type_ == T_FUN_SYS_INNER_PRODUCT ||
           type_ == T_FUN_SYS_NEGATIVE_INNER_PRODUCT ||
           type_ == T_FUN_SYS_COSINE_DISTANCE ||
           type_ == T_FUN_SYS_SEMANTIC_DISTANCE ||
           type_ == T_FUN_SYS_SEMANTIC_VECTOR_DISTANCE;
  }

  inline bool is_semantic_distance_expr() const {
    return type_ == T_FUN_SYS_SEMANTIC_DISTANCE ||
           type_ == T_FUN_SYS_SEMANTIC_VECTOR_DISTANCE;
  }

  inline bool is_semantic_vector_distance_expr() const {
    return type_ == T_FUN_SYS_SEMANTIC_VECTOR_DISTANCE;
  }

  // Evaluate all parameters, assign the first sizeof...(args) parameters to %args.
  //
  // e.g.:
  //  arg_cnt_ = 2;
  //
  //  eval_param_values(ctx):
  //    call: args_[0]->eval(), args_[1]->eval()
  //
  //  eval_param_values(ctx, param0);
  //    call:args_[0]->eval(param0), args_[1]->eval();
  //
  //  eval_param_values(ctx, param0, param1, param2
  //    call: args[0]->eval(param0), args[1]->eval(param1)
  //    keep param2 unchanged.
  template <typename ...TS>
  OB_INLINE int eval_param_value(ObEvalCtx &ctx, TS &...args) const;

  // batch version for eval_param_value
  template <typename ...TS>
  OB_INLINE int eval_batch_param_value(ObEvalCtx &ctx, const ObBitVector &skip,
                                       const int64_t size, TS &...args) const;

  OB_INLINE int deep_copy_self_datum(ObEvalCtx &ctx) const;

  // deep copy %datum to reserve buffer or new allocated buffer if reserved buffer is not enough.
  OB_INLINE int deep_copy_datum(ObEvalCtx &ctx, const common::Datum &datum) const;

  bool is_batch_result() const { return batch_result_; }
  int64_t get_datum_idx(ObEvalCtx &ctx) const
  {
    return batch_idx_mask_ & ctx.get_batch_idx();
  }

  // mark contian = TRUE if it meets the following rules:
  //   - expr itself equal to input expr
  //   - expr's children equals to input expr
  // Note: NO stack-overflow check as it is done when resolve raw expr
  int contain_expr (const ObExpr * expr, bool & is_contain) const
  {
    int ret = OB_SUCCESS;
    if (this == expr) {
      is_contain = true;
    } else {
      if (arg_cnt_ > 0) {
        for (auto i = 0; !is_contain && i < arg_cnt_; i++) {
          if (args_[i] != nullptr) {
            ret = args_[i]->contain_expr(expr, is_contain);
          }
        }
      } else {
        // do nothing
      }
    }
    return ret;
  }
  OB_INLINE void clear_evaluated_flag(ObEvalCtx &ctx)
  {
    if (is_batch_result()) {
      get_evaluated_flags(ctx).unset(ctx.get_batch_idx());
    } else {
      get_eval_info(ctx).clear_evaluated_flag();
    }
  }

  OB_INLINE void set_evaluated_flag(ObEvalCtx &ctx) const
  {
    get_eval_info(ctx).evaluated_ = true;
    if (batch_result_) {
      get_evaluated_flags(ctx).set(ctx.get_batch_idx());
    }
  }

  OB_INLINE void set_evaluated_projected(ObEvalCtx &ctx) const
  {
    get_eval_info(ctx).evaluated_ = true;
    get_eval_info(ctx).projected_ = true;
  }

  typedef common::ObIArray<ObExpr> ObExprIArray;
  OB_INLINE static ObExprIArray *&get_serialize_array()
  {
    RLOCAL_INLINE(ObExprIArray *, g_expr_ser_array);
    return g_expr_ser_array;
  }

  TO_STRING_KV("type", get_type_name(type_),
              K_(datum_meta),
              K_(obj_meta),
              K_(obj_datum_map),
              K_(flag),
              KP_(eval_func),
              KP_(eval_batch_func),
              KP_(inner_functions),
              K_(inner_func_cnt),
              K_(arg_cnt),
              K_(parent_cnt),
              K_(frame_idx),
              K_(datum_off),
              K_(res_buf_off),
              K_(dyn_buf_header_offset),
              K_(res_buf_len),
              K_(eval_flags_off),
              K_(pvt_skip_off),
              K_(expr_ctx_id),
              K_(extra),
              K_(batch_idx_mask),
              K_(local_session_var_id),
              KP(this));

private:
  char *alloc_str_res_mem(ObEvalCtx &ctx, const int64_t size, const int64_t idx) const;
  // Initialize the values in a fresh, unevaluated batch to NULL.
  void clear_datum_descriptors(char *frame, const int64_t size) const;
  int eval_one_datum_of_batch(ObEvalCtx &ctx, common::Datum *&datum) const;
  int do_eval_batch(ObEvalCtx &ctx, const ObBitVector &skip, const int64_t size) const;
  int do_eval_native_i64_batch(ObEvalCtx &ctx, const ObBitVector &skip, int64_t size) const;

public:
  typedef int (*EvalFunc) (const ObExpr &expr,
                           ObEvalCtx &ctx,
                           Datum &expr_datum);
  typedef int (*EvalBatchFunc) (const ObExpr &expr,
                                ObEvalCtx &ctx,
                                const ObBitVector &skip,
                                const int64_t size);
  typedef int (*EvalEnumSetFunc) (const ObExpr &expr,
                                  const common::ObIArray<common::ObString> &str_values,
                                  const uint64_t cast_mode,
                                  ObEvalCtx &ctx,
                                  Datum &expr_datum);

  const static uint64_t MAGIC_NUM = 0x6367614D72707845L; // string of "ExprMagc"
  uint64_t magic_;
  ObExprOperatorType type_;
  // meta data of datum
  ObDatumMeta datum_meta_;
  // Owned by the plan; clone/deserialize must reconstruct this descriptor.
  const common::SqlType *sql_type_;
  const NativeI64ExprLayout *native_i64_layout_;
  // Value metadata used for legacy conversion boundaries.
  common::SqlTypeMeta obj_meta_;
  // max length of datum value. can be less than 0
  int32_t max_length_;
  // Mapping between legacy value memory layout and Datum memory layout.
  common::SqlValueDatumMapType obj_datum_map_;

  union {
    struct {
      uint64_t batch_result_:1;
      uint64_t is_called_in_sql_:1;
      uint64_t is_static_const_:1; // is const during the whole execution
      uint64_t is_boolean_:1; // to distinguish result of this expr between and int tc
      uint64_t is_dynamic_const_:1; // is const during the subplan execution, including exec param
      uint64_t need_stack_check_:1; // the expression tree depth needs to check whether the stack overflows
      uint64_t nullable_:1;
      // Ordinary SQL QM only. The serialized flag selects native owner/index
      // access instead of interpreting its logical legacy byte offsets.
      uint64_t native_parameter_alias_:1;
    };
    uint64_t flag_;
  };
  // expr evaluate function
  union {
    EvalFunc eval_func_;
    // helper union member for eval_func_ serialize && deserialize
    sql::serializable_function ser_eval_func_;
  };
  union {
    EvalBatchFunc eval_batch_func_;
    sql::ser_eval_batch_function ser_eval_batch_func_;
  };
  // aux evaluate functions for eval_func_, array of any function pointers, which interpreted
  // by eval_func_.
  // mysql row operand use the inner function array
  // to compare condition pairs. e.g.:
  //     (a, b, c) = (1, 2, 3)
  //     arg_cnt_ is 6
  //     inner_func_cnt_ is 3
  union {
    void **inner_functions_;
    // helper member for inner_functions_ serialize && deserialize
    sql::serializable_function *ser_inner_functions_;
  };
  uint32_t inner_func_cnt_;
  ObExpr **args_;
  uint32_t arg_cnt_;
  ObExpr **parents_;
  uint32_t parent_cnt_;
  // frame index
  uint32_t frame_idx_;
  // offset of Datum in frame
  uint32_t datum_off_;
  // offset of ObEvalInfo
  uint32_t eval_info_off_;
  // Dynamic buf header offset
  uint32_t dyn_buf_header_offset_;
  // reserve buffer offset in frame
  uint32_t res_buf_off_;
  // reserve buffer length for each datum
  uint32_t res_buf_len_;
  // evaluated flags for batch
  uint32_t eval_flags_off_;
  // private skip bit vector
  uint32_t pvt_skip_off_;
  // expr context id
  uint32_t expr_ctx_id_;
  // extra info, reinterpreted by each expr
  union {
    uint64_t extra_;
    struct {
      int64_t div_calc_scale_          :   16;
      int64_t is_error_div_by_zero_    :    1;
      int64_t may_not_need_raw_check_          :    1;
      int64_t reserved_                :   46;
    };
  };
  const common::DatumBasicFuncs *basic_funcs_;
  common::DatumCmpOp *native_cmp_ops_;
  CmpSpec *native_cmp_specs_;
  int64_t native_cmp_op_count_;
  uint64_t batch_idx_mask_;
  ObIExprExtraInfo *extra_info_;
  int64_t local_session_var_id_;
};

// helper template to access ObExpr::extra_
template <typename T>
struct ObExprExtraInfoAccess
{
  static T &get_info(int64_t &v) { return *reinterpret_cast<T *>(&v); }
  static const T &get_info(const int64_t &v) { return *reinterpret_cast<const T*>(&v); }
  static T &get_info(ObExpr &e) { return get_info(*reinterpret_cast<int64_t *>(&e.extra_)); }
  static const T &get_info(const ObExpr &e)
  { return get_info(*reinterpret_cast<const int64_t *>(&e.extra_)); }
};

// Wrap expression string result buffer allocation to allocator interface.
// Please try not to use this, if you mast use it, make sure it only allocate one time and is
// for expression result.
class ObExprStrResAlloc : public common::ObIAllocator
{
public:
  ObExprStrResAlloc(const ObExpr &expr, ObEvalCtx &ctx) : off_(0), expr_(expr), ctx_(ctx)
  {
  }

  void *alloc(const int64_t size) override;
  void* alloc(const int64_t size, const ObMemAttr &attr) override
  {
    UNUSED(attr);
    return alloc(size);
  }
  void free(void *ptr) override { UNUSED(ptr); }
private:
  int64_t off_;
  const ObExpr &expr_;
  ObEvalCtx &ctx_;
};

using common::DatumParameter;

typedef common::ObIArray<ObExpr *> ObExprPtrIArray;
typedef common::ObFixedArray<ObExpr *, common::ObIAllocator> ExprFixedArray;

// Capture the one evaluated native value with its actual complete declaration.
// A retaining consumer clones both before the frame or declaration owner retires.
int capture_datum_parameter(const ObExpr &expr, ObEvalCtx &ctx,
    common::NativeTypedDatum &value, common::DatumParameterMetadata &attributes);

// Explicit physical boundary for PL/resolver consumers.
// Preserve raw LOB/type metadata separately from declaration accuracy. The
// caller must clone before the expression frame can be reset or overwritten.
int capture_datum_parameter(const ObExpr &expr, ObEvalCtx &ctx, DatumParameter &value);

// A borrowed native descriptor binding; the producer owns its payload.
int bind_datum_parameter(const common::DatumParameter &source,
                            DatumParameter &destination,
                            common::ObIAllocator *allocator);

// Explicit value boundaries for remaining resolver / range interfaces. Values
// and complete type metadata stay separate in native caches.
int clone_datum_value(const common::ObDatum &source, const common::SqlTypeMeta &meta,
                      common::ObDatum &destination, common::ObIAllocator &allocator);

int clone_datum_value(const common::DatumParameter &source,
                      common::DatumParameter &destination,
                      common::ObIAllocator &allocator);
int clone_datum_parameter(const common::DatumParameter &source, DatumParameter &destination,
                          common::ObIAllocator &allocator);
int assign_datum_parameter(const DatumParameter &source, common::DatumParameter &destination);

using common::DatumParamStore;

// Helper function for print datum which interpreted by expression
// Can only be used in log message like this:
//    LOG_WARN(....., "datum", DATUM2STR(*expr, *datum))
struct ObToStringDatum
{
  ObToStringDatum(const ObExpr &e, const common::Datum &d) : e_(e), d_(d) {}

  DECLARE_TO_STRING;

  const ObExpr &e_;
  const common::Datum  &d_;
};
typedef ObToStringDatum DATUM2STR;

// Helper function for expression evaluate && print result
// Can only be used in log message like this:
//    LOG_WARN(....., "datum", EXPR2STR(eval_ctx_, *expr))
//
// Note: Do not use this wrapper to print expression values in the eval_func_ definition of ObExpr,
// Because the implemented eval_func, when printing logs, if calling ObToStringExpr(ctx, expr),
// The function will also call eval_func to calculate, continuously looping the call, and never reaching the set evaluated_
// Marked as true logic, will eventually lead to stack overflow;
// bug:
struct ObToStringExpr
{
  ObToStringExpr(ObEvalCtx &ctx, const ObExpr &e) : c_(ctx), e_(e) {}

  DECLARE_TO_STRING;

  ObEvalCtx &c_;
  const ObExpr &e_;
};
typedef ObToStringExpr EXPR2STR;

// Helper function for expression array evaluate && print result
// Can only be used in log message like this:
//    LOG_WARN(......, "row", ROWEXPR2STR(eval_ctx_, output_)
struct ObToStringExprRow
{
  ObToStringExprRow(ObEvalCtx &ctx, const common::ObIArray<ObExpr *> &exprs)
      : c_(ctx), exprs_(exprs) { }

  DECLARE_TO_STRING;

  ObEvalCtx &c_;
  const common::ObIArray<ObExpr *> &exprs_;
};
typedef ObToStringExprRow ROWEXPR2STR;

struct ToStringExprRowVec {
  ToStringExprRowVec(const common::ObIArray<ObExpr *> &exprs, ObEvalCtx &ctx,
      const ObBitVector *skip, const EvalBound &bound, const int64_t &batch_size)
      : exprs_(exprs), ctx_(ctx), skip_(skip), bound_(bound), batch_size_(batch_size)
  {}
  DECLARE_TO_STRING;
  int meta_to_string(char *buf, const int64_t buf_len) const;

private:
  const common::ObIArray<ObExpr *> &exprs_;
  ObEvalCtx &ctx_;
  const ObBitVector *skip_;
  const EvalBound &bound_;
  int64_t batch_size_;
};
typedef ToStringExprRowVec ROWEXPR2STRVEC;

OB_INLINE Datum &ObExpr::locate_datum_for_write(ObEvalCtx &ctx) const
{
  return locate_expr_datum(ctx);
}

inline Datum *ObExpr::locate_datums_for_update(ObEvalCtx &ctx, const int64_t size) const
{
  UNUSED(size);
  return locate_batch_datums(ctx);
}

template <typename ...TS>
OB_INLINE int ObExpr::eval_param_value(ObEvalCtx &ctx, TS &...args) const
{
  int ret = common::OB_SUCCESS;
  common::Datum **params[] = { &args... };
  common::Datum *tmp = NULL;
  for (int param_index = 0; OB_SUCC(ret) && param_index < arg_cnt_; param_index++) {
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Warray-bounds\"")
    if (OB_FAIL(args_[param_index]->eval(
                ctx, param_index < ARRAYSIZEOF(params) ? *params[param_index] : tmp))) {
    }
_Pragma("GCC diagnostic pop")
  }
  return ret;
}

template <typename ...TS>
OB_INLINE int ObExpr::eval_batch_param_value(ObEvalCtx &ctx, const ObBitVector &skip,
                                       const int64_t size, TS &...args) const
{
  int ret = common::OB_SUCCESS;
  DatumVector *params[] = { &args...};
  for (int param_index = 0; OB_SUCC(ret) && param_index < arg_cnt_; param_index++) {
    if (OB_FAIL(args_[param_index]->eval_batch(ctx, skip, size))) {
    } else if (param_index < ARRAYSIZEOF(params)) {
      *params[param_index] = locate_param_datumvector(ctx, param_index);
    }
  }
  return ret;
}

OB_INLINE int ObExpr::eval(ObEvalCtx &ctx, common::Datum *&datum) const
{
  if (native_i64_layout_ != nullptr) {
    bool native = false;
    const int ret = get_native_i64_mode(ctx, native);
    if (ret != common::OB_SUCCESS) { return ret; }
    if (native) { return common::OB_ERR_UNEXPECTED; }
  }
  if (native_parameter_alias_) { return eval_native_parameter(ctx, datum); }
  // performance critical, do not check %frame_idx_ and %frame again. (checked in CG)
	int ret = common::OB_SUCCESS;
	char *frame = ctx.frames_[frame_idx_];
  OB_ASSERT(NULL != frame);
	datum = (Datum *)(frame + datum_off_);
  ObEvalInfo *eval_info = (ObEvalInfo *)(frame + eval_info_off_);
  if (is_batch_result()) {
    if (NULL == eval_func_ || eval_info->projected_) {
      datum = datum + ctx.get_batch_idx();
    } else {
      ret = eval_one_datum_of_batch(ctx, datum);
    }
  } else if (NULL != eval_func_ && !eval_info->evaluated_) {
	// do nothing for const/column reference expr or already evaluated expr
    if (OB_UNLIKELY(need_stack_check_) && OB_FAIL(check_stack_overflow())) {
      SQL_LOG(WARN, "failed to check stack overflow", K(ret));
    } else {
      ret = eval_func_(*this, ctx, *datum);
      if (OB_LIKELY(common::OB_SUCCESS == ret)) {
        eval_info->evaluated_ = true;
      } else {
        datum->set_null();
      }
    }
	}
	return ret;
}

OB_INLINE int ObExpr::eval_batch(ObEvalCtx &ctx,
                                 const ObBitVector &skip,
                                 const int64_t size) const
{
  if (native_i64_layout_ != nullptr) {
    bool native = false;
    const int ret = get_native_i64_mode(ctx, native);
    if (ret != common::OB_SUCCESS) { return ret; }
    if (native) { return do_eval_native_i64_batch(ctx, skip, size); }
  }
  // A parameter's first generic ABI request can allocate. This failure must
  // reach the caller even for an all-skipped batch: callers locate the result
  // after success, unlike the old const callback dry-run error contract.
  if (native_parameter_alias_) {
    common::Datum *datum = nullptr;
    return eval_native_parameter(ctx, datum);
  }
  int ret = common::OB_SUCCESS;
  const ObEvalInfo &info = get_eval_info(ctx);
  if (!is_batch_result()) {
    bool const_dry_run = skip.accumulate_bit_cnt(size) >= size;
    common::Datum *datum = NULL;
    ret = eval(ctx, datum);
    if (!const_dry_run) {
      // do nothing
    } else if (OB_FAIL(ret)) {
      ret = OB_SUCCESS;
    }
  } else if (info.projected_ || NULL == eval_batch_func_) {
    // expr values is projected by child or has no evaluate func, do nothing.
  } else if (size > 0) {
    ret = do_eval_batch(ctx, skip, size);
  }
  return ret;
}

OB_INLINE int ObExpr::deep_copy_self_datum(ObEvalCtx &ctx) const
{
  int ret = OB_SUCCESS;
  Datum *parameter = nullptr;
  if (native_parameter_alias_ && OB_FAIL(eval_native_parameter(ctx, parameter))) { return ret; }
  const Datum &datum = locate_expr_datum(ctx);
  if (OB_FAIL(deep_copy_datum(ctx, datum))) {
  }

  return ret;
}

OB_INLINE int ObExpr::deep_copy_datum(ObEvalCtx &ctx, const common::Datum &datum) const
{
  if (native_parameter_alias_) { return assign_native_parameter(ctx, datum); }
  // Preserve the source before selecting owner memory: source may be this slot.
  const common::Datum source = datum;
  const common::DatumCopyPlan plan = get_datum_copy_plan();
  int64_t bytes = 0;
  int ret = plan.carrier_span_size(source, bytes);
  if (OB_SUCC(ret) && source.is_present() && source.is_ext()
      && get_sql_type().legacy_meta().is_pl_extend_type()) {
    // The complete CopyPlan validation above still precedes publication.
    // Retain only the PL carrier here; ownership of its pointee is unchanged.
    common::Datum candidate;
    candidate.set_inline_extended(source.get_ext(), source.get_extend_type(),
        source.get_extend_size(), source.get_extend_collation_level(),
        source.get_extend_collation(), source.is_inline_extended()
            ? source.inline_extended_payload_size() == common::DATUM_LEGACY_EXTEND_SIZE
            : source.length() == common::DATUM_LEGACY_EXTEND_SIZE);
    locate_expr_datum(ctx) = candidate;
  } else if (OB_SUCC(ret)) {
    char *buffer = bytes > 0 ? get_reference_res_mem(ctx, bytes) : nullptr;
    if (bytes > 0 && buffer == nullptr) {
      ret = common::OB_ALLOCATE_MEMORY_FAILED;
    } else {
      int64_t position = 0;
      ret = plan.copy_carrier_span_borrowing_resources(
          source, locate_expr_datum(ctx), buffer, bytes, position);
    }
  }
  return ret;
}

class VectorizedRowsWrapper final
{
public:
  VectorizedRowsWrapper(ObEvalCtx &ctx, const common::ObIArray<ObExpr *> &exprs,
    const ObBitVector *skip, const EvalBound &bound, const int64_t &batch_size,
    int64_t index)
    : vec_(exprs, ctx, skip, bound, batch_size), index_(index)
  {}
  ~VectorizedRowsWrapper() = default;
  OB_INLINE int64_t to_string(char *buf, const int64_t buf_len) const;
private:
  DISABLE_COPY_ASSIGN(VectorizedRowsWrapper);
private:
  ROWEXPR2STRVEC vec_;
  int64_t index_;
};

OB_INLINE int64_t VectorizedRowsWrapper::to_string(char *buf, const int64_t buf_len) const
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  if (nullptr != buf && buf_len > 0) {
    if (OB_FAIL(databuff_printf(buf, buf_len, pos, "vectorized_rows(%ld)=", index_))) {
    } else {
      int64_t str_len = vec_.to_string(buf + pos, buf_len - pos - 1);
      pos += str_len;
      if (pos >= 0 && pos < buf_len) {
        buf[pos] = '\0';
      } else {
        buf[0] = '\0';
        pos = 0;
      }
    }
  }
  return pos;
}

class VectorizedExprsMetaWrapper final
{
public:
VectorizedExprsMetaWrapper(ObEvalCtx &ctx, const common::ObIArray<ObExpr *> &exprs,
  const ObBitVector *skip, const EvalBound &bound, const int64_t &batch_size)
    : vec_(exprs, ctx, skip, bound, batch_size)
  {}
  ~VectorizedExprsMetaWrapper() = default;
  OB_INLINE int64_t to_string(char *buf, const int64_t buf_len) const;
private:
  DISABLE_COPY_ASSIGN(VectorizedExprsMetaWrapper);
private:
  ROWEXPR2STRVEC vec_;
};

OB_INLINE int64_t VectorizedExprsMetaWrapper::to_string(char *buf, const int64_t buf_len) const
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  if (nullptr != buf && buf_len > 0) {
    if (OB_FAIL(databuff_printf(buf, buf_len, pos, "vectorized_exprs_meta="))) {
    } else {
      int64_t str_len = vec_.meta_to_string(buf + pos, buf_len - pos - 1);
      pos += str_len;
      if (pos >= 0 && pos < buf_len) {
        buf[pos] = '\0';
      } else {
        buf[0] = '\0';
        pos = 0;
      }
    }
  }
  return pos;
}

class VectorizedRowByColumnWrapper final
{
public:
  VectorizedRowByColumnWrapper(ObEvalCtx &ctx, const ObExpr *expr,
    const ObBitVector *skip, const EvalBound &bound, const int64_t index)
    : ctx_(ctx), expr_(expr), skip_(skip), bound_(bound), index_(index)
  {}
  ~VectorizedRowByColumnWrapper() = default;
  OB_INLINE int64_t to_string(char *buf, const int64_t buf_len) const;
private:
  DISABLE_COPY_ASSIGN(VectorizedRowByColumnWrapper);
private:
  ObEvalCtx &ctx_;
  const ObExpr *expr_;
  const ObBitVector *skip_;
  const EvalBound &bound_;
  int64_t index_;
};

OB_INLINE int64_t VectorizedRowByColumnWrapper::to_string(char *buf, const int64_t buf_len) const
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  if (nullptr != buf && buf_len > 0) {
    if (OB_FAIL(databuff_printf(buf, buf_len, pos, "vectorized_expr(%ld)=", index_))) {
    } else if (NULL != expr_) {
      int64_t batch_size = bound_.batch_size();
      ObEvalCtx::BatchInfoScopeGuard _batch_info_guard(ctx_);
      _batch_info_guard.set_batch_size(batch_size);
      for (int64_t i = 0; i < batch_size; ++i) {
        if (NULL != skip_ && skip_->at(i)) {
          continue;
        }
        _batch_info_guard.set_batch_idx(i);
        pos += ObToStringExpr(ctx_, *expr_).to_string(buf + pos, buf_len - pos - 1);
      }
    } else {
      if (OB_FAIL(databuff_printf(buf, buf_len, pos, "{null}"))) {
      }
    }
    if (OB_SUCC(ret) && pos >= 0 && pos < buf_len) {
      buf[pos] = '\0';
    } else {
      buf[0] = '\0';
      pos = 0;
    }
  }
  return pos;
}

#define PRINT_VECTORIZED_ROWS(parMod, level, eval_ctx, exprs, batch_size, skip, args...)         \
  do {                                                                                           \
    if (IS_LOG_ENABLED(level)) {                                                                 \
      [&](const char *_fun_name_) __attribute__((GET_LOG_FUNC_ATTR(level)))                      \
      {                                                                                          \
        if (OB_UNLIKELY(OB_LOGGER.need_to_print(                                                 \
                ::oceanbase::common::OB_LOG_ROOT::M_##parMod, OB_LOG_LEVEL_##level))) {          \
          int64_t _batch_size = common::min(batch_size, (eval_ctx).get_batch_size());            \
          if (_batch_size > 0) {                                                                 \
            const sql::EvalBound bound =  sql::EvalBound(_batch_size, false);                    \
            {                                                                                    \
              ObCStringHelper helper;                                                            \
              sql::VectorizedExprsMetaWrapper wrapper(eval_ctx, exprs, skip, bound, _batch_size);\
              ::oceanbase::common::OB_PRINT("[" #parMod "] ",                                    \
                  OB_LOG_LEVEL(level), helper.convert(wrapper), LOG_KVS(args));                  \
            }                                                                                    \
            ObEvalCtx::BatchInfoScopeGuard _batch_info_guard(eval_ctx);                          \
            _batch_info_guard.set_batch_size(_batch_size);                                       \
            for (int64_t i = 0; i < _batch_size; ++i) {                                          \
              if (NULL != skip && skip->at(i)) {                                                 \
                continue;                                                                        \
              }                                                                                  \
              _batch_info_guard.set_batch_idx(i);                                                \
              ObCStringHelper helper;                                                            \
              sql::VectorizedRowsWrapper wrapper(eval_ctx, exprs, skip, bound, _batch_size, i);  \
              ::oceanbase::common::OB_PRINT("["#parMod"] ",                                      \
                  OB_LOG_LEVEL(level), helper.convert(wrapper), LOG_KVS(args));                  \
            }                                                                                    \
          }                                                                                      \
        }                                                                                        \
      }                                                                                          \
      (__FUNCTION__);                                                                            \
    }                                                                                            \
  } while (false)

#define PRINT_VECTORIZED_ROWS_BY_COLUMN(parMod, level, eval_ctx, exprs, batch_size, skip, args...) \
  do {                                                                                             \
    if (IS_LOG_ENABLED(level)) {                                                                   \
      [&]([[maybe_unused]] const char *_fun_name_) __attribute__((GET_LOG_FUNC_ATTR(level)))        \
      {                                                                                            \
        if (OB_UNLIKELY(OB_LOGGER.need_to_print(                                                   \
                ::oceanbase::common::OB_LOG_ROOT::M_##parMod, OB_LOG_LEVEL_##level))) {            \
          int64_t _batch_size = common::min(batch_size, (eval_ctx).get_batch_size());              \
          if (_batch_size > 0) {                                                                   \
            for (int64_t i = 0; i < (exprs).count(); i++) {                                        \
              const ObExpr *expr = (exprs).at(i);                                                  \
              ObCStringHelper helper;                                                              \
              sql::VectorizedRowByColumnWrapper wrapper(eval_ctx, expr, skip,                       \
                  sql::EvalBound(_batch_size, true), i);                                           \
              ::oceanbase::common::OB_PRINT("[" #parMod "] ",                                      \
                  OB_LOG_LEVEL(level), helper.convert(wrapper), LOG_KVS(args));                    \
            }                                                                                      \
          }                                                                                        \
        }                                                                                          \
      }                                                                                            \
      (__FUNCTION__);                                                                              \
    }                                                                                              \
  } while (false)
}  // end namespace sql

namespace common
{
namespace serialization
{

// ObExpr pointer serialize && deserialize.
// Convert pointer to index of expr array, serialize && deserialize the index.
// The serialized index is the real index + 1, to make room for NULL.
inline int64_t encoded_length(sql::ObExpr *)
{
  return sizeof(uint32_t);
}

inline int encode(char *buf, const int64_t buf_len, int64_t &pos, sql::ObExpr *expr)
{
  int ret = common::OB_SUCCESS;
  uint32_t idx = 0;
  if (NULL != expr) {
    sql::ObExpr::ObExprIArray *array = sql::ObExpr::get_serialize_array();
    if (OB_UNLIKELY(NULL == array || array->empty() || expr < &array->at(0)
                    || (idx = static_cast<uint32_t>(expr - &array->at(0) + 1)) > array->count())) {
      ret = OB_ERR_UNEXPECTED;
      SQL_LOG(WARN, "expr not in array", K(ret), KP(array), KP(idx), KP(expr));
    }
  }
  if (OB_SUCC(ret)) {
    ret = encode_i32(buf, buf_len, pos, idx);
  }
  return ret;
}

inline int decode(const char *buf, const int64_t data_len, int64_t &pos, sql::ObExpr *&expr)
{
  int ret = common::OB_SUCCESS;
  uint32_t idx = 0;
  ret = decode_i32(buf, data_len, pos, reinterpret_cast<int32_t *>(&idx));
  if (OB_SUCC(ret)) {
    if (0 == idx) {
      expr = NULL;
    } else {
      sql::ObExpr::ObExprIArray *array = sql::ObExpr::get_serialize_array();
      if (OB_UNLIKELY(NULL == array) || OB_UNLIKELY(idx > array->count())) {
        ret = OB_ERR_UNEXPECTED;
        SQL_LOG(WARN, "expr index out of expr array range", K(ret), KP(array), K(idx));
      } else {
        expr = &array->at(idx - 1);
      }
    }
  }
  return ret;
}

template <>
struct EnumEncoder<false, sql::ObExpr *>
{
  static int encode(char *buf, const int64_t buf_len, int64_t &pos, sql::ObExpr *expr)
  {
    return serialization::encode(buf, buf_len, pos, expr);
  }

  static int decode(const char *buf, const int64_t data_len, int64_t &pos, sql::ObExpr *&expr)
  {
    return serialization::decode(buf, data_len, pos, expr);
  }

  static int64_t encoded_length(sql::ObExpr *expr)
  {
    return serialization::encoded_length(expr);
  }
};


//
// Simple c array wrapper for serialize && deserialize. e.g.:
//  OB_SERIALIZE_MEMBER(Foo, make_ser_carray(items_, item_cnt_))
//
template <typename T, typename U>
struct ObSerCArray
{
  ObSerCArray(T &data, U &cnt) : data_(data), cnt_(cnt) {}
  T &data_;
  U &cnt_;
  TO_STRING_KV(K(cnt_));
};

template <typename T, typename U>
ObSerCArray<T, U> make_ser_carray(T &data, U &cnt) { return ObSerCArray<T, U>(data, cnt); }


template <typename T, typename U>
inline int64_t encoded_length(const ObSerCArray<T *const, U> &array)
{
  int64_t len = 0;
  OB_UNIS_ADD_LEN_ARRAY(array.data_, array.cnt_);
  return len;
}

template <typename T, typename U>
inline int encode(char *buf, const int64_t buf_len, int64_t &pos,
                  const ObSerCArray<T *const, U> &array)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(array.cnt_ > 0 && NULL == array.data_)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_LOG(WARN, "array not empty but data is NULL",
            K(ret), KP(array.data_), K(array.cnt_));
  } else {
    OB_UNIS_ENCODE_ARRAY(array.data_, array.cnt_);
  }
  return ret;
}

template <typename T, typename U>
inline int decode(const char *buf, const int64_t data_len, int64_t &pos,
                  const ObSerCArray<T *, U> &array)
{
  int ret = OB_SUCCESS;
  OB_UNIS_DECODE(array.cnt_);
  if (OB_SUCC(ret)) {
    if (0 == array.cnt_) {
      array.data_ = NULL;
    } else {
      const int64_t alloc_size = sizeof(*array.data_) * array.cnt_;
      array.data_ = static_cast<T *>(
          CURRENT_CONTEXT->get_arena_allocator().alloc(alloc_size));
      if (OB_ISNULL(array.data_)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        SQL_LOG(WARN, "alloc memory failed", K(ret), K(alloc_size));
      } else {
        int64_t idx = 0;
        for ( ; OB_SUCC(ret) && idx < array.cnt_; idx++) {
          new (&array.data_[idx]) T();
          OB_UNIS_DECODE(array.data_[idx]);
        }
        // deconstruct the constructed objects.
        if (OB_SUCCESS != ret) {
          for (int64_t i = idx; i >= 0; i--) {
            array.data_[i].~T();
          }
        }
      }
    }
  }
  return ret;
}

} // end namespace serialization
} // end namespace common
} // end namespace oceanbase

#endif // OCEANBASE_EXPR_OB_EXPR_H_
