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

#define USING_LOG_PREFIX SERVER

#include "ob_query_driver.h"
#include "ob_mysql_result_set.h"
#include "query/protocol/ob_mysql_packet_sender.h"
#include "sql/engine/ob_physical_plan.h"
#include "obsm_row.h"
#include "rpc/obmysql/packet/ompk_eof.h"
#include "rpc/obmysql/packet/ompk_row.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/engine/expr/ob_expr_sql_udt_utils.h"
#include "sql/engine/expr/ob_lob_result_materializer.h"
#include "sql/monitor/show_trace/ob_show_trace.h"
#include "share/datum/datum_value_cast.h"

namespace oceanbase
{
using namespace common;
using namespace sql;
using namespace obmysql;
namespace observer
{

int ObQueryDriver::response_query_header(ObResultSet &result,
                                         bool has_more_result,
                                         bool need_set_ps_out_flag) {
  int ret = OB_SUCCESS;
  if (NULL == result.get_field_columns()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(response_query_header(*result.get_field_columns(),
                                           has_more_result,
                                           need_set_ps_out_flag,
                                           &result))) {
  }
  if (OB_FAIL(ret)) {
    result.set_errcode(ret);
  }
  return ret;
}

int ObQueryDriver::response_query_header(const ColumnsFieldIArray &fields,
                                         bool has_more_result,
                                         bool need_set_ps_out_flag,
                                         ObResultSet *result)
{
  int ret = OB_SUCCESS;
  bool ac = true;
  const bool is_field_list = NULL != result && result->get_is_com_filed_list();
  ObSEArray<ObMySQLField, 16> mysql_fields;
  // result == null means ps cursor in execute or fetch .
  if (NULL != result && (&fields != result->get_field_columns())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (fields.count() <= 0) {
    ret = OB_ERR_BAD_FIELD_ERROR;
  } else if (OB_FAIL(session_.get_autocommit(ac))) {
  } else if (OB_FAIL(mysql_fields.reserve(fields.count()))) {
  }

  // Complete wildcard filtering and every ObField conversion before entering
  // Rust. A conversion failure must not leave a result header or field prefix
  // in the response batch.
  if (OB_SUCC(ret)) {
    for (int64_t i = 0; OB_SUCC(ret) && i < fields.count(); ++i) {
      bool is_not_match = false;
      ObMySQLField field;
      const ObField &ob_field = fields.at(i);
      if (is_field_list &&
          OB_FAIL(is_com_filed_list_match_wildcard_str(
              *result, static_cast<ObCollationType>(ob_field.charsetnr_),
              ob_field.org_cname_, is_not_match))) {
      } else if (is_not_match) {
        /*do nothing*/
      } else {
        if (OB_FAIL(ObMySQLResultSet::to_mysql_field(ob_field, field, is_field_list))) {
        } else {
        }
        if (OB_SUCC(ret)) {
          ObMySQLResultSet::replace_lob_type(field);
          if (OB_FAIL(mysql_fields.push_back(field))) {
          }
        }
      }
    }
  }

  if (OB_SUCC(ret)) {
    OMPKEOF eofp;
    eofp.set_warning_count(0);
    ObServerStatusFlags flags = eofp.get_server_status();
    flags.status_flags_.OB_SERVER_STATUS_IN_TRANS
      = (session_.is_server_status_in_transaction() ? 1 : 0);
    flags.status_flags_.OB_SERVER_STATUS_AUTOCOMMIT = (ac ? 1 : 0);
    flags.status_flags_.OB_SERVER_MORE_RESULTS_EXISTS = has_more_result;
    flags.status_flags_.OB_SERVER_PS_OUT_PARAMS = need_set_ps_out_flag ? 1 : 0;
    // NULL == result indicates it is an old protocol ps cursor execute response, or fetch protocol response, cursor_exit = true
    flags.status_flags_.OB_SERVER_STATUS_CURSOR_EXISTS = NULL == result ? 1 : 0; 
    eofp.set_server_status(flags);

    if (OB_FAIL(sender_.response_resultset_metadata(
            mysql_fields, !is_field_list, eofp.get_field_count(),
            eofp.get_warning_count(), eofp.get_server_status().flags_))) {
    }
  }
  return ret;
}

int ObQueryDriver::response_query_result(ObResultSet &result,
                                         bool is_ps_protocol,
                                         bool has_more_result,
                                         bool &can_retry,
                                         int64_t fetch_limit)
{
  int ret = OB_SUCCESS;
  ObTraceSpanGuard response_span(&session_, TRACE_RESPONSE_RESULT);
  can_retry = true;
  bool is_first_row = true;
  const ObNewRow *result_row = NULL;
  ResultRowView result_view;
  ObSEArray<int64_t, 16> obj_indexes;
  ObSEArray<DatumValue, 4> fallback_cells;
  bool is_cac_found_rows =  result.is_calc_found_rows();
  int64_t row_num = 0;
  ObSqlCtx *sql_ctx = result.get_exec_context().get_sql_ctx();
  bool is_packed = result.get_physical_plan() ? result.get_physical_plan()->is_packed() : false;
  MYSQL_PROTOCOL_TYPE protocol_type = is_ps_protocol ? MYSQL_PROTOCOL_TYPE::BINARY : MYSQL_PROTOCOL_TYPE::TEXT;

  int64_t limit_count = INT64_MAX;
  if (OB_FAIL(ret)) {
  } else if (OB_INVALID_COUNT != fetch_limit) {
    limit_count = fetch_limit;
  } else {
    if (!result.get_has_top_limit() && OB_FAIL(session_.get_sql_select_limit(limit_count))) {
    }
  }

  const common::ColumnsFieldIArray *fields = NULL;
  if (OB_SUCC(ret)) {
    fields = result.get_field_columns();
    if (OB_ISNULL(fields)) {
      ret = OB_INVALID_ARGUMENT;
    }
  }
  
  ObCharsetType charset_type = CHARSET_INVALID;
  
  if (OB_SUCC(ret)) {
    const ObSQLSessionInfo &my_session = result.get_session();
    if (OB_FAIL(my_session.get_character_set_results(charset_type))) {
    }
  }

  const ObDataTypeCastParams dtc_params =
      ObBasicSessionInfo::create_dtc_params(&session_);
  while (OB_SUCC(ret) && row_num < limit_count && !OB_FAIL(result.get_next_result_row(result_view)) ) {
    ObNewRow *row = const_cast<ObNewRow*>(result_view.legacy_row());
    if (result_view.is_datum_row()) {
      if (OB_UNLIKELY(result_view.count() != fields->count())) {
        ret = OB_ERR_UNEXPECTED;
      } else if (is_first_row) {
        int64_t obj_count = 0;
        if (OB_FAIL(obj_indexes.prepare_allocate(result_view.count()))) {
        } else {
          for (int64_t i = 0; i < result_view.count(); ++i) {
            bool convert_charset = false;
            // Negative entries select raw Datum bytes or charset conversion;
            // nonnegative entries address converted native value views.
            obj_indexes.at(i) = DatumMysqlRow::can_encode(
                result_view.expr(i), fields->at(i), charset_type, result.is_ps_protocol(), convert_charset)
                                    ? (convert_charset ? -2 : -1) : obj_count++;
          }
          ret = fallback_cells.prepare_allocate(obj_count);
        }
      }
      // Finish all compatibility materialization before sending the header,
      // retaining the old retry boundary for conversion errors.
      for (int64_t i = 0; OB_SUCC(ret) && i < result_view.count(); ++i) {
        if (obj_indexes.at(i) >= 0) {
          ret = result_view.get_value(i, fallback_cells.at(obj_indexes.at(i)));
        }
      }
      if (OB_FAIL(ret)) {
        break;
      }
    }
    // If it is the first line, then reply to the client with field information etc.
    if (is_first_row) {
      is_first_row = false;
      can_retry = false; // Already obtained the first row of data, no longer retrying
      if (OB_FAIL(response_query_header(result, has_more_result, false))) {
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < result_view.count(); i++) {
      if (result_view.is_datum_row() && obj_indexes.at(i) < 0) {
        continue;
      }
      DatumValue& value = result_view.is_datum_row()
                         ? fallback_cells.at(obj_indexes.at(i)) : row->get_cell(i);
      if (result.is_ps_protocol() && !is_packed) {
        if (value.get_type() != fields->at(i).type_meta_.get_type()) {
          ObCastCtx cast_ctx(&result.get_mem_pool(), NULL, CM_WARN_ON_FAIL, 
            fields->at(i).type_meta_.get_collation_type());
          if (OB_FAIL(common::DatumValueCaster::to_type(fields->at(i).type_meta_.get_type(),
                                           cast_ctx, value, result.get_mem_pool(), value))) {
          }
        }
      }
      if (OB_SUCC(ret) && !is_packed) {
        // Compatibility with server versions before 4.1.
        //    use only locator and response routine
        // >= 4.1 with full lob locator v2
        //    1. use extern header if client supports locator
        //    2. remove locator if client does not support locator
        // >= 4.1 for mysql mode
        //    remove locator
        if (ob_is_string_tc(value.get_type())
            && CS_TYPE_INVALID != value.get_collation_type()) {
          OZ(convert_string_value_charset(value, result, charset_type));
        } else if (ob_is_text_tc(value.get_type())
                    && OB_FAIL(convert_text_value_charset(value, result, charset_type))) {
        }
        if (OB_FAIL(ret)){
        } else if ((value.get_meta().is_lob() || value.is_json() || value.is_geometry())
                  && OB_FAIL(process_lob_locator_results(value, result))) {
        } else if ((value.is_collection_sql_type() || value.is_geometry()) &&
                   OB_FAIL(ObSqlUdtUtils::convert_result_for_client(value, result))) {
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (result_view.is_datum_row()) {
        DatumMysqlRow sm(protocol_type, result_view, obj_indexes, fallback_cells,
                        dtc_params, session_, *fields, charset_type, ctx_.schema_guard_);
        OMPKRow rp(sm);
        ret = sender_.response_packet(rp);
      } else {
        ObSMRow sm(protocol_type, *row, dtc_params, session_,
                   result.get_field_columns(), ctx_.schema_guard_);
        sm.set_packed(is_packed);
        OMPKRow rp(sm);
        ret = sender_.response_packet(rp);
      }
      if (OB_SUCC(ret)) {
        ++row_num;
        if (0 == row_num % RESET_CONVERT_CHARSET_ALLOCATOR_EVERY_X_ROWS) {
          (void) result.get_exec_context().try_reset_convert_charset_allocator();
        }
      }
    }
  }
  if (is_cac_found_rows) {
    while (OB_SUCC(ret) && !OB_FAIL(result.get_next_row(result_row))) {
      // nothing
    }
  }
  if (OB_ITER_END == ret) {
    ret = OB_SUCCESS;
  } else {
  }
  if (OB_SUCC(ret) && 0 == row_num) {
    // If there is no data at all, we still need to reply to the client with field information, and no more retries will be attempted
    can_retry = false;
    if (OB_FAIL(response_query_header(result, has_more_result, false))) {
    }
  }
  if (OB_FAIL(ret) && !can_retry) {
    FLOG_INFO("The query has already returned partial results to the client and cannot be retried", KR(ret));
  }

  return ret;
}

int ObQueryDriver::convert_field_charset(ObIAllocator& allocator,
                                         const ObCollationType& from_collation,
                                         const ObCollationType& dest_collation,
                                         const ObString &from_string,
                                         ObString &dest_string)
{
  int ret = OB_SUCCESS;
  char *buf = NULL;
  int32_t buf_len = from_string.length() * ObCharset::CharConvertFactorNum;
  uint32_t result_len = 0;
  if (0 == buf_len) {
  } else if (OB_UNLIKELY(NULL == (buf = static_cast<char*>(
            allocator.alloc(buf_len))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("alloc memory failed", K(ret));
  } else if (OB_FAIL(ObCharset::charset_convert(
          static_cast<ObCollationType>(from_collation),
          from_string.ptr(),
          from_string.length(),
          dest_collation,
          buf,
          buf_len,
          result_len))) {
  } else {
    dest_string.assign(buf, static_cast<int32_t>(result_len));
  }
  return ret;
}

namespace {
void set_native_lob_view(DatumValue &value, SqlValueType type, const ObString &bytes)
{
  value.meta_.set_type(type);
  value.meta_.set_inrow();
  if (type != ObCollectionSQLType) { value.meta_.set_collation_level(CS_LEVEL_IMPLICIT); }
  value.datum_.set_string(bytes);
}
}

int ObQueryDriver::convert_string_value_charset(DatumValue &value, ObResultSet &result,
                                                ObCharsetType charset_type)
{
  int ret = OB_SUCCESS;
  ObCollationType collation = value.get_collation_type();
  const ObCollationType target = ObCharset::get_default_collation(charset_type);
  ObArenaAllocator *owner = nullptr;
  if (collation == target) {
    if (ObCharset::get_charset(collation) == nullptr || collation == CS_TYPE_INVALID) {
      ret = OB_ERR_UNEXPECTED;
    }
  } else if (OB_FAIL(result.get_exec_context().get_convert_charset_allocator(owner))) {
  } else if (owner == nullptr) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObString bytes;
    if (OB_FAIL(value.get_string(bytes))) {
    } else if (OB_FAIL(ObCharsetUtils::convert_string_value_charset(bytes, collation,
                                                                  charset_type, *owner))) {
    } else if (collation != value.get_collation_type()) {
      value.datum_.set_string(bytes);
      value.meta_.set_collation_level(CS_LEVEL_IMPLICIT);
      value.set_collation_type(collation);
    }
  }
  return ret;
}

int ObQueryDriver::convert_text_value_charset(DatumValue &value, ObResultSet &result,
                                              ObCharsetType charset_type)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator *owner = nullptr;
  if (OB_FAIL(result.get_exec_context().get_convert_charset_allocator(owner))) {
  } else if (owner == nullptr) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ret = convert_text_value_charset(value, charset_type, *owner,
                                     &result.get_session(), &result.get_exec_context());
  }
  return ret;
}

int ObQueryDriver::convert_text_value_charset(DatumValue &value, ObCharsetType charset_type,
    ObIAllocator &owner, const ObSQLSessionInfo *session, ObExecContext *exec_ctx)
{
  int ret = OB_SUCCESS;
  UNUSED(session);
  if (value.is_null() || value.is_nop_value()) {
  } else {
    const ObString raw = value.get_string();
    if (raw.ptr() == nullptr || raw.empty()) {
      if (value.has_lob_header() && value.is_lob_storage()) { ret = OB_ERR_UNEXPECTED; }
    } else if (ObCharset::is_valid_charset(charset_type) && charset_type != CHARSET_BINARY) {
      const ObCollationType from = value.get_collation_type();
      const ObCollationType to = ObCharset::get_default_collation(charset_type);
      const ObCharsetInfo *from_info = ObCharset::get_charset(from);
      const ObCharsetInfo *to_info = ObCharset::get_charset(to);
      if (from_info == nullptr || to_info == nullptr || from == CS_TYPE_INVALID || to == CS_TYPE_INVALID) {
        ret = OB_ERR_UNEXPECTED;
      } else if (from != CS_TYPE_BINARY && to != CS_TYPE_BINARY && strcmp(from_info->csname, to_info->csname) != 0) {
        ObString bytes = raw;
        int64_t data_bytes = raw.length();
        ObArenaAllocator temporary("LobRead", OB_MALLOC_NORMAL_BLOCK_SIZE);
        if (value.has_lob_header()) {
          ObLobLocatorV2 locator(raw, true);
          ObTextStringIter input(value.get_type(), from, raw, true);
          if (exec_ctx == nullptr) {
            ret = OB_INVALID_ARGUMENT;
          } else if (OB_FAIL(ObTextStringHelper::build_text_iter(input, *exec_ctx,
                                                        &temporary, &temporary))) {
          } else if (OB_FAIL(input.get_full_data(bytes))) {
          } else if (OB_FAIL(locator.get_lob_data_byte_len(data_bytes))) {
          }
        }
        if (OB_SUCC(ret)) {
          ObTextStringResult output(value.get_type(), value.has_lob_header(), &owner);
          char *buffer = nullptr;
          int64_t capacity = 0;
          uint32_t count = 0;
          if (OB_FAIL(output.init(data_bytes * ObCharset::CharConvertFactorNum))) {
          } else if (OB_FAIL(output.get_reserved_buffer(buffer, capacity))) {
          } else if (OB_FAIL(convert_string_charset(bytes, from, to, buffer, capacity, count))) {
          } else if (OB_FAIL(output.lseek(count, 0))) {
          } else {
            ObString converted;
            output.get_result_buffer(converted);
            set_native_lob_view(value, value.get_type(), converted);
            value.set_collation_type(to);
            if (output.has_lob_header()) { value.meta_.set_has_lob_header(); }
          }
        }
      }
    }
  }
  return ret;
}

int ObQueryDriver::process_lob_locator_results(DatumValue &value, ObResultSet &result)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator *owner = nullptr;
  if (OB_FAIL(result.get_exec_context().get_convert_charset_allocator(owner))) {
  } else if (owner == nullptr) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ret = process_lob_locator_results(value, owner, &result.get_session(), &result.get_exec_context());
  }
  return ret;
}

int ObQueryDriver::process_lob_locator_results(DatumValue &value, ObIAllocator *owner,
    const ObSQLSessionInfo *session, ObExecContext *exec_ctx)
{
  int ret = OB_SUCCESS;
  UNUSED(session);
  if (!(value.get_meta().is_lob() || value.is_json() || value.is_geometry())
             || value.is_null() || value.is_nop_value()) {
  } else if (exec_ctx == nullptr) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObLobLocatorV2 locator(value.get_string(), value.has_lob_header());
    if (!locator.is_null()) {
      ObString bytes;
      ObArenaAllocator temporary("LobRead", OB_MALLOC_NORMAL_BLOCK_SIZE);
      ObTextStringIter input(value.get_type(), value.get_collation_type(),
                             value.get_string(), value.has_lob_header());
      if (OB_FAIL(ObTextStringHelper::build_text_iter(input, *exec_ctx, owner, &temporary))) {
      } else if (OB_FAIL(input.get_full_data(bytes))) {
      } else {
        const SqlValueType type = value.is_json() ? ObJsonType
            : value.is_geometry() ? ObGeometryType : ObLongTextType;
        set_native_lob_view(value, type, bytes);
      }
    }
  }
  return ret;
}

int ObQueryDriver::like_match(const char* str, int64_t length_str, int64_t i,
                              const char* pattern, int64_t length_pat, int64_t j,
                              bool &is_match)
{
  int ret = OB_SUCCESS;
  bool is_stack_overflow = false;
  if (OB_ISNULL(str) || OB_ISNULL(pattern) ||
      OB_UNLIKELY(length_str < 0 || i < 0 || length_pat < 0 || j < 0)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(check_stack_overflow(is_stack_overflow))) {
  } else if (is_stack_overflow) {
    ret = OB_SIZE_OVERFLOW;
  } else if (i == length_str && j == length_pat) {
    is_match = true;
  } else if (i != length_str && j >= length_pat) {
    is_match = false;
  } else if (j < length_pat && pattern[j] == '%') {
    ++j;
    if (OB_FAIL(like_match(str, length_str, i,
                           pattern, length_pat, j,
                           is_match))) {
    } else if (!is_match && i < length_str) {
      ++i;
      --j;
      if (OB_FAIL(like_match(str, length_str, i,
                             pattern, length_pat, j,
                             is_match))) {
      }
    }
  } else if (i < length_str && j < length_pat && pattern[j] == '_') {
    ++i;
    ++j;
    if (OB_FAIL(like_match(str, length_str, i,
                           pattern, length_pat, j,
                           is_match))) {
    }
  } else if (i < length_str && j < length_pat && tolower(str[i]) == tolower(pattern[j])) {
    ++i;
    ++j;
    if (OB_FAIL(like_match(str, length_str, i,
                           pattern, length_pat, j,
                           is_match))) {
    }
  } else {
    is_match = false;
  }
  return ret;
}


int ObQueryDriver::convert_string_charset(const ObString &in_str, const ObCollationType in_cs_type,
                                          const ObCollationType out_cs_type, 
                                          char *buf, int32_t buf_len, uint32_t &result_len)
{
  int ret = OB_SUCCESS;
  ret = ObCharset::charset_convert(in_cs_type, in_str.ptr(),
        in_str.length(),out_cs_type, buf, buf_len, result_len);
  if (OB_SUCCESS != ret) {
    int32_t str_offset = 0;
    int64_t buf_offset = 0;
    ObString question_mark = ObCharsetUtils::get_const_str(out_cs_type, '?');
    while (str_offset < in_str.length() && buf_offset + question_mark.length() <= buf_len) {
      int64_t offset = ObCharset::charpos(in_cs_type, in_str.ptr() + str_offset,
                                          in_str.length() - str_offset, 1);
      ret = ObCharset::charset_convert(in_cs_type, in_str.ptr() + str_offset, offset, out_cs_type,
                                       buf + buf_offset, buf_len - buf_offset, result_len);
      str_offset += offset;
      if (OB_SUCCESS == ret) {
        buf_offset += result_len;
      } else {
        MEMCPY(buf + buf_offset, question_mark.ptr(), question_mark.length());
        buf_offset += question_mark.length();
      }
    }
    if (str_offset < in_str.length()) {
      ret = OB_SIZE_OVERFLOW;
    } else {
      result_len = buf_offset;
      ret = OB_SUCCESS;
    }
  }
  return ret;
}

/*@brief: is_com_filed_list_match_wildcard_str is used to match the parameters in the COM_FIELD_LIST sent by the client that contain wildcard characters
* Scenario, e.g.: COM_FIELD_LIST(t1, c*) , t1 has columns c1, c2, pk ==> only return c1, c2, do not return pk, because it does not match c*;
* The rule is similar to the like scenario; for details, refer to the link:
* 
*/
int ObQueryDriver::is_com_filed_list_match_wildcard_str(ObResultSet &result,
                                                        const ObCollationType &from_collation,
                                                        const ObString &from_string,
                                                        bool &is_not_match)
{
  int ret = OB_SUCCESS;
  is_not_match = false;
  if (!result.get_is_com_filed_list() || result.get_wildcard_string().empty()) {
    /*do nothing*/
  } else {
    /*Need to consider converting to the same character set for comparison when comparing between different character sets*/
    ObIAllocator &allocator = result.get_mem_pool();
    ObString wildcard_str;
    if (result.get_session().get_nls_collation() != from_collation) {
      if (OB_FAIL(convert_field_charset(allocator,
                                        result.get_session().get_nls_collation(),
                                        from_collation,
                                        result.get_wildcard_string(),
                                        wildcard_str))) {
      }
    } else {
      wildcard_str = result.get_wildcard_string();
    }
    if (OB_SUCC(ret)) {
      bool is_match = false;
      if (OB_FAIL(like_match(from_string.ptr(),
                             from_string.length(),
                             0,
                             wildcard_str.ptr(),
                             wildcard_str.length(),
                             0,
                             is_match))) {
      } else if (!is_match) {
        is_not_match = true;
      }
    }
  }
  return ret;
}

}
}
