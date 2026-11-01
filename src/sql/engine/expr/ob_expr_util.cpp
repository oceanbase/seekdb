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
#include "share/datum/datum_value_cast.h"
#include "ob_expr_util.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "share/ob_timezone_mgr.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_subschema_ctx.h"
#include "common/udt/ob_collection_type.h"
#include "common/udt/ob_array_type.h"
#include "common/enumset/ob_enum_set_meta.h"
#include "sql/resolver/expr/ob_raw_expr_util.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/pl/ob_pl.h"
#include "sql/pl/ob_pl_user_type.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace
{
// Collection schema nodes already retain the actual basic declaration and
// nesting. This is preparation work; values only borrow the resulting Type.
int resolve_collection_type(const ObCollectionTypeBase &node, const ObDataType &attributes,
                            ObIAllocator &owner, const SqlType *&out, int64_t depth)
{
  int ret = OB_SUCCESS;
  const SqlType *candidate = nullptr;
  if (depth > 64) {
    ret = OB_SIZE_OVERFLOW;
  } else if (node.type_id_ == OB_BASIC_TYPE) {
    ret = SqlType::from_legacy(static_cast<const ObCollectionBasicType &>(node).basic_meta_, owner, candidate);
  } else {
    SqlTypeSpec spec;
    spec.attributes = attributes;
    if (attributes.get_length() >= 0) { spec.specified |= SqlTypeSpec::Length; }
    if (attributes.get_length_semantics() != LS_INVALIED) { spec.specified |= SqlTypeSpec::LengthSemantics; }
    ObDataType child_attributes;
    child_attributes.meta_.set_type(ObCollectionSQLType);
    child_attributes.meta_.set_collation_type(CS_TYPE_BINARY);
    if (node.type_id_ == OB_ARRAY_TYPE || node.type_id_ == OB_VECTOR_TYPE) {
      const auto &array = static_cast<const ObCollectionArrayType &>(node);
      spec.form = node.type_id_ == OB_VECTOR_TYPE ? SqlTypeForm::Vector : SqlTypeForm::Array;
      spec.dimension = node.type_id_ == OB_VECTOR_TYPE ? array.dim_cnt_ : 0;
      if (array.element_type_ == nullptr) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        ret = resolve_collection_type(*array.element_type_, child_attributes, owner, spec.element, depth + 1);
      }
    } else if (node.type_id_ == OB_MAP_TYPE || node.type_id_ == OB_SPARSE_VECTOR_TYPE) {
      const auto &map = static_cast<const ObCollectionMapType &>(node);
      spec.form = node.type_id_ == OB_MAP_TYPE ? SqlTypeForm::Map : SqlTypeForm::SparseVector;
      if (map.key_type_ == nullptr || map.value_type_ == nullptr) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(resolve_collection_type(*map.key_type_, child_attributes, owner, spec.key, depth + 1))) {
      } else {
        ret = resolve_collection_type(*map.value_type_, child_attributes, owner, spec.value, depth + 1);
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
    }
    if (OB_SUCC(ret)) { ret = SqlType::create(spec, owner, candidate); }
  }
  if (OB_SUCC(ret)) { out = candidate; }
  return ret;
}

// These definitions are compile/runtime declarations, never PL payloads. The
// identity getters are populated at the real declaration producer and copied
// with that declaration, including parent-local imports and cached versions.
int check_pl_record_identity(const oceanbase::pl::ObUserDefinedType &definition, uint64_t declaration_id)
{
  int ret = OB_SUCCESS;
  const SqlPLDeclarationIdentity &identity = definition.get_sql_declaration_identity();
  const SqlNominalType &nominal = definition.get_sql_nominal_identity();
  if (!identity.is_specified() || !identity.valid_with(nominal)
      || identity.declaration_id != declaration_id || declaration_id == OB_INVALID_ID) {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int resolve_pl_record_type(const ObDataType &attributes, const oceanbase::pl::ObPLINS &declarations,
                           const ObExecContext *sql_metadata, ObIAllocator &owner,
                           const SqlType *&out, int64_t depth);

int resolve_pl_member_type(const oceanbase::pl::ObPLDataType &declaration, const oceanbase::pl::ObPLINS &declarations,
                           const ObExecContext *sql_metadata, ObIAllocator &owner,
                           const SqlType *&out, int64_t depth)
{
  int ret = OB_SUCCESS;
  const SqlType *candidate = nullptr;
  if (depth > 64) {
    ret = OB_SIZE_OVERFLOW;
  } else if (declaration.is_obj_type()) {
    const ObDataType *attributes = declaration.get_data_type();
    if (attributes == nullptr) {
      ret = OB_ERR_UNEXPECTED;
    } else if (attributes->meta_.is_enum_or_set() || attributes->meta_.is_enumset_inner_type()) {
      ObIArray<ObString> *labels = nullptr;
      if (OB_FAIL(declaration.get_type_info(labels))) {
      } else if (labels == nullptr) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        SqlTypeSpec spec;
        spec.attributes = *attributes;
        spec.form = attributes->get_obj_type() == ObEnumType || attributes->get_obj_type() == ObEnumInnerType
            ? SqlTypeForm::Enum : SqlTypeForm::Set;
        ObSEArray<ObString, 16> values;
        for (int64_t i = 0; OB_SUCC(ret) && i < labels->count(); ++i) { ret = values.push_back(labels->at(i)); }
        if (OB_SUCC(ret)) {
          spec.labels = values.empty() ? nullptr : &values.at(0);
          spec.label_count = values.count();
          ret = SqlType::create(spec, owner, candidate);
        }
      }
    } else if (attributes->meta_.is_collection_sql_type()) {
      if (sql_metadata == nullptr) { ret = OB_ERR_UNEXPECTED; }
      else {
        ret = ObExprUtil::resolve_sql_type(*const_cast<ObExecContext *>(sql_metadata), attributes->meta_, attributes->get_accuracy(),
            attributes->meta_.get_subschema_id(), owner, candidate, &declarations);
      }
    } else if (attributes->meta_.is_ext_sql_array()) {
      // T_EXT_SQL_ARRAY declares a heterogeneous SQL array, unlike a PL
      // nested-table/varray whose element declaration lives in the PL namespace.
      SqlTypeSpec spec;
      spec.attributes = *attributes;
      spec.form = SqlTypeForm::Array;
      spec.heterogeneous = true;
      ret = SqlType::create(spec, owner, candidate);
    } else {
      ret = SqlType::from_legacy(*attributes, owner, candidate);
    }
  } else if (declaration.is_record_type()) {
    ObDataType attributes;
    attributes.meta_.set_ext();
    attributes.meta_.set_extend_type(oceanbase::pl::PL_RECORD_TYPE);
    attributes.set_udt_id(declaration.get_user_type_id());
    ret = resolve_pl_record_type(attributes, declarations, sql_metadata, owner, candidate, depth);
  } else {
    // Cursor and PL collection/opaque domains require their own declaration
    // and retaining contracts. They are not a SQL Array or an empty Record.
    ret = OB_NOT_SUPPORTED;
  }
  if (OB_SUCC(ret)) { out = candidate; }
  return ret;
}

int resolve_pl_record_type(const ObDataType &attributes, const oceanbase::pl::ObPLINS &declarations,
                           const ObExecContext *sql_metadata, ObIAllocator &owner,
                           const SqlType *&out, int64_t depth)
{
  int ret = OB_SUCCESS;
  const SqlType *candidate = nullptr;
  const oceanbase::pl::ObUserDefinedType *definition = nullptr;
  // Ext accuracy is the complete 64-bit declaration ID, not numeric precision
  // and scale. This path is identical for NULL and non-NULL values.
  const uint64_t declaration_id = attributes.get_udt_id();
  if (depth > 64) {
    ret = OB_SIZE_OVERFLOW;
  } else if (!attributes.meta_.is_pl_extend_type()
      || attributes.meta_.get_extend_type() != oceanbase::pl::PL_RECORD_TYPE) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(declarations.get_user_type(declaration_id, definition))) {
  } else if (definition == nullptr || !definition->is_record_type()
      || definition->get_user_type_id() != declaration_id) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(check_pl_record_identity(*definition, declaration_id))) {
  } else {
    const auto &record = static_cast<const oceanbase::pl::ObRecordType &>(*definition);
    SqlTypeSpec spec;
    spec.attributes = attributes;
    spec.form = SqlTypeForm::Record;
    spec.pl_declaration = definition->get_sql_declaration_identity();
    spec.nominal = definition->get_sql_nominal_identity();
    ObSEArray<SqlTypeMember, 16> members;
    for (int64_t i = 0; OB_SUCC(ret) && i < record.get_record_member_count(); ++i) {
      const ObString *name = record.get_record_member_name(i);
      const oceanbase::pl::ObPLDataType *member = record.get_record_member_type(i);
      SqlTypeMember resolved;
      if (name == nullptr || member == nullptr) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(resolve_pl_member_type(*member, declarations, sql_metadata, owner,
                                               resolved.type, depth + 1))) {
      } else {
        resolved.name = *name;
        resolved.nullable = !member->is_not_null();
        ret = members.push_back(resolved);
      }
    }
    if (OB_SUCC(ret)) {
      spec.members = members.empty() ? nullptr : &members.at(0);
      spec.member_count = members.count();
      // create() clones all member names and child types into the requested
      // owner; no namespace or temporary descriptor escapes this preparation.
      ret = SqlType::create(spec, owner, candidate);
    }
  }
  if (OB_SUCC(ret)) { out = candidate; }
  return ret;
}
} // namespace

int ObExprUtil::resolve_pl_parameter_sql_type(const pl::ObPLDataType &declaration,
    const pl::ObPLINS &declarations, const ObExecContext &sql_metadata,
    ObIAllocator &owner, const SqlType *&out)
{
  return resolve_pl_member_type(declaration, declarations, &sql_metadata, owner, out, 0);
}

int ObExprUtil::resolve_enum_sql_type(const SqlTypeMeta &actual_meta,
                                     const ObAccuracy &accuracy,
                                     const ObIArray<ObString> &labels,
                                     ObIAllocator &owner, const SqlType *&out)
{
  int ret = OB_SUCCESS;
  const SqlType *candidate = nullptr;
  if (!actual_meta.is_enum_or_set() && !actual_meta.is_enumset_inner_type()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    SqlTypeSpec spec;
    spec.attributes.set_meta_type(actual_meta);
    spec.attributes.set_accuracy(accuracy);
    spec.attributes.set_charset_type(actual_meta.get_charset_type());
    spec.form = actual_meta.get_type() == ObEnumType || actual_meta.get_type() == ObEnumInnerType
        ? SqlTypeForm::Enum : SqlTypeForm::Set;
    ObSEArray<ObString, 16> values;
    for (int64_t i = 0; OB_SUCC(ret) && i < labels.count(); ++i) { ret = values.push_back(labels.at(i)); }
    if (OB_SUCC(ret)) {
      spec.labels = values.empty() ? nullptr : &values.at(0);
      spec.label_count = values.count();
      ret = SqlType::create(spec, owner, candidate);
    }
  }
  if (OB_SUCC(ret)) { out = candidate; }
  return ret;
}

int ObExprUtil::resolve_raw_sql_type(const ObRawExprResType &resolved,
                                    const ObSQLSessionInfo *session,
                                    share::schema::ObSchemaGetterGuard *schema_guard,
                                    ObIAllocator &owner, const SqlType *&out,
                                    const pl::ObPLINS *pl_declarations)
{
  int ret = OB_SUCCESS;
  const SqlType *candidate = nullptr;
  UNUSED(schema_guard); // PL origins retain the version of their actual definition.
  if (resolved.is_pl_extend_type() && resolved.get_extend_type() == pl::PL_RECORD_TYPE) {
    if (pl_declarations == nullptr) { ret = OB_ERR_UNEXPECTED; }
    else {
      ObDataType attributes;
      attributes.set_meta_type(resolved.get_sql_type_meta());
      attributes.set_accuracy(resolved.get_accuracy());
      const ObExecContext *sql_metadata = session == nullptr ? nullptr : session->get_cur_exec_ctx();
      ret = resolve_pl_record_type(attributes, *pl_declarations, sql_metadata, owner, candidate, 0);
    }
  } else if (resolved.is_ext_sql_array()) {
    SqlTypeSpec spec;
    spec.attributes.set_meta_type(resolved.get_sql_type_meta());
    spec.attributes.set_accuracy(resolved.get_accuracy());
    spec.form = SqlTypeForm::Array;
    spec.heterogeneous = true;
    ret = SqlType::create(spec, owner, candidate);
  } else if (resolved.is_enum_or_set() || resolved.is_enumset_inner_type()) {
    const ObEnumSetMeta *definition = nullptr;
    if (OB_FAIL(ObRawExprUtils::extract_enum_set_meta(resolved, session, definition))) {
    } else if (definition == nullptr || definition->get_str_values() == nullptr) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      // Raw enum metadata carries the SubSchema ID in its collation bytes.
      // The actual definition owns collation/charset; keep the requested kind.
      SqlTypeMeta actual_meta = definition->get_sql_type_meta();
      actual_meta.set_type_simple(resolved.get_type());
      ret = resolve_enum_sql_type(actual_meta, resolved.get_accuracy(),
                                  *definition->get_str_values(), owner, candidate);
    }
  } else if (resolved.is_collection_sql_type()) {
    const ObExecContext *context = session == nullptr ? nullptr : session->get_cur_exec_ctx();
    if (context == nullptr) { ret = OB_ERR_UNEXPECTED; }
    else {
      // Existing resolver/array CG paths own this same request SubSchema. The
      // resolver below only reads it and copies its complete declaration tree.
      ret = resolve_sql_type(*const_cast<ObExecContext *>(context), resolved.get_sql_type_meta(),
          resolved.get_accuracy(), resolved.get_subschema_id(), owner, candidate, pl_declarations);
    }
  } else {
    ret = SqlType::from_legacy(resolved.get_sql_type_meta(), resolved.get_accuracy(), owner, candidate);
  }
  if (OB_SUCC(ret)) { out = candidate; }
  return ret;
}

int ObExprUtil::resolve_sql_type(ObExecContext &ctx, const SqlTypeMeta &meta,
                                const ObAccuracy &accuracy, const uint16_t subschema_id,
                                ObIAllocator &owner, const SqlType *&out,
                                const pl::ObPLINS *pl_declarations)
{
  int ret = OB_SUCCESS;
  const SqlType *candidate = nullptr;
  if (meta.is_pl_extend_type() && meta.get_extend_type() == pl::PL_RECORD_TYPE) {
    if (pl_declarations == nullptr) { ret = OB_ERR_UNEXPECTED; }
    else {
      ObDataType attributes;
      attributes.set_meta_type(meta);
      attributes.set_accuracy(accuracy);
      ret = resolve_pl_record_type(attributes, *pl_declarations, &ctx, owner, candidate, 0);
    }
  } else if (meta.is_ext_sql_array()) {
    SqlTypeSpec spec;
    spec.attributes.set_meta_type(meta);
    spec.attributes.set_accuracy(accuracy);
    spec.form = SqlTypeForm::Array;
    spec.heterogeneous = true;
    ret = SqlType::create(spec, owner, candidate);
  } else if (meta.is_enum_or_set() || meta.is_enumset_inner_type()) {
    const ObEnumSetMeta *definition = nullptr;
    if (OB_FAIL(ctx.get_enumset_meta_by_subschema_id(subschema_id, false, definition))) {
    } else if (definition == nullptr || definition->get_str_values() == nullptr) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      SqlTypeMeta actual_meta = definition->get_sql_type_meta();
      actual_meta.set_type_simple(meta.get_type());
      ret = resolve_enum_sql_type(actual_meta, accuracy, *definition->get_str_values(), owner, candidate);
    }
  } else if (meta.is_collection_sql_type()) {
    ObSubSchemaValue value;
    if (OB_FAIL(ctx.get_sqludt_meta_by_subschema_id(subschema_id, value))) {
    } else if (value.type_ != OB_SUBSCHEMA_COLLECTION_TYPE || value.value_ == nullptr) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      const auto *collection = static_cast<const ObSqlCollectionInfo *>(value.value_);
      ObDataType attributes;
      attributes.set_meta_type(meta); attributes.set_accuracy(accuracy);
      attributes.set_charset_type(meta.get_charset_type());
      if (collection->collection_meta_ == nullptr) { ret = OB_ERR_UNEXPECTED; }
      else { ret = resolve_collection_type(*collection->collection_meta_, attributes, owner, candidate, 0); }
    }
  } else {
    // Scalar construction intentionally does not guess a PL/UDT nominal ID,
    // schema version, record fields or an extended array's child declaration.
    ret = SqlType::from_legacy(meta, accuracy, owner, candidate);
  }
  if (OB_SUCC(ret)) { out = candidate; }
  return ret;
}

int ObExprUtil::set_number(const ObExpr &expr, ObEvalCtx &ctx, Datum &out,
                           const number::ObNumber &value, const int64_t index)
{
  const uint32_t bytes = sizeof(number::ObCompactNumber) + value.get_length() * sizeof(uint32_t);
  char *buffer = expr.get_reference_res_mem(ctx, bytes, index);
  return buffer == nullptr ? OB_ALLOCATE_MEMORY_FAILED : out.set_number(value, buffer, bytes);
}

int ObExprUtil::set_number(const ObExpr &expr, ObEvalCtx &ctx, Datum &out,
                           const number::ObCompactNumber &value, const int64_t index)
{
  const uint32_t bytes = sizeof(value) + value.desc_.len_ * sizeof(uint32_t);
  char *buffer = expr.get_reference_res_mem(ctx, bytes, index);
  return buffer == nullptr ? OB_ALLOCATE_MEMORY_FAILED : out.set_number(value, buffer, bytes);
}

int ObExprUtil::export_legacy_value(const ObExpr &expr, const Datum &value,
                                    ObIAllocator &owner, common::ObDatum &output)
{
  const DatumCopyPlan plan = expr.get_datum_copy_plan();
  int64_t bytes = 0;
  int64_t position = 0;
  int ret = plan.legacy_payload_size(value, bytes);
  char *buffer = nullptr;
  if (OB_SUCC(ret) && bytes > 0 && (buffer = static_cast<char *>(owner.alloc(bytes))) == nullptr) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  }
  if (OB_SUCC(ret)) { ret = plan.export_legacy(value, output, buffer, bytes, position); }
  return ret;
}

int ObExprUtil::set_array_element(const ObExpr &expr, ObEvalCtx &ctx,
                                  const ObIArrayType &array, const uint32_t element,
                                  Datum &out, const int64_t index)
{
  int ret = OB_SUCCESS;
  alignas(8) char scratch[64];
  common::ObDatum legacy;
  legacy.ptr_ = scratch;
  Datum value;
  const DatumCopyPlan plan = expr.get_datum_copy_plan();
  int64_t bytes = 0;
  int64_t position = 0;
  if (OB_FAIL(array.elem_at_datum(element, legacy))) {
  } else if (OB_FAIL(plan.import_legacy(legacy, value))) {
  } else if (OB_FAIL(plan.payload_copy_size(value, bytes))) {
  } else {
    char *buffer = bytes > 0 ? expr.get_reference_res_mem(ctx, bytes, index) : nullptr;
    if (bytes > 0 && buffer == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
    else { ret = plan.deep_copy(value, out, buffer, bytes, position); }
  }
  return ret;
}

int ObExprUtil::set_otimestamp_tiny(const ObExpr &expr, ObEvalCtx &ctx, Datum &out,
                                   const ObOTimestampData &value, const int64_t index)
{
  const uint32_t bytes = sizeof(ObOTimestampTinyData);
  char *buffer = expr.get_reference_res_mem(ctx, bytes, index);
  return buffer == nullptr ? OB_ALLOCATE_MEMORY_FAILED : out.set_otimestamp_tiny(value, buffer, bytes);
}

int ObExprUtil::set_decimal_int(const ObExpr &expr, ObEvalCtx &ctx, Datum &out,
                                const ObDecimalInt *coefficient, const int32_t bytes,
                                const int64_t index)
{
  if ((bytes > 0 && coefficient == nullptr)
      || (bytes != 0 && bytes != 4 && bytes != 8 && bytes != 16 && bytes != 32 && bytes != 64)) {
    return OB_INVALID_ARGUMENT;
  }
  const DatumCopyPlan &plan = expr.get_datum_copy_plan();
  const bool inline_decimal = expr.get_sql_type().canonical_representation() == DatumRepresentation::Inline64;
  char *buffer = inline_decimal || bytes == 0 ? nullptr : expr.get_reference_res_mem(ctx, bytes, index);
  return !inline_decimal && bytes > 0 && buffer == nullptr ? OB_ALLOCATE_MEMORY_FAILED
      : plan.set_decimal(coefficient, bytes, out, buffer, inline_decimal ? 0 : bytes);
}

int ObExprUtil::get_int64_from_value(const DatumValue &obj,
                                   ObExprCtx &expr_ctx,
                                   const bool is_trunc, //true: trunc; false: round
                                   int64_t &out)
{
  int ret = OB_SUCCESS;
  out = 0;
  SqlValueType type = obj.get_type();
  if (OB_LIKELY(ob_is_int_tc(type))) {
    out = obj.get_int();
  } else if (ob_is_uint_tc(type)) {
    uint64_t value = obj.get_uint64();
    out = (value > INT64_MAX) ? INT64_MAX : static_cast<int64_t>(value);
  } else if (OB_UNLIKELY(obj.is_number())) {
    number::ObNumber nmb = obj.get_number();
    ret = get_int64_from_num(nmb, expr_ctx, is_trunc, out);
  } else if (ob_is_decimal_int(obj.get_type())) {
    bool is_valid_int64 = false;
    ret = wide::check_range_valid_int64(obj.get_decimal_int(), obj.get_int_bytes(), is_valid_int64, out);
    if (OB_SUCC(ret) && !is_valid_int64) {
      out = INT64_MAX;
    }
  } else {
    // Except for type number, force conversion to number
    // and trunc to an integer
    number::ObNumber nmb;
    EXPR_DEFINE_CAST_CTX(expr_ctx, CM_NONE);
    DatumValue converted;
    if (expr_ctx.calc_buf_ == nullptr) { ret = OB_ERR_UNEXPECTED; }
    else if (OB_FAIL(DatumValueCaster::to_type(ObNumberType, cast_ctx, obj,
        *expr_ctx.calc_buf_, converted))) {
    } else if (converted.is_null() || !converted.is_number()) { ret = OB_OBJ_TYPE_ERROR; }
    else { nmb = converted.get_number(); }
    if (OB_SUCC(ret)) {
      ret = get_int64_from_num(nmb, expr_ctx, is_trunc, out);
    }
  }
  return ret;
}

int ObExprUtil::get_trunc_int64(const DatumValue &value, ObExprCtx &ctx, int64_t &out)
{ return get_int64_from_value(value, ctx, true, out); }
int ObExprUtil::get_round_int64(const DatumValue &value, ObExprCtx &ctx, int64_t &out)
{ return get_int64_from_value(value, ctx, false, out); }



int ObExprUtil::get_int64_from_num(number::ObNumber &nmb,
                                   ObExprCtx &expr_ctx,
                                   const bool is_trunc, //true: trunc; false: round
                                   int64_t &out)
{
  int ret = OB_SUCCESS;
  int64_t tmp_int = 0;
  uint64_t tmp_uint = 0;
  number::ObNumber *pnmb = &nmb;
  number::ObNumber newmb;
  if (OB_UNLIKELY(!nmb.is_integer())) {
    EXPR_DEFINE_CAST_CTX(expr_ctx, CM_NONE);
    //copy is essential since if we did not do that, obj will be modified
    if (OB_FAIL(newmb.from(nmb, cast_ctx))) {
    } else if (is_trunc && OB_FAIL(newmb.trunc(0))) {
    } else if (!is_trunc && OB_FAIL(newmb.round(0))) {
    } else {
      pnmb = &newmb;
    }
  }
  if (OB_FAIL(ret)) {
    //do nothing
  } else {
    OB_ASSERT(pnmb != nullptr);
    if (pnmb->is_valid_int64(tmp_int)) {
      out = tmp_int;
    } else if (pnmb->is_valid_uint64(tmp_uint)) {
      out = (tmp_uint > INT64_MAX) ? INT64_MAX : static_cast<int64_t>(tmp_uint);
    } else {
      // Even if number exceeds the INT64 value range, no error is reported,
      // select substr('abcd',-18446744073709551615) from dual; expect to return NULL
      out = INT64_MAX;
    }
  }
    // ret = OB_ERR_TRUNCATED_WRONG_VALUE;
    // if (CM_IS_WARN_ON_FAIL(expr_ctx.cast_mode_)) {
    //   ret = OB_SUCCESS;
    //   out = 0;
    // }
  return ret;
}

int ObExprUtil::trunc_num2int64(const common::number::ObNumber &nmb, int64_t &v)
{
  int ret = OB_SUCCESS;
  ObNumStackOnceAlloc tmp_alloc;
  number::ObNumber trunc_nmb;
  const number::ObNumber *nmb_val = &nmb;

  if (OB_UNLIKELY(!nmb.is_integer())) {
    if (OB_FAIL(trunc_nmb.from(nmb, tmp_alloc))) {
    } else if (OB_FAIL(trunc_nmb.trunc(0))) {
    } else {
      nmb_val = &trunc_nmb;
    }
  }

  if (OB_SUCC(ret)) {
    if (nmb_val->is_valid_int64(v)) {
      // %v is set, do nothing
    } else {
      // num_val out of int64 range, set to INT64_MIN or INT64_MAX
      v = nmb_val->is_negative() ? INT64_MIN : INT64_MAX;
    }
  }
  return ret;
}

int ObExprUtil::trunc_decint2int64(const ObDecimalInt *decint, const int32_t int_bytes, const ObScale scale, int64_t &v)
{
#define TRUNC_DECINT_CASE(len, int_type)                                                           \
  case len: {                                                                                      \
    ret = wide::scale_down_decimalint_for_trunc(*reinterpret_cast<const int_type *>(decint),       \
                                                scale, tmp_res);                                   \
  } break

  int ret = OB_SUCCESS;
  ObDecimalIntBuilder tmp_res;
  bool is_valid = false;
  switch (int_bytes) {
  TRUNC_DECINT_CASE(4, int32_t);
  TRUNC_DECINT_CASE(8, int64_t);
  TRUNC_DECINT_CASE(16, int128_t);
  TRUNC_DECINT_CASE(32, int256_t);
  TRUNC_DECINT_CASE(64, int512_t);
  default: {
    ret = OB_ERR_UNEXPECTED;
  }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(wide::check_range_valid_int64(tmp_res.get_decimal_int(), tmp_res.get_int_bytes(), is_valid, v))) {
  } else if (is_valid) {
    // do nothing
  } else {
    v = wide::is_negative(tmp_res.get_decimal_int(), tmp_res.get_int_bytes()) ? INT64_MIN : INT64_MAX;
  }

  return ret;
#undef TRUNC_DECINT_CASE
}

int ObExprUtil::round_num2int64(const common::number::ObNumber &nmb, int64_t &v)
{
  int ret = OB_SUCCESS;
  ObNumStackOnceAlloc tmp_alloc;
  number::ObNumber round_nmb;
  const number::ObNumber *nmb_val = &nmb;

  if (OB_UNLIKELY(!nmb.is_integer())) {
    if (OB_FAIL(round_nmb.from(nmb, tmp_alloc))) {
    } else if (OB_FAIL(round_nmb.round(0))) {
    } else {
      nmb_val = &round_nmb;
    }
  }

  if (OB_SUCC(ret)) {
    if (nmb_val->is_valid_int64(v)) {
      // %v is set, do nothing
    } else {
      // num_val out of int64 range, set to INT64_MIN or INT64_MAX
      v = nmb_val->is_negative() ? INT64_MIN : INT64_MAX;
    }
  }
  return ret;
}


int ObExprUtil::get_int_param_val(const Datum *datum, const ObExpr *input_expr, int64_t &int_val)
{
  int ret = OB_SUCCESS;
  if (NULL != datum && !datum->is_null()) {
    if (input_expr == nullptr) {
      ret = OB_ERR_UNEXPECTED;
    } else if (ob_is_decimal_int(input_expr->datum_meta_.type_)) {
      bool is_valid_int64 = false;
      alignas(8) char scratch[64];
      common::ObDatum coefficient;
      if (OB_FAIL(input_expr->get_datum_copy_plan().get_decimal(*datum, coefficient, scratch, sizeof(scratch)))) {
      } else if (OB_FAIL(wide::check_range_valid_int64(coefficient.get_decimal_int(), coefficient.get_int_bytes(),
                                                is_valid_int64, int_val))) {
      } else if (OB_UNLIKELY(!is_valid_int64)) {
        if (wide::is_negative(coefficient.get_decimal_int(), coefficient.get_int_bytes())) {
          int_val = INT64_MIN;
        } else {
          int_val = INT64_MAX;
        }
      }
    } else {
      int_val = datum->get_int();
    }
  }
  return ret;
}

int ObExprUtil::kmp(const char *pattern,
                    const int64_t pattern_len,
                    const char *text,
                    const int64_t text_len,
                    const int64_t nth_appearance,
                    const int32_t *next, /* calculated, size same with pattern */
                    int64_t &result)
{
  int ret = OB_SUCCESS;
  result = -1;
  if (OB_ISNULL(pattern) || OB_ISNULL(text) ||
       OB_UNLIKELY(pattern_len <= 0) || OB_UNLIKELY(text_len <= 0) || OB_ISNULL(next)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_LIKELY(pattern_len <= text_len)) {
    int64_t i = 0; // for pattern
    int64_t j = 0; // for text
    int64_t t = 0; // times matched
    while (j < text_len) {
      if (i == -1 || pattern[i] == text[j]) {
        ++j;
        if (++i == pattern_len) { // matched
          if (++t == nth_appearance) { // find nth apperance
            result = j - i;
            break;
          }
          // reset start pos to first character
          i = 0;
        }
      } else {
        i = next[i];
      }
    }
  }
  return ret;
}

int ObExprUtil::kmp_next(const char *pattern, const int64_t pattern_len, int32_t *next)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(pattern) || OB_UNLIKELY(pattern_len <= 0) || OB_ISNULL(next)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    // calc the kmp next array of pattern, next[i] represents the common maximum length of
    // the prefix and suffix of the substring before the (i-1)th position of the pattern.
    // for example, the next array of "ABCABB" is [-1, 0, 0, 0, 1, 2].
    // next[5] = 2, the substring is ABCAB, perfix set is {A,AB,ABC,ABCA},
    // suffix set is {B,AB,CAB,BCAB}, the maximun length of common substring is "AB",
    // which length is 2.
    next[0] = -1;
    int i = 0;
    int k = -1;
    while (i < pattern_len - 1) {
      if (k == -1 || pattern[i] == pattern[k]) {
        next[++i] = ++k;
      } else {
        k = next[k];
      }
    }
  }
  return ret;
}

int ObExprUtil::kmp_reverse(const char *pattern,
                            const int64_t pattern_len,
                            const char *text,
                            const int64_t text_len,
                            const int64_t nth_appearance,
                            const int32_t *next, /* calculated, size same with pattern */
                            int64_t &result)
{
  int ret = OB_SUCCESS;
  result = -1;
  if (OB_ISNULL(pattern) || OB_ISNULL(text) ||
       OB_UNLIKELY(pattern_len <= 0) || OB_UNLIKELY(text_len <= 0) || OB_ISNULL(next)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_LIKELY(pattern_len <= text_len)) {
    int64_t i = pattern_len - 1; // for pattern
    int64_t j = text_len - 1; // for text
    int64_t t = 0; // times matched
    while (j >= 0) {
      if (i == pattern_len || pattern[i] == text[j]) {
        --j;
        if (--i == -1) { // matched
          if (--t == nth_appearance) { // find nth apperance
            result = j + 1;
            break;
          }
          // reset start pos to last character
          i = pattern_len - 1;
        }
      } else {
        i = next[i];
      }
    }
  }
  return ret;
}

int ObExprUtil::kmp_next_reverse(const char *pattern,
                                 const int64_t pattern_len,
                                 int32_t *next)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(pattern) || OB_UNLIKELY(pattern_len <= 0) || OB_ISNULL(next)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    // reverse next array for kmp, ABCABB's reverse next array is [4, 5, 5, 4, 5, 6].
    next[pattern_len - 1] = pattern_len;
    int i = pattern_len - 1;
    int k = pattern_len;
    while (i >= 1) {
      if (k == pattern_len || pattern[i] == pattern[k]) {
        next[--i] = --k;
      } else {
        k = next[k];
      }
    }
  }
  return ret;
}
// Get the byte offset of each character in the multiple bytes string
// as well as the number of bytes each mb character occupies
// byte_offsets: the first element is the starting byte position of the second character, the last element is the length of the string
// byte_num each element corresponds to the number of bytes each character in the input str occupies
// ori_str: abc
// ori_str_byte_offsets: [0, 1, 2, 3]
// ori_str_byte_num: [1, 1, 1]
int ObExprUtil::get_mb_str_info(const ObString &str,
                                ObCollationType cs_type,
                                ObIArray<size_t> &byte_num,
                                ObIArray<size_t> &byte_offsets)
{
  int ret = OB_SUCCESS;
  size_t byte_index = 0;
  size_t str_char_len = -1;
  int64_t well_formed_len = 0;
  if (CS_TYPE_INVALID == cs_type) {
    ret = OB_INVALID_ARGUMENT;
  } else if (0 == str.length()) {
    // do nothing
  } else if (OB_ISNULL(str.ptr())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObCharset::well_formed_len(cs_type,
                                                str.ptr(),
                                                str.length(),
                                                well_formed_len))) {
  } else {
    if (OB_FAIL(byte_offsets.push_back(0))) {
    } else {
      str_char_len = ObCharset::strlen_char(cs_type, str.ptr(), str.length());
      int64_t last_byte_index = 0;
      while (OB_SUCC(ret) && (byte_index < str.length())) {
        byte_index = ObCharset::charpos(cs_type, str.ptr() + last_byte_index,
                                        str.length() - last_byte_index, 1);
        byte_index = last_byte_index + byte_index;
        last_byte_index = byte_index;
        if (OB_FAIL(byte_offsets.push_back(byte_index))) {
        }
      }
      // Get the number of bytes each character occupies
      for (size_t i = 1; OB_SUCC(ret) && (i < byte_offsets.count()); ++i) {
        if (OB_FAIL(byte_num.push_back(byte_offsets.at(i) - byte_offsets.at(i-1)))) {
        }
      }
    }
  }
  return ret;
}

double ObExprUtil::round_double(double val, int64_t dec)
{
  const double pow_val = std::pow(10.0, static_cast<double>(std::abs(dec)));
  double val_div_tmp = val / pow_val;
  double val_mul_tmp = val * pow_val;
  double res = 0.0;
  if (dec < 0 && std::isinf(pow_val)) {
    res = 0.0;
  } else if (dec >= 0 && !std::isfinite(val_mul_tmp)) {
    res = val;
  } else {
    res = dec < 0 ? rint(val_div_tmp) * pow_val : rint(val_mul_tmp) / pow_val;
  }
  return res;
}

uint64_t ObExprUtil::round_uint64(uint64_t val, int64_t dec)
{
  uint64_t res = 0;
  if (dec >= 0) {
    // Integer has no decimal places, directly assign
    res = val;
  } else {
    dec = -dec;
    // 20 is digit num of (UINT64_MAX)18446744073709551615
    if (dec >= 20) {
      res = 0;
    } else {
      // val: 250, dec: 2, div: 100, tmp: 200
      uint64_t div = 1;
      for (int64_t i = 0; i < dec; ++i) {
        div *= 10;
      }
      uint64_t tmp = val / div * div;
      res = (val - tmp) >= (div / 2) ? tmp + div : tmp;
    }
  }
  return res;
}

double ObExprUtil::trunc_double(double val, int64_t dec)
{
  const double pow_val = std::pow(10, static_cast<double>(std::abs(dec)));
  volatile double val_div_tmp = val / pow_val;
  volatile double val_mul_tmp = val * pow_val;
  volatile double res = 0.0;
  if (dec < 0 && std::isinf(pow_val)) {
    res = 0.0;
  } else if (dec >= 0 && !std::isfinite(val_mul_tmp)) {
    res = val;
  } else {
    if (val >= 0) {
      res = dec < 0 ? floor(val_div_tmp) * pow_val : floor(val_mul_tmp) / pow_val;
    } else {
      res = dec < 0 ? ceil(val_div_tmp) * pow_val : ceil(val_mul_tmp) / pow_val;
    }
  }
  return res;
}

int ObExprUtil::set_expr_asscii_result(const ObExpr &expr, ObEvalCtx &ctx,
                                          ObTextStringDatumResult &out_res,
                                          const ObString &ascii, const int64_t datum_idx,
                                          const bool is_ascii,
                                          const common::ObCollationType src_coll_type)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator temp_allocator(ObModIds::OB_LOB_ACCESS_BUFFER, OB_MALLOC_NORMAL_BLOCK_SIZE);
  ObString out;
  if (ascii.empty()) {
    if (OB_FAIL(out_res.init_with_batch_idx(0, datum_idx))) {
    } else {
      out_res.set_result();
    }
  } else {
    if (is_ascii && !ObCharset::is_cs_nonascii(expr.datum_meta_.cs_type_)) {
      out = ascii;
    } else if (OB_FAIL(ObCharset::charset_convert(temp_allocator, ascii, src_coll_type,
                                                  expr.datum_meta_.cs_type_, out))) {
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(out_res.init_with_batch_idx(out.length(), datum_idx))) {
    } else if (OB_FAIL(out_res.append(out.ptr(), out.length()))) {
    } else {
      out_res.set_result();
    }
  }
  return ret;
}

int ObExprUtil::set_expr_ascii_result(const ObExpr &expr, ObEvalCtx &ctx, Datum &expr_datum,
                                      const ObString &ascii, const int64_t datum_idx,
                                      const bool is_ascii,
                                      const common::ObCollationType src_coll_type)
{
  int ret = OB_SUCCESS;
  if (!ob_is_text_tc(expr.datum_meta_.type_)) {
    if (ascii.empty()) {
      expr_datum.set_string(ObString());
    } else {
      ObArenaAllocator temp_allocator;
      ObString out;
      char *buf = NULL;
      if (is_ascii && !ObCharset::is_cs_nonascii(expr.datum_meta_.cs_type_)) {
        out = ascii;
      } else if (OB_FAIL(ObCharset::charset_convert(temp_allocator, ascii, src_coll_type,
                                            expr.datum_meta_.cs_type_, out))) {
      }
      if (OB_FAIL(ret)) {
      } else if (OB_ISNULL(buf = expr.get_str_res_mem(ctx, out.length(), datum_idx))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        MEMCPY(buf, out.ptr(), out.length());
        expr_datum.set_string(buf, out.length());
      }
    }
  } else { // text tc
    ObArenaAllocator temp_allocator(ObModIds::OB_LOB_ACCESS_BUFFER, OB_MALLOC_NORMAL_BLOCK_SIZE);
    ObString out;
    char *buf = NULL;
    ObTextStringDatumResult res(expr.datum_meta_.type_, &expr, &ctx, &expr_datum);
    if (ascii.empty()) {
      if (OB_FAIL(res.init_with_batch_idx(0, datum_idx))) {
      } else {
        res.set_result();
      }
    } else {
      if (is_ascii && !ObCharset::is_cs_nonascii(expr.datum_meta_.cs_type_)) {
        out = ascii;
      } else if (OB_FAIL(ObCharset::charset_convert(temp_allocator, ascii, src_coll_type,
                                                    expr.datum_meta_.cs_type_, out))) {
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(res.init_with_batch_idx(out.length(), datum_idx))) {
      } else if (OB_FAIL(res.append(out.ptr(), out.length()))) {
      } else {
        res.set_result();
      }
    }
  }
  return ret;
}

// for non batch mode
int ObExprUtil::set_expr_ascii_result(const ObExpr &expr, ObEvalCtx &ctx, Datum &expr_datum,
                                      const ObString &ascii_str, const bool is_ascii,
                                      const common::ObCollationType src_coll_type)
{
  return set_expr_ascii_result(expr, ctx, expr_datum, ascii_str, ctx.get_batch_idx(),
                               is_ascii, src_coll_type);
}

int ObExprUtil::need_convert_string_collation(const ObCollationType &in_collation,
                                              const ObCollationType &out_collation,
                                              bool &need_convert)
{
  int ret = OB_SUCCESS;
  need_convert = true;
  if (OB_UNLIKELY(!ObCharset::is_valid_collation(in_collation)
                  || !ObCharset::is_valid_collation(out_collation))) {
    ret = OB_INVALID_ARGUMENT;
  } else if (ObCharset::charset_type_by_coll(in_collation)
             == ObCharset::charset_type_by_coll(out_collation) ||
             CS_TYPE_BINARY == in_collation ||
             CS_TYPE_BINARY == out_collation) {
    need_convert = false;
  } else {
    // do nothing
  }
  return ret;
}


int ObExprUtil::convert_string_collation(const ObString &in_str,
                                         const ObCollationType &in_collation,
                                         ObString &out_str,
                                         const ObCollationType &out_collation,
                                         ObIAllocator &alloc)
{
  int ret = OB_SUCCESS;
  bool need_convert = false;
  if (in_str.empty()) {
    out_str.reset();
  } else if (OB_FAIL(need_convert_string_collation(in_collation, out_collation,
                                                   need_convert))) {
  } else if (!need_convert) {
    out_str = in_str;
  } else {
    char *buf = NULL;
    int32_t buf_len = in_str.length() * ObCharset::CharConvertFactorNum;
    uint32_t result_len = 0;
    if (OB_ISNULL(buf = static_cast<char*>(alloc.alloc(buf_len)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_FAIL(ObCharset::charset_convert(in_collation, in_str.ptr(), in_str.length(),
                                                  out_collation, buf, buf_len,
                                                  result_len))) {
    } else {
      out_str.assign_ptr(buf, result_len);
    }
  }
  return ret;
}

int ObExprUtil::convert_string_collation_for_regexp(const ObString &in_str,
                                                    const ObCollationType &in_collation,
                                                    ObString &out_str,
                                                    const ObCollationType &out_collation,
                                                    ObIAllocator &alloc)
{
  int ret = OB_SUCCESS;
  if (in_str.empty()) {
    out_str.reset();
  } else if (OB_UNLIKELY(!ObCharset::is_valid_collation(in_collation) ||
                         !ObCharset::is_valid_collation(out_collation))) {
    ret = OB_INVALID_ARGUMENT;
  } else if (ObCharset::charset_type_by_coll(in_collation) ==
             ObCharset::charset_type_by_coll(out_collation)) {
    out_str = in_str;
  } else {
    char *buf = NULL;
    int32_t buf_len = in_str.length() * ObCharset::CharConvertFactorNum;
    uint32_t result_len = 0;
    if (OB_ISNULL(buf = static_cast<char*>(alloc.alloc(buf_len)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_FAIL(ObCharset::charset_convert(in_collation, in_str.ptr(), in_str.length(),
                                                  out_collation, buf, buf_len,
                                                  result_len))) {
    } else {
      out_str.assign_ptr(buf, result_len);
    }
  }
  return ret;
}

int ObExprUtil::deep_copy_str(const ObString &src, ObString &out, ObIAllocator &alloc)
{
  int ret = OB_SUCCESS;
  char *buf = reinterpret_cast<char*>(alloc.alloc(src.length()));
  OV(OB_NOT_NULL(buf), OB_ALLOCATE_MEMORY_FAILED);
  if (OB_SUCC(ret)) {
    MEMCPY(buf, src.ptr(), src.length());
    out.assign_ptr(buf, src.length());
  }
  return ret;
}

int ObExprUtil::eval_stack_overflow_check(const ObExpr &expr,
                                          ObEvalCtx &ctx,
                                          Datum &)
{
  int ret = OB_SUCCESS;

  // control the occupied stack size by event to simulate stack overflow in testing.
  int stack_size = OB_E(EventTable::EN_STACK_OVERFLOW_CHECK_EXPR_STACK_SIZE) 0;
  stack_size = std::abs(stack_size);
  char *cur_stack[stack_size];
  if (stack_size > 0) {
  }

  if (OB_FAIL(SMART_CALL(expr.eval_param_value(ctx)))) {
  } else {
    // Since stack overflow check expr has the same Datum with child,
    // need do nothing here.
  }
  return ret;
}

int ObExprUtil::eval_batch_stack_overflow_check(const ObExpr &expr,
                                                ObEvalCtx &ctx,
                                                const ObBitVector &skip,
                                                const int64_t batch_size)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_stack_overflow())) {
  } else if (OB_FAIL(expr.args_[0]->eval_batch(ctx, skip, batch_size))) {
  } else {
    // do nothing
  }
  return ret;
}


int ObExprUtil::get_real_expr_without_cast(const ObExpr *expr, const ObExpr *&out_expr)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(expr)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    while (T_FUN_SYS_CAST == expr->type_ && CM_IS_IMPLICIT_CAST(expr->extra_)) {
      if (OB_UNLIKELY(2 != expr->arg_cnt_)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_ISNULL(expr = expr->args_[0])) {
        ret = OB_ERR_UNEXPECTED;
      }
    }
    if (OB_SUCC(ret)) {
      out_expr = expr;
    }
  }
  return ret;
}

int ObSolidifiedVarsContext::get_local_tz_info(const sql::ObBasicSessionInfo *session, const common::ObTimeZoneInfo *&tz_info)
{
  int ret = OB_SUCCESS;
  tz_info = NULL;
  if (NULL != local_session_var_ && NULL == local_tz_wrap_) {
    ObSessionSysVar *local_var = NULL;
    //init local tz_wrap
    if (OB_FAIL(local_session_var_->get_local_var(SYS_VAR_TIME_ZONE, local_var))) {
    } else if (NULL != local_var) {
      const ObTZInfoMap *tz_info_map = NULL;
      if (OB_ISNULL(tz_info_map = session->get_timezone_info()->get_tz_info_map())) {
        ObTZMapWrap tz_map_wrap;
        if (OB_SUCC(OTTZ_MGR.get_timezone_map(tz_map_wrap))) {
          tz_info_map = tz_map_wrap.get_tz_map();
        } else {
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_ISNULL(local_tz_wrap_ = OB_NEWx(ObTimeZoneInfoWrap, alloc_))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else if (OB_FAIL(local_tz_wrap_->init_time_zone(local_var->get_value_datum().get_string(),
                                        OB_INVALID_VERSION,
                                        *(const_cast<ObTZInfoMap *>(tz_info_map))))) {
        }
      }
    }
  }
  if (NULL != local_tz_wrap_) {
    tz_info = local_tz_wrap_->get_time_zone_info();
  }
  return ret;
}

DEF_TO_STRING(ObSolidifiedVarsContext)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(local_session_var));
  J_OBJ_END();
  return pos;
}

ObSolidifiedVarsGetter::ObSolidifiedVarsGetter(const ObExpr &expr, const ObEvalCtx &ctx, const ObBasicSessionInfo *session)
  : local_session_var_(NULL),
  session_(session)
{
  ObPhysicalPlanCtx * phy_ctx = ctx.exec_ctx_.get_physical_plan_ctx();
  if (OB_NOT_NULL(phy_ctx)) {
    if (OB_SUCCESS == (phy_ctx->get_local_session_vars(expr.local_session_var_id_, local_session_var_))) {
    }
  }
}

int ObSolidifiedVarsGetter::get_dtc_params(ObDataTypeCastParams &dtc_params)
{
  int ret = OB_SUCCESS;
  bool is_valid = false;
  const common::ObTimeZoneInfo *tz_info = NULL;
  dtc_params = ObBasicSessionInfo::create_dtc_params(session_);
  if (OB_ISNULL(session_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(get_time_zone_info(tz_info))) {
  } else if (NULL != local_session_var_
             && OB_FAIL(ObSQLUtils::merge_solidified_var_into_dtc_params(local_session_var_->get_local_vars(),
                                                                  tz_info,
                                                                  dtc_params))) {
  }
  return ret;
}

int ObSolidifiedVarsGetter::get_time_zone_info(const common::ObTimeZoneInfo *&tz_info)
{
  int ret = OB_SUCCESS;
  bool is_valid = false;
  ObSessionSysVar *local_var = NULL;
  tz_info = NULL;
  if (OB_ISNULL(session_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (NULL != local_session_var_
             && OB_FAIL(const_cast<ObSolidifiedVarsContext *>(local_session_var_)->get_local_tz_info(session_, tz_info))) {
  } else if (NULL == tz_info) {
    tz_info = session_->get_timezone_info();
  }
  return ret;
}

int ObSolidifiedVarsGetter::get_sql_mode(ObSQLMode &sql_mode)
{
  int ret = OB_SUCCESS;
  bool is_valid = false;
  if (OB_ISNULL(session_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (FALSE_IT(sql_mode = session_->get_sql_mode())) {
  } else if (NULL != local_session_var_
             && OB_FAIL(ObSQLUtils::merge_solidified_var_into_sql_mode(local_session_var_->get_local_vars(),
                                                                       sql_mode))) {
  }
  return ret;
}

int ObSolidifiedVarsGetter::get_local_var(ObSysVarClassType var_type, ObSessionSysVar *&sys_var)
{
  int ret = OB_SUCCESS;
  sys_var = NULL;
  if (NULL != local_session_var_
      && NULL != local_session_var_->get_local_vars()
      && OB_FAIL(local_session_var_->get_local_vars()->get_local_var(var_type, sys_var))) {
  }
  return ret;
}


int ObSolidifiedVarsGetter::get_max_allowed_packet(int64_t &max_size)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(session_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (FALSE_IT(session_->get_max_allowed_packet(max_size))) {
  } else if (NULL != local_session_var_
             && OB_FAIL(ObSQLUtils::merge_solidified_var_into_max_allowed_packet(local_session_var_->get_local_vars(),
                                                                              max_size))) {
  }
  return ret;
}
