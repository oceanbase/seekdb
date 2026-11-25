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
#include "sql/engine/expr/ob_expr_sql_udt_utils.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/ob_spi.h"
#include "sql/pl/ob_pl_resolver.h"
#include "sql/pl/ob_pl_user_type.h"
#include "sql/pl/ob_pl_package_guard.h"
#include "lib/string/ob_string_buffer.h"
#include <cmath>

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{

namespace
{

// Encodes one PL composite value as JSON text.
//   record                  -> JSON object keyed by the record field names
//   nested table / varray   -> JSON array, in index order, deleted slots skipped
//   NULL inside a composite -> JSON null
//   nested composite        -> recursion
// Associative arrays and leaf types without a JSON mapping are rejected with an
// explicit error instead of producing a lossy value.
class ObPLCompositeJsonWriter
{
public:
  ObPLCompositeJsonWriter(const pl::ObPLINS &ns,
                          const ObDataTypeCastParams &dtc_params,
                          ObIAllocator &allocator,
                          ObStringBuffer &buf)
    : ns_(ns), dtc_params_(dtc_params), allocator_(allocator), buf_(buf)
  {}
  int write_value(const ObObj &obj, const int64_t depth);

private:
  static const int64_t MAX_DEPTH = 64;
  int write_composite(const ObObj &obj, const int64_t depth);
  int write_record(const pl::ObPLRecord &record, const int64_t depth);
  int write_collection(const pl::ObPLCollection &coll, const int64_t depth);
  int write_scalar(const ObObj &obj);
  int write_json_string(const ObString &str);

private:
  const pl::ObPLINS &ns_;
  const ObDataTypeCastParams &dtc_params_;
  ObIAllocator &allocator_;
  ObStringBuffer &buf_;
};

int ObPLCompositeJsonWriter::write_json_string(const ObString &str)
{
  int ret = OB_SUCCESS;
  const char *ptr = str.ptr();
  const int64_t len = str.length();
  int64_t run_start = 0;
  if (OB_FAIL(buf_.append("\""))) {
    LOG_WARN("append json string head failed", K(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < len; ++i) {
    const unsigned char c = static_cast<unsigned char>(ptr[i]);
    char tmp[8];
    const char *esc = NULL;
    switch (c) {
      case '"':  esc = "\\\""; break;
      case '\\': esc = "\\\\"; break;
      case '\b': esc = "\\b";  break;
      case '\f': esc = "\\f";  break;
      case '\n': esc = "\\n";  break;
      case '\r': esc = "\\r";  break;
      case '\t': esc = "\\t";  break;
      default:
        if (c < 0x20) {
          snprintf(tmp, sizeof(tmp), "\\u%04x", c);
          esc = tmp;
        }
        break;
    }
    if (NULL != esc) {
      if (i > run_start && OB_FAIL(buf_.append(ptr + run_start, i - run_start))) {
        LOG_WARN("append json string run failed", K(ret));
      } else if (OB_FAIL(buf_.append(esc))) {
        LOG_WARN("append json escape failed", K(ret));
      } else {
        run_start = i + 1;
      }
    }
  }
  if (OB_SUCC(ret) && len > run_start
      && OB_FAIL(buf_.append(ptr + run_start, len - run_start))) {
    LOG_WARN("append json string tail failed", K(ret));
  }
  if (OB_SUCC(ret) && OB_FAIL(buf_.append("\""))) {
    LOG_WARN("append json string end failed", K(ret));
  }
  return ret;
}

int ObPLCompositeJsonWriter::write_scalar(const ObObj &obj)
{
  int ret = OB_SUCCESS;
  bool as_number = false;
  switch (obj.get_type_class()) {
    case ObIntTC:
    case ObUIntTC:
    case ObNumberTC:
    case ObDecimalIntTC:
      as_number = true;
      break;
    case ObFloatTC:
      as_number = true;
      if (!std::isfinite(obj.get_float())) {
        ret = OB_NOT_SUPPORTED;
      }
      break;
    case ObDoubleTC:
      as_number = true;
      if (!std::isfinite(obj.get_double())) {
        ret = OB_NOT_SUPPORTED;
      }
      break;
    case ObStringTC:
    case ObDateTimeTC:
    case ObDateTC:
    case ObTimeTC:
    case ObYearTC:
    case ObMySQLDateTC:
    case ObMySQLDateTimeTC:
      break;
    default:
      ret = OB_NOT_SUPPORTED;
      break;
  }
  if (OB_NOT_SUPPORTED == ret) {
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "PL composite element type in a client result");
    LOG_WARN("element type has no JSON mapping", K(ret), K(obj.get_type()));
  } else if (OB_SUCC(ret)) {
    ObObj str_obj;
    ObCastCtx cast_ctx(&allocator_, &dtc_params_, CM_NONE, CS_TYPE_UTF8MB4_GENERAL_CI);
    if (OB_FAIL(ObObjCaster::to_type(ObVarcharType, cast_ctx, obj, str_obj))) {
      LOG_WARN("cast element to text failed", K(ret), K(obj));
    } else if (as_number) {
      ret = buf_.append(str_obj.get_string());
    } else {
      ret = write_json_string(str_obj.get_string());
    }
  }
  return ret;
}

int ObPLCompositeJsonWriter::write_record(const pl::ObPLRecord &record, const int64_t depth)
{
  int ret = OB_SUCCESS;
  const pl::ObUserDefinedType *udt = NULL;
  const pl::ObRecordType *record_type = NULL;
  if (!record.is_inited()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("record is not inited", K(ret), K(record));
  } else if (OB_FAIL(ns_.get_user_type(record.get_id(), udt, &allocator_))) {
    LOG_WARN("get record type failed", K(ret), K(record.get_id()));
  } else if (OB_ISNULL(udt) || !udt->is_record_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("not a record type", K(ret), K(record.get_id()));
  } else if (FALSE_IT(record_type = static_cast<const pl::ObRecordType *>(udt))) {
  } else if (record.get_count() != record_type->get_record_member_count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("record field count mismatch", K(ret), K(record.get_count()),
             K(record_type->get_record_member_count()));
  } else if (OB_FAIL(buf_.append("{"))) {
    LOG_WARN("append record head failed", K(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < record.get_count(); ++i) {
    const ObString *name = record_type->get_record_member_name(i);
    ObObj elem;
    if (OB_ISNULL(name)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("record field name is null", K(ret), K(i));
    } else if (i > 0 && OB_FAIL(buf_.append(","))) {
      LOG_WARN("append separator failed", K(ret));
    } else if (OB_FAIL(write_json_string(*name))) {
    } else if (OB_FAIL(buf_.append(":"))) {
      LOG_WARN("append colon failed", K(ret));
    } else if (OB_FAIL(record.get_element(i, elem))) {
      LOG_WARN("get record element failed", K(ret), K(i));
    } else if (OB_FAIL(write_value(elem, depth + 1))) {
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(buf_.append("}"))) {
    LOG_WARN("append record end failed", K(ret));
  }
  return ret;
}

int ObPLCompositeJsonWriter::write_collection(const pl::ObPLCollection &coll, const int64_t depth)
{
  int ret = OB_SUCCESS;
  bool first = true;
  if (OB_FAIL(buf_.append("["))) {
    LOG_WARN("append array head failed", K(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < coll.get_count(); ++i) {
    const ObObj *elem = coll.get_data() + i;
    bool is_deleted = false;
    if (OB_ISNULL(coll.get_data())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("collection data is null", K(ret), K(coll));
    } else if (OB_FAIL(coll.is_elem_deleted(i, is_deleted))) {
      LOG_WARN("check deleted element failed", K(ret), K(i));
    } else if (is_deleted) {
      // deleted slots of a nested table are not part of the value
    } else if (!first && OB_FAIL(buf_.append(","))) {
      LOG_WARN("append separator failed", K(ret));
    } else if (FALSE_IT(first = false)) {
    } else if (OB_FAIL(write_value(*elem, depth + 1))) {
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(buf_.append("]"))) {
    LOG_WARN("append array end failed", K(ret));
  }
  return ret;
}

int ObPLCompositeJsonWriter::write_composite(const ObObj &obj, const int64_t depth)
{
  int ret = OB_SUCCESS;
  const pl::ObPLComposite *composite = reinterpret_cast<const pl::ObPLComposite *>(obj.get_ext());
  if (OB_ISNULL(composite) || composite->is_null()) {
    ret = buf_.append("null");
  } else if (composite->is_record()) {
    ret = write_record(*static_cast<const pl::ObPLRecord *>(composite), depth);
  } else if (composite->is_nested_table() || composite->is_varray()) {
    const pl::ObPLCollection *coll = static_cast<const pl::ObPLCollection *>(composite);
    ret = coll->is_collection_null() ? buf_.append("null") : write_collection(*coll, depth);
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "associative array or other PL type in a client result");
    LOG_WARN("composite kind has no JSON mapping", K(ret), K(*composite));
  }
  return ret;
}

int ObPLCompositeJsonWriter::write_value(const ObObj &obj, const int64_t depth)
{
  int ret = OB_SUCCESS;
  if (depth > MAX_DEPTH) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "PL composite nesting deeper than 64 levels in a client result");
  } else if (obj.is_null()) {
    ret = buf_.append("null");
  } else if (obj.is_ext()) {
    ret = write_composite(obj, depth);
  } else {
    ret = write_scalar(obj);
  }
  return ret;
}

} // anonymous namespace

int ObSqlUdtUtils::convert_pl_composite_for_client(ObObj &value, ObResultSet &result)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator *allocator = NULL;
  if (OB_FAIL(result.get_exec_context().get_convert_charset_allocator(allocator))) {
  } else if (OB_ISNULL(allocator)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(convert_pl_composite_for_client(value, *allocator,
                                                     result.get_exec_context()))) {
  }
  return ret;
}

int ObSqlUdtUtils::convert_pl_composite_for_client(ObObj &value,
                                                   ObIAllocator &allocator,
                                                   ObExecContext &exec_ctx)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = exec_ctx.get_my_session();
  ObSqlCtx *sql_ctx = exec_ctx.get_sql_ctx();
  share::schema::ObSchemaGetterGuard *schema_guard = NULL;
  common::ObMySQLProxy *sql_proxy = exec_ctx.get_sql_proxy();
  const pl::ObPLComposite *composite = NULL;
  if (!value.is_ext()) {
    // not a PL composite, nothing to do
  } else if (OB_ISNULL(session) || OB_ISNULL(sql_ctx)
             || OB_ISNULL(schema_guard = sql_ctx->schema_guard_) || OB_ISNULL(sql_proxy)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid context for composite encoding", K(ret), KP(session), KP(sql_ctx),
             KP(schema_guard), KP(sql_proxy));
  } else if (OB_ISNULL(composite = reinterpret_cast<const pl::ObPLComposite *>(value.get_ext()))
             || composite->is_null()
             || (composite->is_collection()
                 && static_cast<const pl::ObPLCollection *>(composite)->is_collection_null())) {
    // a NULL composite column is SQL NULL
    value.set_null();
  } else {
    pl::ObPLPackageGuard package_guard{};
    if (OB_FAIL(package_guard.init())) {
      LOG_WARN("init package guard failed", K(ret));
    } else {
      pl::ObPLResolveCtx resolve_ctx(allocator, *session, *schema_guard, package_guard,
                                     *sql_proxy, false);
      resolve_ctx.params_.plan_cache_ = exec_ctx.get_plan_cache();
      resolve_ctx.params_.pl_sql_runtime_ = exec_ctx.get_pl_sql_runtime();
      resolve_ctx.params_.pl_engine_ = exec_ctx.get_pl_engine();
      resolve_ctx.params_.srs_provider_ = exec_ctx.get_srs_provider();
      resolve_ctx.params_.lob_read_service_ = exec_ctx.get_lob_read_service();
      const ObDataTypeCastParams dtc_params = ObBasicSessionInfo::create_dtc_params(session);
      ObStringBuffer buf(&allocator);
      ObPLCompositeJsonWriter writer(resolve_ctx, dtc_params, allocator, buf);
      ObString json;
      ObString json_copy;
      if (OB_FAIL(writer.write_value(value, 0))) {
        LOG_WARN("encode composite as json failed", K(ret));
      } else if (OB_FAIL(buf.get_result_string(json))) {
        LOG_WARN("get json result failed", K(ret));
      } else if (OB_FAIL(ob_write_string(allocator, json, json_copy))) {
        LOG_WARN("copy json result failed", K(ret));
      } else {
        value.set_varchar(json_copy);
        value.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
        value.set_collation_level(CS_LEVEL_IMPLICIT);
      }
    }
  }
  return ret;
}

int ObSqlUdtUtils::convert_result_for_client(ObObj &value, ObResultSet &result)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator *allocator = NULL;
  if (OB_FAIL(result.get_exec_context().get_convert_charset_allocator(allocator))) {
  } else if (OB_ISNULL(allocator)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(convert_result_for_client(value,
                                               allocator,
                                               &result.get_session(),
                                               &result.get_exec_context(),
                                               result.is_ps_protocol()))) {
  }
  return ret;
}

int ObSqlUdtUtils::convert_result_for_client(ObObj &value,
                                             ObIAllocator *allocator,
                                             ObSQLSessionInfo *session_info,
                                             ObExecContext *exec_context,
                                             bool is_ps_protocol,
                                             const ColumnsFieldIArray *fields,
                                             share::schema::ObSchemaGetterGuard *schema_guard)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(session_info) || OB_ISNULL(allocator)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (!value.is_collection_sql_type() && !value.is_geometry()) {
    ret = OB_NOT_SUPPORTED;
  } else if (value.is_geometry()) {
    // MySQL mode: no-op.
  } else {
    if (OB_ISNULL(exec_context)) {
      ret = OB_BAD_NULL_ERROR;
    } else if (OB_ISNULL(exec_context->get_physical_plan_ctx())
               || !exec_context->get_physical_plan_ctx()->is_subschema_ctx_inited()) {
      if (OB_ISNULL(fields)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_ISNULL(exec_context->get_physical_plan_ctx())
                 && OB_FAIL(exec_context->create_physical_plan_ctx())) {
      } else if (OB_FAIL(exec_context->get_physical_plan_ctx()->build_subschema_by_fields(fields, schema_guard))) {
      }
    }
    if (OB_SUCC(ret)) {
      const uint16_t subschema_id = value.get_meta().get_subschema_id();
      ObSubSchemaValue sub_meta;
      if (OB_NOT_NULL(exec_context->get_physical_plan_ctx())
          && OB_FAIL(exec_context->get_sqludt_meta_by_subschema_id(subschema_id, sub_meta))) {
      }
      if (OB_FAIL(ret)) {
      } else if (sub_meta.type_ == ObSubSchemaType::OB_SUBSCHEMA_COLLECTION_TYPE) {
        ObSqlCollectionInfo *coll_meta = reinterpret_cast<ObSqlCollectionInfo *>(sub_meta.value_);
        ObString res_str;
        if (OB_FAIL(convert_collection_to_string(
                *exec_context, value, *coll_meta, allocator, res_str))) {
        } else {
          value.set_udt_value(res_str.ptr(), res_str.length());
        }
      } else {
        ObSqlUDTMeta udt_meta = *(reinterpret_cast<ObSqlUDTMeta *>(sub_meta.value_));
        if (!ObObjUDTUtil::ob_is_supported_sql_udt(udt_meta.udt_id_)) {
          ret = OB_NOT_SUPPORTED;
        } else if (!is_ps_protocol) {
          ObSqlUDT sql_udt;
          sql_udt.set_udt_meta(udt_meta);
          ObString res_str;
          if (OB_FAIL(convert_sql_udt_to_string(value, allocator, exec_context, sql_udt, res_str))) {
          } else {
            value.set_udt_value(res_str.ptr(), res_str.length());
          }
        } else {
          ObString udt_data = value.get_string();
          ObObj result;
          if (OB_FAIL(cast_sql_record_to_pl_record(exec_context, result, udt_data, udt_meta))) {
          } else {
            value = result;
          }
        }
      }
    }
  }
  return ret;
}

bool ObSqlUdtNullBitMap::is_valid()
{
  return (OB_NOT_NULL(bitmap_) && bitmap_len_ > 0);
}

int ObSqlUdtNullBitMap::check_bitmap_pos(uint32_t pos, bool &is_set)
{
  int ret = OB_SUCCESS;
  is_set = false;
  uint32 index = pos / 8;
  uint32 byte_index = pos % 8;
  if (index >= bitmap_len_) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    is_set = bitmap_[index] & (1 << byte_index);
  }
  return ret;
}


int ObSqlUdtNullBitMap::set_bitmap_pos(uint32_t pos)
{
  int ret = OB_SUCCESS;
  uint32 index = pos / 8;
  uint32 byte_index = pos % 8;
  if (index >= bitmap_len_) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    bitmap_[index] |= (1 << byte_index);
  }
  return ret;
}

int ObSqlUdtNullBitMap::set_current_bitmap_pos()
{
  return set_bitmap_pos(pos_);
}

int ObSqlUdtNullBitMap::reset_bitmap_pos(uint32_t pos)
{
  int ret = OB_SUCCESS;
  uint32 index = pos / 8;
  uint32 byte_index = pos % 8;
  if (index >= bitmap_len_) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    bitmap_[index] &= ~(1 << byte_index);
  }
  return ret;
}


int ObSqlUdtNullBitMap::assign(ObSqlUdtNullBitMap &src, uint32_t pos, uint32_t bit_len)
{
  int ret = OB_SUCCESS;
  uint32_t src_pos = src.get_pos();
  for (int i = 0; i < bit_len && OB_SUCC(ret); i++) {
    bool is_set = false;
    if (OB_FAIL(src.check_bitmap_pos(pos + i, is_set))) {
    } else if (is_set && OB_FAIL(set_current_bitmap_pos())) {
    } else {
      get_pos()++;
    }
  }
  src.set_bitmap_pos(src_pos);
  return ret;
}



int ObSqlUdtUtils::ob_udt_flattern_pl_extend(const ObObj **flattern_objs,
                                             const int32_t &flattern_object_count,
                                             int32_t &pos,
                                             ObSqlUdtNullBitMap &nested_udt_bitmap,
                                             const ObObj *obj,
                                             ObExecContext &exec_context,
                                             ObSqlUDT &sql_udt)
{
  int ret = OB_SUCCESS;
  ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObSqlUdtUtils::ob_udt_reordering_leaf_objects(const ObObj **flattern_objs,
                                                  const ObObj **sorted_objs,
                                                  const int32_t &flattern_object_count,
                                                  ObSqlUDT &sql_udt)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; i < flattern_object_count; i++) {
    int64_t index = sql_udt.get_udt_meta().leaf_attrs_meta_[i].order_;
    sorted_objs[i] = flattern_objs[index];
  }
  return ret;
}

int ObSqlUdtUtils::ob_udt_calc_sql_varray_length(const ObObj *cur_obj,
                                                 int64_t &sql_udt_total_len,
                                                 bool with_lob_header)
{
  int ret = OB_SUCCESS;
  ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObSqlUdtUtils::ob_udt_calc_total_len(const ObObj **sorted_objs,
                                         const int32_t &flattern_object_count,
                                         int64_t &sql_udt_total_len,
                                         ObSqlUDT &sql_udt)
{
  int ret = OB_SUCCESS;
  ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObSqlUdtUtils::ob_udt_convert_pl_varray_to_sql_varray(const ObObj *cur_obj,
                                                          char *buf,
                                                          const int64_t buf_len,
                                                          int64_t &pos,
                                                          bool with_lob_header)
{
  int ret = OB_SUCCESS;
  ret = OB_NOT_SUPPORTED;
  return ret;
}

// convert pl extend to sql udt by type
int ObSqlUdtUtils::ob_udt_convert_sorted_objs_array_to_udf_format(const ObObj **sorted_objs,
                                                                  char *buf,
                                                                  const int64_t buf_len,
                                                                  int64_t &pos,
                                                                  ObSqlUDT &sql_udt)
{
  int ret = OB_SUCCESS;
  ret = OB_NOT_SUPPORTED;
  return ret;
}

// convert sql_udt to string by type



int ObSqlUdtUtils::convert_sql_udt_to_string(ObObj &sql_udt_obj,
                                             common::ObIAllocator *allocator,
                                             sql::ObExecContext *exec_context,
                                             ObSqlUDT &sql_udt,
                                             ObString &res_str)
{
  int ret = OB_SUCCESS;
  ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObSqlUdtUtils::convert_collection_to_string(ObExecContext &exec_ctx,
                                                ObObj &coll_obj,
                                                const ObSqlCollectionInfo &coll_meta,
                                                common::ObIAllocator *allocator,
                                                ObString &res_str)
{
  int ret = OB_SUCCESS;
  ObIArrayType *arr_obj = NULL;
  ObString coll_data = coll_obj.get_string();
  ObStringBuffer buf(allocator);
  ObArenaAllocator lob_allocator(ObModIds::OB_LOB_ACCESS_BUFFER, OB_MALLOC_NORMAL_BLOCK_SIZE);
  ObCollectionArrayType *arr_type = static_cast<ObCollectionArrayType *>(coll_meta.collection_meta_);
  if (OB_FAIL(sql::ObTextStringHelper::read_real_string_data(exec_ctx, &lob_allocator,
                                                        ObLongTextType,
                                                        CS_TYPE_BINARY,
                                                        true, coll_data))) {
  } else if (OB_FAIL(ObArrayTypeObjFactory::construct(*allocator, *arr_type, arr_obj, true))) {
  } else {
    if (OB_FAIL(arr_obj->init(coll_data))) {
    } else if (OB_FAIL(arr_obj->print(buf))) {
    } else {
      res_str.assign_ptr(buf.ptr(), buf.length());
    }
  }

  return ret;
}






int ObSqlUdtUtils::cast_sql_record_to_pl_record(sql::ObExecContext *exec_ctx,
                                                ObObj &result,
                                                ObString &udt_data,
                                                ObSqlUDTMeta &udt_meta)
{
  int ret = OB_SUCCESS;
  ret = OB_NOT_SUPPORTED;
  return ret;
}


int ObSqlUdtMetaUtils::generate_udt_meta_from_schema(ObSchemaGetterGuard *schema_guard,
                                                     ObSubSchemaCtx *subschema_ctx,
                                                     common::ObIAllocator &allocator,
                                                     uint64_t udt_id,
                                                     ObSqlUDTMeta &udt_meta)
{
  int ret = OB_SUCCESS;
  ret = OB_NOT_SUPPORTED;
  return ret;
}

}
}
