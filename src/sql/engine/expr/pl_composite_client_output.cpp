/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 */

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/expr/pl_composite_client_output.h"
#include "lib/utility/ob_common_utility.h"
#include "sql/engine/expr/ob_expr_json_func_helper.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/ob_result_set.h"
#include "sql/pl/ob_pl.h"
#include "sql/pl/ob_pl_resolver.h"
#include "sql/pl/ob_pl_user_type.h"

namespace oceanbase
{
using namespace common;
namespace sql
{
namespace
{
// Streaming the containers avoids building a second copy of the entire PL
// graph. JSON leaf conversion and escaping use the ordinary SQL JSON rules.
class CompositeJsonWriter final
{
public:
  CompositeJsonWriter(ObIAllocator &allocator, const pl::ObPLINS *type_namespace,
                      ObBasicSessionInfo &session, const ObLobReadOptions *lob_options,
                      ObExecContext *exec_ctx)
      : allocator_(allocator), type_namespace_(type_namespace), session_(session),
        lob_options_(lob_options), exec_ctx_(exec_ctx), buffer_(&allocator),
        max_depth_(ObJsonExprHelper::get_json_max_depth_config()) {}

  int write(const ObObj &value, uint32_t depth = 0, bool is_boolean = false)
  {
    int ret = OB_SUCCESS;
    bool stack_overflow = false;
    if (OB_FAIL(check_stack_overflow(stack_overflow))) {
    } else if (stack_overflow) {
      ret = OB_SIZE_OVERFLOW;
    } else if (depth >= max_depth_) {
      // This also bounds a malformed/cyclic PL graph before recursing forever.
      ret = OB_ERR_JSON_OUT_OF_DEPTH;
    } else if (value.is_null()) {
      ret = buffer_.append("null");
    } else if (value.is_ext()) {
      if (!PlCompositeClientOutput::needs_conversion(value.get_meta())) {
        ret = OB_NOT_SUPPORTED;
      } else if (0 == value.get_ext()) {
        ret = buffer_.append("null");
      } else {
        const pl::ObPLComposite *composite =
            reinterpret_cast<const pl::ObPLComposite *>(value.get_ext());
        if (composite->get_type() != value.get_meta().get_extend_type()) {
          ret = OB_ERR_UNEXPECTED;
        } else if (composite->is_null()) {
          ret = buffer_.append("null");
        } else if (composite->is_record()) {
          ret = write_record(*static_cast<const pl::ObPLRecord *>(composite), depth);
        } else if (composite->is_associative_array()) {
          // There is no key-bearing associative-array runtime in this build.
          // Do not reinterpret a removed layout or discard its keys as an array.
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "PL associative-array client output without key metadata");
        } else {
          ret = write_collection(*static_cast<const pl::ObPLCollection *>(composite), depth);
        }
      }
    } else {
      ObDatum datum;
      // from_obj borrows string payloads, but copies fixed-width values and
      // NUMBER/DECIMAL payloads into caller-provided storage.
      alignas(uint64_t) char datum_buffer[OBJ_DATUM_MAX_RES_SIZE];
      datum.ptr_ = datum_buffer;
      ObObjMeta meta = value.get_meta();
      ObIJsonBase *json = nullptr;
      // In-memory PL values do not require a storage runtime. Resolve the LOB
      // service only for a leaf whose JSON conversion can read LOB data.
      if (is_lob_storage(value.get_type()) && nullptr == lob_options_
          && nullptr != exec_ctx_
          && OB_FAIL(exec_ctx_->get_lob_read_options(lob_options_))) {
      } else if (OB_FAIL(datum.from_obj(value))) {
      } else if (OB_FAIL(ObJsonExprHelper::datum_to_json_val(
                     &datum, meta, &allocator_, &session_, json, is_boolean,
                     false, true, false, lob_options_))) {
      } else if (OB_ISNULL(json)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (depth + json->depth() > max_depth_) {
        ret = OB_ERR_JSON_OUT_OF_DEPTH;
      } else if (OB_FAIL(json->print(buffer_, true))) {
      }
    }
    return ret;
  }

  int take_result(ObString &result) { return buffer_.get_result_string(result); }

private:
  int write_record(const pl::ObPLRecord &record, uint32_t depth)
  {
    int ret = OB_SUCCESS;
    const pl::ObUserDefinedType *user_type = nullptr;
    if (!record.is_inited()) {
      ret = buffer_.append("null");
    } else if (OB_ISNULL(type_namespace_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(type_namespace_->get_user_type(record.get_id(), user_type, &allocator_))) {
    } else if (OB_ISNULL(user_type) || !user_type->is_record_type()) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      const pl::ObRecordType &type = *static_cast<const pl::ObRecordType *>(user_type);
      if (type.get_record_member_count() != record.get_count()) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(buffer_.append("{"))) {
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < record.get_count(); ++i) {
        const ObString *name = type.get_record_member_name(i);
        const pl::ObPLDataType *member_type = type.get_record_member_type(i);
        ObObj element;
        if (OB_ISNULL(name) || OB_ISNULL(member_type)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (i > 0 && OB_FAIL(buffer_.append(","))) {
        } else {
          ObJsonString json_name(name->ptr(), name->length());
          if (OB_FAIL(json_name.print(buffer_, true))) {
          } else if (OB_FAIL(buffer_.append(":"))) {
          } else if (OB_FAIL(record.get_element(i, element))) {
          } else if (OB_FAIL(write(element, depth + 1, member_type->is_boolean_type()))) {
          }
        }
      }
      if (OB_SUCC(ret)) {
        ret = buffer_.append("}");
      }
    }
    return ret;
  }

  int write_collection(const pl::ObPLCollection &collection, uint32_t depth)
  {
    int ret = OB_SUCCESS;
    if (!collection.is_inited()) {
      ret = buffer_.append("null");
    } else if (collection.get_count() < 0
               || (collection.get_count() > 0 && OB_ISNULL(collection.get_data()))) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(buffer_.append("["))) {
    } else {
      bool first = true;
      for (int64_t i = 0; OB_SUCC(ret) && i < collection.get_count(); ++i) {
        const ObObj &element = collection.get_data()[i];
        if (element.is_invalid_type()) {
          // Deleted nested-table slots are absent elements, not SQL NULLs.
        } else if (!first && OB_FAIL(buffer_.append(","))) {
        } else if (OB_FAIL(write(element, depth + 1,
                       pl::PL_OBJ_TYPE == collection.get_element_desc().get_pl_type()
                       && collection.get_element_type().get_meta_type().is_tinyint()))) {
        } else {
          first = false;
        }
      }
      if (OB_SUCC(ret)) {
        ret = buffer_.append("]");
      }
    }
    return ret;
  }

  ObIAllocator &allocator_;
  const pl::ObPLINS *type_namespace_;
  ObBasicSessionInfo &session_;
  const ObLobReadOptions *lob_options_;
  ObExecContext *exec_ctx_;
  ObJsonBuffer buffer_;
  const uint32_t max_depth_;
};
int convert_composite(ObObj &value, ObIAllocator &allocator,
                      const pl::ObPLINS *type_namespace, ObBasicSessionInfo &session,
                      const ObLobReadOptions *lob_options, ObExecContext *exec_ctx)
{
  int ret = OB_SUCCESS;
  if (PlCompositeClientOutput::needs_conversion(value.get_meta())) {
    // Do not turn an atomically NULL composite into a non-NULL string "null".
    if (pl::ObPLComposite::obj_is_null(&value)) {
      value.set_null();
    } else {
      CompositeJsonWriter writer(allocator, type_namespace, session, lob_options, exec_ctx);
      ObString result;
      if (OB_FAIL(writer.write(value))) {
      } else if (OB_FAIL(writer.take_result(result))) {
      } else {
        // The buffer belongs to the caller's output allocator. The original PL
        // object and its member values are never changed or freed here.
        value.set_varchar(result);
        value.set_collation_type(CS_TYPE_UTF8MB4_BIN);
        value.set_collation_level(CS_LEVEL_IMPLICIT);
      }
    }
  }
  return ret;
}
} // namespace

bool PlCompositeClientOutput::needs_conversion(const ObObjMeta &meta)
{
  return meta.is_ext()
      && (pl::PL_RECORD_TYPE == meta.get_extend_type()
          || pl::PL_NESTED_TABLE_TYPE == meta.get_extend_type()
          || pl::PL_VARRAY_TYPE == meta.get_extend_type()
          || pl::PL_ASSOCIATIVE_ARRAY_TYPE == meta.get_extend_type());
}

int PlCompositeClientOutput::convert(ObObj &value, ObIAllocator &allocator,
                                    const pl::ObPLINS *type_namespace,
                                    ObBasicSessionInfo &session,
                                    const ObLobReadOptions *lob_options)
{
  return convert_composite(value, allocator, type_namespace, session, lob_options, nullptr);
}

int PlCompositeClientOutput::convert(ObObj &value, ObIAllocator &allocator,
                                    ObExecContext &exec_ctx)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = exec_ctx.get_my_session();
  if (!needs_conversion(value.get_meta())) {
  } else if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (nullptr != session->get_pl_context()) {
    ret = convert_composite(value, allocator, session->get_pl_context()->get_current_ctx(),
                            *session, nullptr, &exec_ctx);
  } else {
    pl::ObPLPackageGuard *package_guard = nullptr;
    if (OB_ISNULL(exec_ctx.get_sql_ctx())
        || OB_ISNULL(exec_ctx.get_sql_ctx()->schema_guard_)
        || OB_ISNULL(exec_ctx.get_sql_proxy())) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(exec_ctx.get_package_guard(package_guard))) {
    } else if (OB_ISNULL(package_guard)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      pl::ObPLResolveCtx resolve_ctx(allocator, *session,
          *exec_ctx.get_sql_ctx()->schema_guard_, *package_guard,
          *exec_ctx.get_sql_proxy(), false);
      resolve_ctx.params_.plan_cache_ = exec_ctx.get_plan_cache();
      resolve_ctx.params_.pl_sql_runtime_ = exec_ctx.get_pl_sql_runtime();
      resolve_ctx.params_.pl_engine_ = exec_ctx.get_pl_engine();
      resolve_ctx.params_.srs_provider_ = exec_ctx.get_srs_provider();
      resolve_ctx.params_.lob_read_service_ = exec_ctx.get_lob_read_service();
      ret = convert_composite(value, allocator, &resolve_ctx, *session, nullptr, &exec_ctx);
    }
  }
  return ret;
}

int PlCompositeClientOutput::convert(ObObj &value, ObResultSet &result)
{
  int ret = OB_SUCCESS;
  if (needs_conversion(value.get_meta())) {
    ObArenaAllocator *allocator = nullptr;
    if (OB_FAIL(result.get_exec_context().get_convert_charset_allocator(allocator))) {
    } else if (OB_ISNULL(allocator)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ret = convert(value, *allocator, result.get_exec_context());
    }
  }
  return ret;
}
} // namespace sql
} // namespace oceanbase
