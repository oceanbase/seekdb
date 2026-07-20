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

#include "ob_sync_cmd_driver.h"

#include "obsm_row.h"
#include "sql/resolver/cmd/ob_variable_set_stmt.h"
#include "observer/mysql/obmp_query.h"
#include "rpc/obmysql/packet/ompk_row.h"
#include "sql/engine/expr/ob_expr_sql_udt_utils.h"

namespace oceanbase
{
using namespace common;
using namespace sql;
using namespace obmysql;
using namespace share;
namespace observer
{

ObSyncCmdDriver::ObSyncCmdDriver(const ObGlobalContext &gctx,
                                 const ObSqlCtx &ctx,
                                 sql::ObSQLSessionInfo &session,
                                 ObQueryRetryCtrl &retry_ctrl,
                                 ObIMPPacketSender &sender)
    : ObQueryDriver(gctx, ctx, session, retry_ctrl, sender)
{
}

ObSyncCmdDriver::~ObSyncCmdDriver()
{
}

int ObSyncCmdDriver::send_eof_packet(bool has_more_result)
{
  int ret = OB_SUCCESS;
  OMPKEOF eofp;

  if (OB_FAIL(seal_eof_packet(has_more_result, eofp))) {
    LOG_WARN("failed to seal eof packet", K(ret), K(has_more_result));
  } else if (OB_FAIL(sender_.response_packet(eofp, &session_))) {
    LOG_WARN("response packet fail", K(ret), K(has_more_result));
  }
  return ret;
}

int ObSyncCmdDriver::seal_eof_packet(bool has_more_result, OMPKEOF& eofp)
{
  int ret = OB_SUCCESS;
  const ObWarningBuffer *warnings_buf = common::ob_get_tsi_warning_buffer();
  uint16_t warning_count = 0;
  if (OB_ISNULL(warnings_buf)) {
    // ignore ret
    LOG_WARN("can not get thread warnings buffer", K(warnings_buf));
  } else {
    warning_count = static_cast<uint16_t>(warnings_buf->get_readable_warning_count());
  }
  eofp.set_warning_count(warning_count);
  ObServerStatusFlags flags = eofp.get_server_status();
  flags.status_flags_.OB_SERVER_STATUS_IN_TRANS
    = (session_.is_server_status_in_transaction() ? 1 : 0);
  flags.status_flags_.OB_SERVER_STATUS_AUTOCOMMIT = (session_.get_local_autocommit() ? 1 : 0);
  flags.status_flags_.OB_SERVER_MORE_RESULTS_EXISTS = has_more_result;
  // flags.status_flags_.OB_SERVER_PS_OUT_PARAMS = 1;
  eofp.set_server_status(flags);

  return ret;
}

int ObSyncCmdDriver::response_query_result(sql::ObResultSet &result,
                                           bool is_ps_protocol,
                                           bool has_more_result,
                                           bool &can_retry,
                                           int64_t fetch_limit)
{
  return ObQueryDriver::response_query_result(
    result, is_ps_protocol, has_more_result, can_retry, fetch_limit);
}


void ObSyncCmdDriver::free_output_row(ObMySQLResultSet &result)
{
  if (OB_NOT_NULL(result.get_exec_context().get_output_row())) {
    const ObNewRow *row = result.get_exec_context().get_output_row();
    for (int64_t i = 0; i < row->get_count(); ++i) {
      ObObj &obj = row->cells_[i];
      if (obj.is_pl_extend()) {
        (void)pl::ObUserDefinedType::destruct_obj(obj, &session_);
      }
    }
  }
}

int ObSyncCmdDriver::response_result(ObMySQLResultSet &result)
{
  ACTIVE_SESSION_FLAG_SETTER_GUARD(in_sql_execution);
  int ret = OB_SUCCESS;
  bool process_ok = false;
  // for select SQL
  OMPKEOF eofp;
  bool need_send_eof = false;
  if (OB_FAIL(result.open())) {
    // Only retry when open fails, because open will start transactions/statements, etc., and no information is returned to the user
    int cret = OB_SUCCESS;
    int cli_ret = OB_SUCCESS;
    if (ObStmt::is_ddl_stmt(result.get_stmt_type(), result.has_global_variable())) {
      // even failed, still need update lsv, as drop multi tables are not in one trx.
      cret = process_schema_version_changes(result);
      if (OB_SUCCESS != cret) {
        LOG_WARN("failed to set schema version changes", K(cret));
      }
    }

    cret = result.close();
    if (cret != OB_SUCCESS) {
      LOG_WARN("close result set fail", K(cret));
    }
    // open failed, decide whether to retry
    retry_ctrl_.test_and_save_retry_state(gctx_, ctx_, result, ret, cli_ret);
    LOG_WARN("result set open failed, check if need retry",
             K(ret), K(cli_ret), K(retry_ctrl_.need_retry()));
    ret = cli_ret;
  } else if (result.is_with_rows()) {
    if (!result.is_pl_stmt(result.get_stmt_type())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("Not SELECT, should not have any row!!!", K(ret));
    } else if (OB_FAIL(response_query_result(result))) {
      LOG_WARN("response query result fail", K(ret));
      free_output_row(result);
      int cret = result.close();
      if (cret != OB_SUCCESS) {
        LOG_WARN("close result set fail", K(cret));
      }
    } else {
      if (OB_FAIL(seal_eof_packet(result.has_more_result(), eofp))) {
        LOG_WARN("failed to send eof package", K(ret), K(result.has_more_result()));
      } else {
        need_send_eof = true;
      }
    }
  }

  if (OB_SUCC(ret)) {
    // for CRUD SQL
    // must be called before result.close()
    process_schema_version_changes(result);
    free_output_row(result);
    if (OB_FAIL(result.close())) {
      LOG_WARN("close result set fail", K(ret));
    } else if (!result.is_with_rows()) {
      process_ok = true;
      ObOKPParam ok_param;
      ok_param.message_ = const_cast<char*>(result.get_message());
      ok_param.affected_rows_ = result.get_affected_rows();
      ok_param.lii_ = result.get_last_insert_id_to_client();
      const ObWarningBuffer *warnings_buf = common::ob_get_tsi_warning_buffer();
      if (OB_ISNULL(warnings_buf)) {
        // ignore ret
        LOG_WARN("can not get thread warnings buffer");
      } else {
        ok_param.warnings_count_ =
            static_cast<uint16_t>(warnings_buf->get_readable_warning_count());
      }
      ok_param.has_more_result_ = result.has_more_result();
      if (need_send_eof) {
        if (OB_FAIL(sender_.send_ok_packet(session_, ok_param, &eofp))) {
          LOG_WARN("send ok packet fail", K(ok_param), K(ret));
        }
      } else {
        if (OB_FAIL(sender_.send_ok_packet(session_, ok_param))) {
          LOG_WARN("send ok packet fail", K(ok_param), K(ret));
        }
      }
    } else {
      if (need_send_eof && OB_FAIL(sender_.response_packet(eofp, &session_))) {
        LOG_WARN("response packet fail", K(ret));
      }
    }
  } else { /*do nothing*/ }

  if (!OB_SUCC(ret) && !process_ok && !retry_ctrl_.need_retry()) {
    int sret = OB_SUCCESS;
    if (OB_SUCCESS != (sret = sender_.send_error_packet(ret, NULL))) {
      LOG_WARN("send error packet fail", K(sret), K(ret));
    }
  }
  return ret;
}

// must be called before result.close()
// Keep a session-local schema fence after DDL so the next statement observes it.
int ObSyncCmdDriver::process_schema_version_changes(
    const ObMySQLResultSet &result)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(gctx_.schema_service_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("invalid schema service", K(ret));
  } else {
    
    if (ObStmt::is_ddl_stmt(result.get_stmt_type(), result.has_global_variable())) {
      if (OB_FAIL(ObSQLUtils::update_session_last_schema_version(*gctx_.schema_service_,
                                                                 session_))) {
        LOG_WARN("fail to update session last schema_version", K(ret));
      }
    }
  }
  return ret;
}
int ObSyncCmdDriver::response_query_result(ObMySQLResultSet &result)
{
  int ret = OB_SUCCESS;
  const common::ObNewRow *row = NULL;
  if (OB_FAIL(result.next_row(row)) ) {
    LOG_WARN("fail to get next row", K(ret));
  } else if (OB_FAIL(response_query_header(result, result.has_more_result(), true))) {
    LOG_WARN("fail to response query header", K(ret));
  } else if (OB_ISNULL(ctx_.session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session info is null", K(ret));
  } else {
    ObCharsetType charset_type = CHARSET_INVALID;
    
    if (OB_SUCC(ret)) {
      const ObSQLSessionInfo &my_session = result.get_session();
      if (OB_FAIL(my_session.get_character_set_results(charset_type))) {
        LOG_WARN("fail to get result charset", K(ret));
      } 
    }

    ObNewRow *tmp_row = const_cast<ObNewRow*>(row);
    for (int64_t i = 0; OB_SUCC(ret) && i < tmp_row->get_count(); i++) {
      ObObj& value = tmp_row->get_cell(i);
      if (ob_is_string_tc(value.get_type()) && CS_TYPE_INVALID != value.get_collation_type()) {
        OZ(convert_string_value_charset(value, result, charset_type));
      } else if (ob_is_text_tc(value.get_type())
                && OB_FAIL(convert_text_value_charset(value, result, charset_type))) {
        LOG_WARN("convert text value charset failed", K(ret));
      }
      if (OB_FAIL(ret)) {
      } else if ((value.is_lob() || value.is_json() || value.is_geometry())
                  && OB_FAIL(process_lob_locator_results(value, result))) {
        LOG_WARN("convert lob locator to longtext failed", K(ret));
      } else if ((value.is_collection_sql_type() || value.is_geometry()) &&
                 OB_FAIL(ObSqlUdtUtils::convert_result_for_client(value, result))) {
        LOG_WARN("convert udt to client format failed", K(ret), K(value.get_udt_subschema_id()));
      }
    }

    if (OB_SUCC(ret)) {
      MYSQL_PROTOCOL_TYPE protocol_type = result.is_ps_protocol() ? MYSQL_PROTOCOL_TYPE::BINARY : MYSQL_PROTOCOL_TYPE::TEXT;
      const ObSQLSessionInfo *tmp_session = result.get_exec_context().get_my_session();
      const ObDataTypeCastParams dtc_params = ObBasicSessionInfo::create_dtc_params(tmp_session);
      ObSMRow sm_row(protocol_type,
                     *row,
                     dtc_params,
                     *tmp_session,
                     result.get_field_columns(),
                     ctx_.schema_guard_);
      OMPKRow rp(sm_row);
      if (OB_FAIL(sender_.response_packet(rp, const_cast<ObSQLSessionInfo *>(tmp_session)))) {
        LOG_WARN("response packet fail", K(ret), KP(row));
      } else {
        ObArenaAllocator *allocator = NULL;
        if (OB_FAIL(result.get_exec_context().get_convert_charset_allocator(allocator))) {
          LOG_WARN("fail to get lob fake allocator", K(ret));
        } else if (OB_NOT_NULL(allocator)) {
          allocator->reset();
        }
      }
    }
  }
  return ret;
}

}/* ns observer*/
}/* ns oceanbase */
