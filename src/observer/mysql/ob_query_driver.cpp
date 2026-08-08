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
    LOG_WARN("response field is null. ", K(ret));
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
    LOG_WARN("filed is not from result in non ps cursor mode. ", K(ret));
  } else if (fields.count() <= 0) {
    LOG_WARN("column cnt is null ", K(fields.count()));
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
        LOG_WARN("failed to is com filed list match wildcard str", K(ret));
      } else if (is_not_match) {
        /*do nothing*/
      } else {
        if (OB_FAIL(ObMySQLResultSet::to_mysql_field(ob_field, field))) {
        } else {
        }
        if (OB_SUCC(ret)) {
          ObMySQLResultSet::replace_lob_type(field);
          if (is_field_list) {
            field.default_value_ =
                static_cast<EMySQLFieldType>(ob_field.default_value_.get_ext());
          }
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
      LOG_WARN("failed to get sytem variable sql_select_limit", K(ret));
    }
  }

  const common::ColumnsFieldIArray *fields = NULL;
  if (OB_SUCC(ret)) {
    fields = result.get_field_columns();
    if (OB_ISNULL(fields)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("fields is null", K(ret), KP(fields));
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
  while (OB_SUCC(ret) && row_num < limit_count && !OB_FAIL(result.get_next_row(result_row)) ) {
    ObNewRow *row = const_cast<ObNewRow*>(result_row);
    // If it is the first line, then reply to the client with field information etc.
    if (is_first_row) {
      is_first_row = false;
      can_retry = false; // Already obtained the first row of data, no longer retrying
      if (OB_FAIL(response_query_header(result, has_more_result, false))) {
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < row->get_count(); i++) {
      ObObj& value = row->get_cell(i);
      if (result.is_ps_protocol() && !is_packed) {
        if (value.get_type() != fields->at(i).type_.get_type()) {
          ObCastCtx cast_ctx(&result.get_mem_pool(), NULL, CM_WARN_ON_FAIL, 
            fields->at(i).type_.get_collation_type());
          if (OB_FAIL(common::ObObjCaster::to_type(fields->at(i).type_.get_type(),
                                           cast_ctx,
                                           value,
                                           value))) {
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
          LOG_WARN("convert text value charset failed", K(ret));
        }
        if (OB_FAIL(ret)){
        } else if ((value.is_lob() || value.is_json() || value.is_geometry())
                  && OB_FAIL(process_lob_locator_results(value, result))) {
          LOG_WARN("convert lob locator to longtext failed", K(ret));
        } else if ((value.is_collection_sql_type() || value.is_geometry()) &&
                   OB_FAIL(ObSqlUdtUtils::convert_result_for_client(value, result))) {
          LOG_WARN("convert udt to client format failed", K(ret), K(value.get_udt_subschema_id()));
        }
      }
    }
    if (OB_SUCC(ret)) {
      ObSMRow sm(protocol_type, *row, dtc_params,
                         session_,  
                         result.get_field_columns(),
                         ctx_.schema_guard_);
      sm.set_packed(is_packed);
      OMPKRow rp(sm);
      if (OB_FAIL(sender_.response_packet(rp))) {
      } else {
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
    LOG_WARN("fail to iterate and response", K(ret), K(row_num), K(can_retry));
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

int ObQueryDriver::convert_string_value_charset(ObObj& value, ObResultSet &result,
                                                ObCharsetType charset_type)
{
  int ret = OB_SUCCESS;
  ObCollationType to_collation_type = ObCharset::get_default_collation(charset_type);
  ObArenaAllocator *allocator = NULL;
  ObCollationType from_collation_type = value.get_collation_type();
  if (OB_FAIL(ret)) {
  } else if (from_collation_type == to_collation_type) {
    const ObCharsetInfo *charset_info = ObCharset::get_charset(from_collation_type);
    if (OB_ISNULL(charset_info)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("charsetinfo is null", K(ret), K(from_collation_type), K(to_collation_type), K(value));
    } else if (CS_TYPE_INVALID == from_collation_type) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid collation", K(ret), K(from_collation_type), K(to_collation_type), K(value));
    }
  } else if (OB_FAIL(result.get_exec_context().get_convert_charset_allocator(allocator))) {
  } else if (OB_ISNULL(allocator)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("lob fake allocator is null.", K(ret), K(value));
  } else {
    OZ (value.convert_string_value_charset(charset_type, *allocator));
  }
  return ret;
}

int ObQueryDriver::convert_text_value_charset(common::ObObj& value, sql::ObResultSet &result,
                                              ObCharsetType charset_type)
{
  int ret = OB_SUCCESS;

  const ObSQLSessionInfo &my_session = result.get_session();

  ObArenaAllocator *allocator = NULL;
  if (OB_FAIL(result.get_exec_context().get_convert_charset_allocator(allocator))) {
  } else if (OB_ISNULL(allocator)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("text fake allocator is null.", K(ret), K(value));
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(convert_text_value_charset(value, charset_type, *allocator, &my_session, &result.get_exec_context()))) {
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
    LOG_WARN("get unexpected error", K(ret), K(str), K(length_str), K(i),
                                     K(pattern), K(length_pat), K(j));
  } else if (OB_FAIL(check_stack_overflow(is_stack_overflow))) {
  } else if (is_stack_overflow) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("too deep recursive", K(ret));
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


int ObQueryDriver::process_lob_locator_results(ObObj& value, sql::ObResultSet &result)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator *allocator = NULL;
  if (OB_FAIL(result.get_exec_context().get_convert_charset_allocator(allocator))) {
  } else if (OB_ISNULL(allocator)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("lob fake allocator is null.", K(ret), K(value));
  } else if (OB_FAIL(process_lob_locator_results(value, 
                                                 allocator,
                                                 &result.get_session(),
                                                 &result.get_exec_context()))) {
  }
  return ret;
}

int ObQueryDriver::process_lob_locator_results(ObObj& value,
                                               ObIAllocator *allocator,
                                               const sql::ObSQLSessionInfo *session_info,
                                               sql::ObExecContext *exec_ctx)
{
  int ret = OB_SUCCESS;
  UNUSED(session_info);
  bool is_lob_type = value.is_lob()
                     || value.is_json() || value.is_geometry();
  if (!is_lob_type) {
    // not lob types, do nothing
  } else if (value.is_null() || value.is_nop_value()) {
    // do nothing
  } else if (OB_ISNULL(exec_ctx)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("exec context is null for LOB result", K(ret), K(value));
  } else {
    // Should remove locator header and read full lob data
    ObString data;
    ObLobLocatorV2 loc(value.get_string(), value.has_lob_header());
    if (loc.is_null()) { // maybe v1 empty lob
    } else { // lob locator v2
      ObArenaAllocator tmp_alloc("LobRead", OB_MALLOC_NORMAL_BLOCK_SIZE);
      ObTextStringIter instr_iter(value);
      if (OB_FAIL(ObTextStringHelper::build_text_iter(
              instr_iter, *exec_ctx, allocator, &tmp_alloc))) {
      } else if (OB_FAIL(instr_iter.get_full_data(data))) {
      } else {
        ObObjType dst_type = ObLongTextType;
        if (value.is_json()) {
          dst_type = ObJsonType;
        } else if (value.is_geometry()) {
          dst_type = ObGeometryType;
        }
        // remove has lob header flag
        value.set_lob_value(dst_type, data.ptr(), static_cast<int32_t>(data.length()));
      }
    }
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
      LOG_WARN("sizeoverflow", K(ret), K(in_str), KPHEX(in_str.ptr(), in_str.length()));
    } else {
      result_len = buf_offset;
      ret = OB_SUCCESS;
      LOG_WARN("charset convert failed", K(ret), K(in_cs_type), K(out_cs_type));
    }
  }
  return ret;
}

int ObQueryDriver::convert_text_value_charset(ObObj& value,
                                              ObCharsetType charset_type, 
                                              ObIAllocator &allocator,
                                              const sql::ObSQLSessionInfo *session,
                                              sql::ObExecContext *exec_ctx)
{
  int ret = OB_SUCCESS;
  UNUSED(session);
  ObString raw_str = value.get_string();
  if (value.is_null() || value.is_nop_value()) {
  } else if (OB_ISNULL(raw_str.ptr()) || raw_str.length() == 0) {
    if (!value.has_lob_header() || !value.is_lob_storage()) {
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Lob: get  empty or null lob obj with header", K(ret), K(value));
    }
  } else if (ObCharset::is_valid_charset(charset_type) && CHARSET_BINARY != charset_type) {
    ObCollationType to_collation_type = ObCharset::get_default_collation(charset_type);
    ObCollationType from_collation_type = value.get_collation_type();
    const ObCharsetInfo *from_charset_info = ObCharset::get_charset(from_collation_type);
    const ObCharsetInfo *to_charset_info = ObCharset::get_charset(to_collation_type);
    const ObObjType type = value.get_type();

    if (OB_ISNULL(from_charset_info) || OB_ISNULL(to_charset_info)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Lob: charsetinfo is null", K(ret), K(from_collation_type), K(to_collation_type));
    } else if (CS_TYPE_INVALID == from_collation_type || CS_TYPE_INVALID == to_collation_type) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Lob: invalid collation", K(from_collation_type), K(to_collation_type), K(ret));
    } else if (CS_TYPE_BINARY != from_collation_type && CS_TYPE_BINARY != to_collation_type
        && strcmp(from_charset_info->csname, to_charset_info->csname) != 0) {
      {
        // get full data, buffer size is full byte length * ObCharset::CharConvertFactorNum
        ObString data_str = value.get_string();
        int64_t lob_data_byte_len = data_str.length();
        ObArenaAllocator tmp_alloc("LobRead", OB_MALLOC_NORMAL_BLOCK_SIZE);
        if (!value.has_lob_header()) {
        } else {
          ObLobLocatorV2 loc(raw_str, value.has_lob_header());
          ObTextStringIter str_iter(value);
          // it's fine that res_allocator and tmp_allocator is same
          // because the final result will be allocated by allocator when convert charset
          if (OB_ISNULL(exec_ctx)) {
            ret = OB_INVALID_ARGUMENT;
            LOG_WARN("exec context is null for LOB charset conversion",
                     K(ret), K(value));
          } else if (OB_FAIL(ObTextStringHelper::build_text_iter(
                         str_iter,
                         *exec_ctx,
                         &tmp_alloc/*res_allocator*/,
                         &tmp_alloc/*tmp_allocator*/))) {
          } else if (OB_FAIL(str_iter.get_full_data(data_str))) {
          } else if (OB_FAIL(loc.get_lob_data_byte_len(lob_data_byte_len))) {
          }
        } 
        if (OB_SUCC(ret)) {
          // mock result buffer and reserve data length
          // could do streaming charset convert
          ObTextStringResult new_tmp_lob(type, value.has_lob_header(), &allocator);
          char *buf = NULL;
          int64_t buf_len = 0;
          uint32_t result_len = 0;
          int64_t converted_len = lob_data_byte_len * ObCharset::CharConvertFactorNum;
          if (OB_FAIL(new_tmp_lob.init(converted_len))) {
          } else if (OB_FAIL(new_tmp_lob.get_reserved_buffer(buf, buf_len))) {
          } else if (OB_FAIL(convert_string_charset(data_str, from_collation_type, to_collation_type,
                                                    buf, buf_len, result_len))) {
          } else if (OB_FAIL(new_tmp_lob.lseek(result_len, 0))) {
          } else {
            ObString lob_loc_str;
            new_tmp_lob.get_result_buffer(lob_loc_str);
            value.set_lob_value(type, lob_loc_str.ptr(), lob_loc_str.length());
            value.set_collation_type(to_collation_type);
            if (new_tmp_lob.has_lob_header()) {
              value.set_has_lob_header();
            }
          }
        }
      }
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
