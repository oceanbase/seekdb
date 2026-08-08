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

#include "nio.h"

#include "obmp_stmt_execute.h"
#include "lib/ob_running_mode.h"
#include "observer/mysql/ob_mysql_result_set.h"
#include "lib/trace/ob_trace.h"
#include "query/protocol/ob_mysql_protocol_util.h"
#include "rpc/obmysql/packet/ompk_field.h"
#include "rpc/obmysql/packet/ompk_resheader.h"
#include "rpc/obmysql/packet/ompk_row.h"
#include "observer/mysql/obsm_row.h"
#include "share/ob_lob_access_utils.h"
#include "share/ob_time_utility2.h"
#include "sql/ob_sql.h"
#include "observer/omt/ob_server_runtime.h"
#include "observer/mysql/ob_sync_plan_driver.h"
#include "observer/mysql/ob_sync_cmd_driver.h"
#include "observer/mysql/ob_async_cmd_driver.h"
#include "observer/mysql/ob_async_plan_driver.h"
#include "pl/ob_pl_package.h"
#include "sql/pl/ob_pl_server_cursor.h"
#include "sql/session/ob_piece_cache.h"
#include "sql/plan_cache/ob_ps_cache.h"

namespace oceanbase
{

using namespace common;
using namespace share;
using namespace obmysql;
using namespace rpc;
using namespace sql;
using namespace pl;
namespace observer
{
inline int ObPSAnalysisChecker::detection(const int64_t len)
{
  int ret = OB_SUCCESS;
  if (!need_check_) {
  } else if (*pos_ + len > end_pos_) {
    ret = OB_ERR_MALFORMED_PS_PACKET;
    LOG_USER_ERROR(OB_ERR_MALFORMED_PS_PACKET);
    LOG_ERROR("malformed ps data packet, please check the number and content of data packet parameters", K(ret), KP(pos_), KP(begin_pos_),
    K(end_pos_ - begin_pos_), K(len), K(data_len_), K(remain_len()));
  }
  return ret;
}

void ObPsSessionInfoParamsCleaner::operator()(
    common::hash::HashMapPair<uint64_t, ObPsSessionInfo *> &entry) {
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(entry.second)) {
    ObPsSessionInfo *ps_session_info =
        static_cast<ObPsSessionInfo *>(entry.second);
    ps_session_info->get_param_types().reuse();
    ps_session_info->get_param_type_flags().reuse();
  } else {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "ps session info pointer is NULL", K(ret));
  }
  ret_ = ret;
}

void ObPsSessionInfoParamsAssignment::operator()(
    common::hash::HashMapPair<uint64_t, ObPsSessionInfo *> &entry) {
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(entry.second)) {
    ObPsSessionInfo *ps_session_info =
        static_cast<ObPsSessionInfo *>(entry.second);
    if (param_types_.count() != param_type_flags_.count()) {
      ret = OB_INVALID_ARGUMENT;
      SERVER_LOG(WARN, "ps parameter type cache arrays disagree", K(ret),
                 K(param_types_.count()), K(param_type_flags_.count()));
    } else if (OB_FAIL(ps_session_info->get_param_types().reserve(
                   param_types_.count()))) {
      SERVER_LOG(WARN, "failed to reserve ps parameter type cache", K(ret));
    } else if (OB_FAIL(ps_session_info->get_param_type_flags().reserve(
                   param_type_flags_.count()))) {
      SERVER_LOG(WARN, "failed to reserve ps parameter flag cache", K(ret));
    } else if (OB_FAIL(
                   ps_session_info->get_param_types().assign(param_types_))) {
      SERVER_LOG(WARN, "failed to assign ps parameter types", K(ret));
    } else if (OB_FAIL(ps_session_info->get_param_type_flags().assign(
                   param_type_flags_))) {
      SERVER_LOG(WARN, "failed to assign ps parameter type flags", K(ret));
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "ps session info pointer is NULL", K(ret));
  }
  ret_ = ret;
}

ObMPStmtExecute::ObMPStmtExecute(const share::ObGlobalContext &gctx)
    : ObMPBase(gctx),
      retry_ctrl_(/*ctx_.retry_info_*/),
      ctx_(),
      stmt_id_(),
      stmt_type_(stmt::T_NONE),
      params_(NULL),
      arraybinding_params_(NULL),
      arraybinding_columns_(NULL),
      arraybinding_row_(NULL),
      is_arraybinding_(false),
      is_save_exception_(false),
      arraybinding_size_(0),
      arraybinding_rowcnt_(0),
      ps_cursor_type_(ObNormalType),
      single_process_timestamp_(0),
      exec_start_timestamp_(0),
      exec_end_timestamp_(0),
      prepare_packet_sent_(false),
      params_num_(0),
      params_value_len_(0),
      params_value_(NULL),
      curr_sql_idx_(0)
{
  ctx_.exec_type_ = MpQuery;
}

int ObMPStmtExecute::init_arraybinding_field(int64_t column_field_cnt,
                                             const ColumnsFieldIArray *column_fields)
{
  int ret = OB_SUCCESS;

  ObField sql_no_field, err_no_field, err_msg_field;

  OX (sql_no_field.charsetnr_ = CS_TYPE_UTF8MB4_GENERAL_CI);
  OX (sql_no_field.type_.set_type(ObIntType));
  OZ (common::ObField::get_field_mb_length(sql_no_field.type_.get_type(),
                                           sql_no_field.accuracy_,
                                           common::CS_TYPE_INVALID,
                                           sql_no_field.length_));
  OX (sql_no_field.cname_ = ObString("sql_no"));

  OX (err_no_field.charsetnr_ = CS_TYPE_UTF8MB4_GENERAL_CI);
  OX (err_no_field.type_.set_type(ObIntType));
  OZ (common::ObField::get_field_mb_length(err_no_field.type_.get_type(),
                                           err_no_field.accuracy_,
                                           common::CS_TYPE_INVALID, err_no_field.length_));
  OX (err_no_field.cname_ = ObString("error_code"));

  OX (err_msg_field.charsetnr_ = CS_TYPE_UTF8MB4_GENERAL_CI);
  OX (err_msg_field.type_.set_type(ObVarcharType));
  OZ (common::ObField::get_field_mb_length(err_msg_field.type_.get_type(),
                                           err_msg_field.accuracy_,
                                           common::CS_TYPE_INVALID,
                                           err_msg_field.length_));
  OX (err_msg_field.cname_ = ObString("error_message"));

  OZ (arraybinding_columns_->push_back(sql_no_field));
  OZ (arraybinding_columns_->push_back(err_no_field));
  OZ (arraybinding_columns_->push_back(err_msg_field));

  return ret;
}

int ObMPStmtExecute::init_row_for_arraybinding(ObIAllocator &alloc, int64_t array_binding_row_num)
{
  int ret = OB_SUCCESS;
  ObObj* obj = static_cast<ObObj*>(alloc.alloc(sizeof(ObObj) * array_binding_row_num));
  if (OB_ISNULL(obj)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc memory for row", K(ret));
  } else {
    ObObj *ptr = obj;
    for (int64_t i = 0; i < array_binding_row_num; ++i) {
      ptr = new(ptr)ObObj();
      ptr++;
    }
    arraybinding_row_->assign(obj, array_binding_row_num);
  }
  return ret;
}

int ObMPStmtExecute::init_arraybinding_paramstore(ObIAllocator &alloc)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(arraybinding_params_
      = static_cast<ParamStore*>(alloc.alloc(sizeof(ParamStore))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate memory", K(ret));
  }
  OX (arraybinding_params_ = new(arraybinding_params_)ParamStore((ObWrapperAllocator(alloc))));
  return ret;
}


int ObMPStmtExecute::init_for_arraybinding(ObIAllocator &alloc)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(arraybinding_params_
      = static_cast<ParamStore*>(alloc.alloc(sizeof(ParamStore))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate memory", K(ret));
  } else if (is_save_exception_) {
    if (OB_ISNULL(arraybinding_columns_
        = static_cast<ColumnsFieldArray*>(alloc.alloc(sizeof(ColumnsFieldArray))))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate memory", K(ret));
    } else if (OB_ISNULL(arraybinding_row_
        = static_cast<ObNewRow*>(alloc.alloc(sizeof(ObNewRow))))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate memory", K(ret));
    } else {
      arraybinding_columns_
        = new(arraybinding_columns_)ColumnsFieldArray(alloc, 3);
      arraybinding_row_ = new(arraybinding_row_)ObNewRow();
    }
    OZ (init_arraybinding_field(3, NULL));
    OZ (init_row_for_arraybinding(alloc, 3));
  }
  OX (arraybinding_params_ = new(arraybinding_params_)ParamStore((ObWrapperAllocator(alloc))));
  return ret;
}

int ObMPStmtExecute::check_precondition_for_arraybinding(const ObSQLSessionInfo &session_info)
{
  int ret = OB_SUCCESS;
  if (!ObStmt::is_dml_write_stmt(stmt_type_)
      && stmt::T_ANONYMOUS_BLOCK != stmt_type_
      && stmt::T_CALL_PROCEDURE != stmt_type_) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("arraybinding only support write dml", K(ret), K(stmt_type_));
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "arraybinding got no write dml");
  } else if (session_info.get_local_autocommit()) {  // read system variable after session info synchronized
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("arraybinding must in autocommit off", K(ret));
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "arraybinding has autocommit = on");
  }
  return ret;
}

int ObMPStmtExecute::check_param_type_for_arraybinding(ParamTypeInfoArray &param_type_infos)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(param_type_infos.count() <= 0)) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("arraybinding must has parameters", K(ret));
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "arraybinding has no parameter");
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < param_type_infos.count(); ++i) {
      TypeInfo &type_info = param_type_infos.at(i);
      if (type_info.is_basic_type_ || !type_info.is_elem_type_) {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("arraybinding parameter must be anonymous array", K(ret));
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "arraybinding parameter is not anonymous array");
      }
    }
  }
  return ret;
}

int ObMPStmtExecute::check_param_value_for_arraybinding(ObObjParam &param)
{
  int ret = OB_SUCCESS;
  ObPLCollection *coll = NULL;
  CK (param.is_ext());
  CK (OB_NOT_NULL(coll = reinterpret_cast<ObPLCollection*>(param.get_ext())));
  if (OB_FAIL(ret)) {
  } else if (0 == arraybinding_size_) {
    arraybinding_size_ = coll->get_count();
  } else {
    CK (arraybinding_size_ == coll->get_count());
  }
  return ret;
}

int ObMPStmtExecute::construct_execute_param_for_arraybinding(int64_t pos)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(arraybinding_params_));
  CK (OB_NOT_NULL(params_));
  CK (arraybinding_params_->count() == params_->count());
  for (int64_t i = 0; OB_SUCC(ret) && i < arraybinding_params_->count(); ++i) {
    ObObjParam &obj = arraybinding_params_->at(i);
    ObPLCollection *coll = NULL;
    ObObj *data = NULL;
    CK (obj.is_ext());
    CK (OB_NOT_NULL(coll = reinterpret_cast<ObPLCollection*>(obj.get_ext())));
    CK (coll->get_count() > pos);
    CK (1 == coll->get_column_count());
    CK (OB_NOT_NULL(data = reinterpret_cast<ObObj*>(coll->get_data())));
    if (stmt::T_ANONYMOUS_BLOCK == stmt_type_) {
      // for anonymous block, no need to convert int type to number
      OX (params_->at(i) = *(data + pos));
    } else {
      OZ (param_assign_after_convert_int2number(params_->at(i), *(data + pos)));
    }
    if (data[pos].is_numeric_type()) {
      ObAccuracy default_acc =
        ObAccuracy::DDL_DEFAULT_ACCURACY[data[pos].get_type()];
      if (params_->at(i).get_scale() == NUMBER_SCALE_UNKNOWN_YET) {
        params_->at(i).set_scale(default_acc.get_scale());
      }
      if (params_->at(i).get_precision() == PRECISION_UNKNOWN_YET) {
        params_->at(i).set_precision(default_acc.get_precision());
      }
    }
    params_->at(i).set_param_meta();
  }
  return ret;
}

// Convert int to number before passing params to the SQL layer when the target anonymous-array
// element uses number semantics. The conversion lives here because `parse_integer_value()` cannot
// distinguish whether the current anonymous array is of arraybinding structure when deserializing
// an integer in the anonymous array.
int ObMPStmtExecute::param_assign_after_convert_int2number(ObObj& dst, const ObObj& src)
{
  int ret = OB_SUCCESS;
  ObIAllocator &alloc = CURRENT_CONTEXT->get_arena_allocator();
  number::ObNumber ob_num;
  switch (src.get_type()) {
    // do the cast which we should have done in parse_integer_value()
    // EMySQLFieldType::MYSQL_TYPE_SHORT
    case ObSmallIntType:
      OZ (ob_num.from(static_cast<int64_t>(src.get_smallint()), alloc), src);
      OX (dst.set_number(ob_num));
      break;
    case ObUSmallIntType:
      OZ (ob_num.from(static_cast<uint64_t>(src.get_usmallint()), alloc), src);
      OX (dst.set_number(ob_num));
      break;
    // EMySQLFieldType::MYSQL_TYPE_LONG
    case ObInt32Type:
      OZ (ob_num.from(static_cast<int64_t>(src.get_int32()), alloc), src);
      OX (dst.set_number(ob_num));
      break;
    case ObUInt32Type:
      OZ (ob_num.from(static_cast<uint64_t>(src.get_uint32()), alloc), src);
      OX (dst.set_number(ob_num));
      break;
    // EMySQLFieldType::MYSQL_TYPE_LONGLONG
    case ObIntType:
      OZ (ob_num.from(src.get_int(), alloc), src);
      OX (dst.set_number(ob_num));
      break;
    case ObUInt64Type:
      OZ (ob_num.from(src.get_uint64(), alloc), src);
      OX (dst.set_number(ob_num));
      break;
    default:
      OX (dst = src);
      break;
  }
  return ret;
}

void ObMPStmtExecute::reset_complex_param_memory(ParamStore *params, ObSQLSessionInfo *session_info)
{
  if (OB_NOT_NULL(params)) {
    for (int64_t i = 0; i < params->count(); ++i) {
      ObObjParam &obj = params->at(i);
      if (obj.is_pl_extend()) {
        int ret = ObUserDefinedType::destruct_obj(obj, session_info);
        if (OB_SUCCESS != ret) {
          LOG_WARN("fail to destruct obj", K(ret), K(i));
        }
      }
      obj.set_null();
    }
  }
}

int ObMPStmtExecute::send_eof_packet_for_arraybinding(ObSQLSessionInfo &session_info)
{
  int ret = OB_SUCCESS;

  OMPKEOF eofp;
  const ObWarningBuffer *warnings_buf = common::ob_get_tsi_warning_buffer();
  uint16_t warning_count = 0;
  if (OB_ISNULL(warnings_buf)) {
    // ignore ret
    LOG_WARN("can not get thread warnings buffer");
  } else {
    warning_count = static_cast<uint16_t>(warnings_buf->get_readable_warning_count());
  }
  eofp.set_warning_count(warning_count);
  ObServerStatusFlags flags = eofp.get_server_status();
  flags.status_flags_.OB_SERVER_STATUS_IN_TRANS
    = (session_info.is_server_status_in_transaction() ? 1 : 0);
  flags.status_flags_.OB_SERVER_STATUS_AUTOCOMMIT = (session_info.get_local_autocommit() ? 1 : 0);
  flags.status_flags_.OB_SERVER_MORE_RESULTS_EXISTS = true;
  eofp.set_server_status(flags);
  OZ(response_packet(eofp));

  return ret;
}

int ObMPStmtExecute::response_result_for_arraybinding(
    ObSQLSessionInfo &session_info,
    ObIArray<ObSavedException> &exception_array)
{
  int ret = OB_SUCCESS;
  if (exception_array.count() > 0) {
    {
      OMPKResheader rhp;
      rhp.set_field_count(3);
      OZ(response_packet(rhp));

      for (int64_t i = 0; OB_SUCC(ret) && i < arraybinding_columns_->count(); ++i) {
        ObMySQLField field;
        OZ (ObMySQLResultSet::to_mysql_field(arraybinding_columns_->at(i), field));
        ObMySQLResultSet::replace_lob_type(field);
        OMPKField fp(field);
        OZ(response_packet(fp));
      }

      OZ (send_eof_packet_for_arraybinding(session_info));

      const ObDataTypeCastParams dtc_params =
          ObBasicSessionInfo::create_dtc_params(&session_info);
      for (int64_t i = 0; OB_SUCC(ret) && i < exception_array.count(); ++i) {
        arraybinding_row_->get_cell(0).set_int(exception_array.at(i).pos_);
        arraybinding_row_->get_cell(1).set_int(exception_array.at(i).error_code_);
        arraybinding_row_->get_cell(2).set_varchar(exception_array.at(i).error_msg_);

        ObSMRow sm_row(BINARY,
                *arraybinding_row_,
                dtc_params,
                session_info,
                arraybinding_columns_,
                ctx_.schema_guard_);
        OMPKRow rp(sm_row);
        OZ(response_packet(rp));
      }
      OZ (send_eof_packet_for_arraybinding(session_info));
    }
  }

  if (OB_SUCC(ret)) {
    bool ps_out = ((stmt::T_ANONYMOUS_BLOCK == stmt_type_ || stmt::T_CALL_PROCEDURE == stmt_type_)
                    && arraybinding_columns_->count() > 3) ? true : false;
    ObOKPParam ok_param;
    ok_param.affected_rows_ = arraybinding_rowcnt_;
    ok_param.has_pl_out_ = ps_out;
    OZ (send_ok_packet(session_info, ok_param));
  }
  return ret;
}

int ObMPStmtExecute::save_exception_for_arraybinding(
  int64_t pos, int error_code, ObIArray<ObSavedException> &exception_array)
{
  int ret = OB_SUCCESS;
  ObSavedException exception;

  const char *errm_result = NULL;
  int64_t errm_length = 0;

  exception.pos_ = pos;
  exception.error_code_ = static_cast<uint16_t>(ob_errpkt_errno(error_code));

  ObIAllocator &alloc = CURRENT_CONTEXT->get_arena_allocator();

  const ObWarningBuffer *wb = common::ob_get_tsi_warning_buffer();
  if (OB_LIKELY(NULL != wb) && wb->get_err_code() == error_code) {
    errm_result = wb->get_err_msg();
    errm_length = strlen(errm_result);
  } else {
    errm_result = ob_errpkt_strerror(error_code);
    if (NULL == errm_result) {
      errm_result = "OBE%ld: Message error_code not found; product=RDBMS; facility=ORA";
    }
    errm_length = strlen(errm_result);
  }

  OZ (ob_write_string(alloc, ObString(errm_length, errm_result), exception.error_msg_));
  OZ (exception_array.push_back(exception));
  return ret;
}

int ObMPStmtExecute::after_do_process_for_arraybinding(ObMySQLResultSet &result)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(result.get_physical_plan())) {
    ret = OB_NOT_INIT;
    LOG_WARN("should have set plan to result set", K(ret));
  } else if (OB_FAIL(result.open())) {
    int cret = OB_SUCCESS;
    int cli_ret = OB_SUCCESS;
    retry_ctrl_.test_and_save_retry_state(gctx_,
                                          ctx_,
                                          result,
                                          ret,
                                          cli_ret,
                                          true/*arraybinding only local retry*/);
    if (OB_TRANSACTION_SET_VIOLATION != ret && OB_REPLICA_NOT_READABLE != ret) {
      if (OB_TRY_LOCK_ROW_CONFLICT == ret && retry_ctrl_.need_retry()) {
        //Lock conflict retry does not print logs to avoid screen flooding
      } else {
        LOG_WARN("result set open failed, check if need retry",
                 K(ret), K(cli_ret), K(retry_ctrl_.need_retry()));
      }
    }
    ret = cli_ret;
    cret = result.close();
    if (cret != OB_SUCCESS &&
        cret != OB_TRANSACTION_SET_VIOLATION &&
        OB_TRY_LOCK_ROW_CONFLICT != cret) {
      LOG_WARN("close result set fail", K(cret));
    }
  } else if (result.is_with_rows()) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("in arraybinding, dml with rows is not supported", K(ret));
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "in arraybinding, dml with rows");
  } else {
    OZ (result.close());
    OX (arraybinding_rowcnt_ += result.get_affected_rows());
  }
  return ret;
}

int ObMPStmtExecute::before_process()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObMPBase::before_process())) {
    LOG_WARN("fail to call before process", K(ret));
  } else if ((OB_ISNULL(req_))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("request should not be null", K(ret));
  } else if (req_->get_type() != ObRequest::OB_MYSQL) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("invalid request", K(ret), K_(*req));
  } else {
    ObIAllocator &alloc = CURRENT_CONTEXT->get_arena_allocator();
    if (OB_ISNULL(params_ = static_cast<ParamStore *>(alloc.alloc(sizeof(ParamStore))))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate memory", K(ret));
    } else {
      params_ = new(params_)ParamStore( (ObWrapperAllocator(alloc)) );
    }
    const ObMySQLRawPacket &pkt =
        reinterpret_cast<const ObMySQLRawPacket &>(req_->get_packet());
    ObString param_tail;
    const char *pos = NULL;
    uint32_t ps_stmt_checksum = 0;
    ObSQLSessionInfo *session = NULL;
    if (ObMySQLCommandLayout::EXECUTE != pkt.get_command_layout()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("stmt-execute command view has wrong layout", K(ret), "layout",
                static_cast<uint32_t>(pkt.get_command_layout()));
    } else if (OB_FAIL(pkt.get_command_field(0, param_tail))) {
      LOG_WARN("get stmt-execute parameter tail failed", K(ret));
    } else {
      stmt_id_ = pkt.get_command_scalar0();
      ps_stmt_checksum = static_cast<uint32_t>(pkt.get_command_scalar1());
      const uint8_t flag = static_cast<uint8_t>(pkt.get_command_scalar2());
      pos = param_tail.ptr();
      analysis_checker_.init(pos, param_tail.length());
      const uint8_t ARRAYBINDING_MODE = 8;
      const uint8_t SAVE_EXCEPTION_MODE = 16;
      is_arraybinding_ = flag & ARRAYBINDING_MODE;
      is_save_exception_ = flag & SAVE_EXCEPTION_MODE;
      ps_cursor_type_ = 0 != (flag & CURSOR_TYPE_READ_ONLY)
                          ? ObExecutePsCursorType
                          : ObNormalType;
      if (is_arraybinding_) {
        OZ (init_for_arraybinding(alloc));
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(get_session(session))) {
      LOG_WARN("get session failed");
    } else if (OB_ISNULL(session)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("session is NULL or invalid", K(ret), K(session));
    } else {
      OZ (request_params(session, pos, ps_stmt_checksum, alloc));
      OZ (store_params_value_to_str(alloc, *session));
    }
    if (session != NULL) {
      revert_session(session);
    }
  }
  if (OB_FAIL(ret)) {
    send_error_packet(ret, NULL);
    if (OB_ERR_PREPARE_STMT_CHECKSUM == ret) {
      force_disconnect();
      LOG_ERROR("prepare stmt checksum error, disconnect connection", K(ret));
    }
    flush_buffer(true);
  }

  return ret;
}

int ObMPStmtExecute::after_process(int error_code)
{
  int ret = OB_SUCCESS;
  reset_complex_param_memory(arraybinding_params_);
  reset_complex_param_memory(params_);
  if (OB_FAIL(ObMPBase::after_process(error_code))) {
    LOG_WARN("after process fail", K(ret));
  }
  return ret;
}

int ObMPStmtExecute::store_params_value_to_str(ObIAllocator &alloc, sql::ObSQLSessionInfo &session)
{
  return sql::store_params_value_to_str(alloc, session, params_, params_value_, params_value_len_);
}

int ObMPStmtExecute::parse_request_type(const char *&pos, int64_t num_of_params,
                                        int8_t new_param_bound_flag,
                                        ObCollationType cs_type,
                                        ParamTypeArray &param_types,
                                        ParamTypeFlagArray &param_type_flags,
                                        ParamTypeInfoArray &param_type_infos
                                        /*ParamCastArray param_cast_infos*/) {
  int ret = OB_SUCCESS;
  // Step3: get type info
  if (param_type_infos.count() < num_of_params) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("type array length is not normal", K(ret), K(param_types.count()), K(param_type_infos.count()));
  }
  for (int i = 0; OB_SUCC(ret) && i < num_of_params; ++i) {
    uint8_t type = 0;
    int8_t flag = 0;
    TypeInfo &type_name_info = param_type_infos.at(i);
    if (1 == new_param_bound_flag) {
      PS_DEFENSE_CHECK(2) // type(1) + flag(1)
      {
        ObMySQLUtil::get_uint1(pos, type);
        ObMySQLUtil::get_int1(pos, flag);
        if (OB_FAIL(param_types.push_back(static_cast<EMySQLFieldType>(type)))) {
          LOG_WARN("fail to push back", K(type), K(i));
        } else if (OB_FAIL(param_type_flags.push_back(
                       static_cast<uint8_t>(flag)))) {
          LOG_WARN("fail to push parameter type flags", K(flag), K(i));
        }
      }
    } else {
      if (num_of_params != param_types.count() ||
          num_of_params != param_type_flags.count()) {
        ret = OB_ERR_WRONG_DYNAMIC_PARAM;
        LOG_USER_ERROR(OB_ERR_WRONG_DYNAMIC_PARAM,
            param_types.count(), num_of_params);
      } else {
        type = static_cast<uint8_t>(param_types.at(i));
        flag = static_cast<int8_t>(param_type_flags.at(i));
      }
    }

    if (OB_SUCC(ret)) {
      if (EMySQLFieldType::MYSQL_TYPE_COMPLEX != type) {
        const int16_t unsigned_flag = 128;
        ObObjType ob_elem_type;
        if (OB_FAIL(ObSMUtils::get_ob_type(
                ob_elem_type, static_cast<EMySQLFieldType>(type),
                flag & unsigned_flag ? true : false))) {
          LOG_WARN("get ob type fail", K(type), K(flag));
        } else {
          type_name_info.elem_type_.set_obj_type(ob_elem_type);
        }
      }
      uint8_t elem_type = 0;
      if (OB_SUCC(ret) && EMySQLFieldType::MYSQL_TYPE_COMPLEX == type) {
        type_name_info.is_basic_type_ = false;
        if (OB_FAIL(decode_type_info(pos, type_name_info))) {
          LOG_WARN("failed to decode type info", K(ret));
        } else if (type_name_info.type_name_.empty()) {
          ObObjType ob_elem_type;
          type_name_info.is_elem_type_ = true;
          PS_DEFENSE_CHECK(1) // elem_type(1)
          {
            ObMySQLUtil::get_uint1(pos, elem_type);
          }
          OZ (ObSMUtils::get_ob_type(
            ob_elem_type, static_cast<EMySQLFieldType>(elem_type)), elem_type);
          OX (type_name_info.elem_type_.set_obj_type(ob_elem_type));
          if (OB_SUCC(ret)) {
            switch (elem_type) {
              case MYSQL_TYPE_ORA_BLOB: {
                type_name_info.elem_type_.set_collation_type(CS_TYPE_BINARY);
              } break;
              case MYSQL_TYPE_VARCHAR:
              case MYSQL_TYPE_STRING:
              case MYSQL_TYPE_VAR_STRING: {
                type_name_info.elem_type_.set_collation_type(cs_type);
                ObLengthSemantics ls = ctx_.session_info_->get_actual_length_semantics();
                if (LS_INVALIED == ls) {
                  type_name_info.elem_type_.set_length_semantics(LS_CHAR);
                } else {
                  type_name_info.elem_type_.set_length_semantics(ls);
                }
              } break;
              default: {
                type_name_info.elem_type_.set_collation_type(cs_type);
              } break;
            }
          }
          if (OB_SUCC(ret) && EMySQLFieldType::MYSQL_TYPE_COMPLEX == elem_type) {
            OZ (decode_type_info(pos, type_name_info));
          }
        }
      }
    }
  }
  return ret;
}

int ObMPStmtExecute::parse_request_param_value(ObIAllocator &alloc,
                                             sql::ObSQLSessionInfo *session,
                                             const char* &pos,
                                             int64_t idx,
                                             EMySQLFieldType &param_type,
                                             TypeInfo &param_type_info,
                                             ObObjParam &param,
                                             const char *bitmap)
{
  int ret = OB_SUCCESS;
  ObCharsetType charset = CHARSET_INVALID;
  ObCollationType cs_conn = CS_TYPE_INVALID;
  ObCollationType cs_server = CS_TYPE_INVALID;
  if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is null", K(ret));
  } else if (OB_FAIL(session->get_character_set_connection(charset))) {
    LOG_WARN("get charset for client failed", K(ret));
  } else if (OB_FAIL(session->get_collation_connection(cs_conn))) {
    LOG_WARN("get charset for client failed", K(ret));
  } else if (OB_FAIL(session->get_collation_server(cs_server))) {
    LOG_WARN("get charset for client failed", K(ret));
  }
  // Step5: decode value
  ObObjType ob_type;
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ObSMUtils::get_ob_type(
        ob_type, static_cast<EMySQLFieldType>(param_type)))) {
    LOG_WARN("cast ob type from mysql type failed",
              K(ob_type), K(param_type), K(ret));
  } else {
    param.set_type(ob_type);
    if (OB_FAIL(parse_param_value(alloc,
                                         param_type,
                                         charset,
                                         cs_conn,
                                         pos,
                                         session->get_timezone_info(),
                                         &param_type_info,
                                         param,
                                         bitmap,
                                         idx))) {
      LOG_WARN("get param value failed", K(param));
    } else {
      LOG_DEBUG("resolve execute with param", K(param));
    }
  }
  return ret;
}

bool ObMPStmtExecute::is_contain_complex_element(const sql::ParamTypeArray &param_types) const
{
  bool b_ret = false;
  for (int64_t i = 0; i < param_types.count(); i++) {
    const obmysql::EMySQLFieldType field_type = param_types.at(i);
    if (MYSQL_TYPE_COMPLEX == field_type) {
      b_ret = true;
      break;
    }
  }
  return b_ret;
}

int ObMPStmtExecute::set_standard_timestamp_param(
    const EMySQLFieldType field_type, const uint16_t year, const uint8_t month,
    const uint8_t day, const uint8_t hour, const uint8_t minute,
    const uint8_t second, const uint32_t microseconds, const bool is_zero,
    const ObTimeZoneInfo *tz_info, ObObj &param) {
  int ret = OB_SUCCESS;
  ObPreciseDateTime value = 0;
  if (!is_zero) {
    ObTime ob_time;
    ob_time.parts_[DT_YEAR] = year;
    ob_time.parts_[DT_MON] = month;
    ob_time.parts_[DT_MDAY] = day;
    ob_time.parts_[DT_HOUR] = hour;
    ob_time.parts_[DT_MIN] = minute;
    ob_time.parts_[DT_SEC] = second;
    ob_time.parts_[DT_USEC] = microseconds;
    if (!ObTimeUtility2::is_valid_date(year, month, day) ||
        !ObTimeUtility2::is_valid_time(hour, minute, second, microseconds)) {
      ret = OB_INVALID_DATE_FORMAT;
      LOG_WARN("invalid date components from Rust execute parser", K(ret),
               K(year), K(month), K(day), K(hour), K(minute), K(second),
               K(microseconds));
    } else {
      ObTimeConvertCtx cvrt_ctx(NULL, false);
      ob_time.parts_[DT_DATE] = ObTimeConverter::ob_time_to_date(ob_time);
      if (MYSQL_TYPE_DATE == field_type || MYSQL_TYPE_NEWDATE == field_type) {
        value = ob_time.parts_[DT_DATE];
      } else if (OB_FAIL(ObTimeConverter::ob_time_to_datetime(ob_time, cvrt_ctx,
                                                              value))) {
        LOG_WARN("convert typed execute datetime failed", K(ret), K(ob_time));
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (MYSQL_TYPE_TIMESTAMP == field_type) {
      int64_t timestamp = 0;
      if (OB_FAIL(ObTimeConverter::datetime_to_timestamp(value, tz_info,
                                                         timestamp))) {
        LOG_WARN("convert typed execute timestamp failed", K(ret), K(value));
      } else {
        param.set_timestamp(timestamp);
      }
    } else if (MYSQL_TYPE_DATETIME == field_type) {
      param.set_datetime(value);
    } else if (MYSQL_TYPE_DATE == field_type ||
               MYSQL_TYPE_NEWDATE == field_type) {
      param.set_date(static_cast<int32_t>(value));
    } else {
      ret = OB_ERR_ILLEGAL_TYPE;
      LOG_WARN("unexpected typed execute temporal type", K(ret), K(field_type));
    }
  }
  return ret;
}

int ObMPStmtExecute::set_standard_time_param(
    const uint32_t days, const uint8_t hour, const uint8_t minute,
    const uint8_t second, const uint32_t microseconds, const bool negative,
    const bool is_zero, ObObj &param) {
  int ret = OB_SUCCESS;
  int64_t value = 0;
  if (!is_zero) {
    ObTime ob_time;
    ob_time.parts_[DT_MDAY] = days;
    ob_time.parts_[DT_HOUR] = hour;
    ob_time.parts_[DT_MIN] = minute;
    ob_time.parts_[DT_SEC] = second;
    ob_time.parts_[DT_USEC] = microseconds;
    if (!ObTimeUtility2::is_valid_time(hour, minute, second, microseconds)) {
      ret = OB_INVALID_DATE_FORMAT;
      LOG_WARN("invalid time components from Rust execute parser", K(ret),
               K(days), K(hour), K(minute), K(second), K(microseconds));
    } else {
      ob_time.parts_[DT_DATE] = ObTimeConverter::ob_time_to_date(ob_time);
      ob_time.parts_[DT_HOUR] += ob_time.parts_[DT_MDAY] * 24;
      ob_time.parts_[DT_MDAY] = 0;
      value = ObTimeConverter::ob_time_to_time(ob_time);
      if (negative) {
        value = -value;
      }
    }
  }
  if (OB_SUCC(ret)) {
    param.set_time(value);
  }
  return ret;
}

int ObMPStmtExecute::set_standard_bytes_param(ObIAllocator &allocator,
                                              const uint32_t type,
                                              const ObCharsetType charset,
                                              const ObCollationType cs_type,
                                              const ObString &str, ObObj &param,
                                              const bool is_complex_element) {
  int ret = OB_SUCCESS;
  ObString dst;
  ObCollationType cur_cs_type = ObCharset::get_default_collation(charset);
  if (str.length() > OB_MAX_LONGTEXT_LENGTH) {
    ret = OB_ERR_INVALID_INPUT_ARGUMENT;
    LOG_WARN("typed execute parameter is over size", K(ret), K(str.length()));
  } else {
    if (MYSQL_TYPE_STRING == type || MYSQL_TYPE_VARCHAR == type ||
        MYSQL_TYPE_VAR_STRING == type || MYSQL_TYPE_ORA_CLOB == type ||
        MYSQL_TYPE_JSON == type || MYSQL_TYPE_GEOMETRY == type) {
      if (MYSQL_TYPE_ORA_CLOB == type) {
        ObLobLocatorV2 lob(str);
        if (!lob.is_valid()) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("got invalid typed ps lob param", K(ret), K(lob), K(type));
        }
      }
      if (OB_SUCC(ret) && MYSQL_TYPE_ORA_CLOB != type) {
        OZ(copy_or_convert_str(allocator, cur_cs_type, cs_type, str, dst));
      } else if (OB_SUCC(ret) &&
                 OB_FAIL(ob_write_string(allocator, str, dst))) {
        LOG_WARN("failed to copy typed ps clob", K(ret));
      }
    } else if (MYSQL_TYPE_OB_RAW == type || MYSQL_TYPE_TINY_BLOB == type ||
               MYSQL_TYPE_MEDIUM_BLOB == type || MYSQL_TYPE_LONG_BLOB == type ||
               MYSQL_TYPE_BLOB == type || MYSQL_TYPE_NEWDECIMAL == type ||
               MYSQL_TYPE_ORA_BLOB == type) {
      if (OB_FAIL(ob_write_string(allocator, str, dst))) {
        LOG_WARN("failed to copy typed ps bytes", K(ret), K(type));
      }
    } else {
      ret = OB_ERR_ILLEGAL_TYPE;
      LOG_WARN("unsupported typed ps byte parameter", K(ret), K(type));
    }
  }

  if (OB_SUCC(ret)) {
    if (MYSQL_TYPE_NEWDECIMAL == type) {
      number::ObNumber number;
      if (OB_FAIL(number.from(str.ptr(), str.length(), allocator))) {
        LOG_WARN("parse typed decimal parameter failed", K(ret), K(str));
      } else {
        param.set_number(number);
      }
    } else if (MYSQL_TYPE_ORA_BLOB == type || MYSQL_TYPE_ORA_CLOB == type) {
      param.set_collation_type(MYSQL_TYPE_ORA_BLOB == type ? CS_TYPE_BINARY
                                                           : cs_type);
      param.set_lob_value(ObLongTextType, dst.ptr(), dst.length());
      param.set_has_lob_header();
    } else if (MYSQL_TYPE_TINY_BLOB == type || MYSQL_TYPE_MEDIUM_BLOB == type ||
               MYSQL_TYPE_BLOB == type || MYSQL_TYPE_LONG_BLOB == type ||
               MYSQL_TYPE_JSON == type || MYSQL_TYPE_GEOMETRY == type) {
      param.set_collation_type(cs_type);
      if (MYSQL_TYPE_TINY_BLOB == type) {
        param.set_lob_value(ObTinyTextType, dst.ptr(), dst.length());
      } else if (MYSQL_TYPE_MEDIUM_BLOB == type) {
        param.set_lob_value(ObMediumTextType, dst.ptr(), dst.length());
      } else if (MYSQL_TYPE_BLOB == type) {
        param.set_lob_value(ObTextType, dst.ptr(), dst.length());
      } else if (MYSQL_TYPE_LONG_BLOB == type) {
        param.set_lob_value(ObLongTextType, dst.ptr(), dst.length());
      } else if (MYSQL_TYPE_JSON == type) {
        param.set_json_value(ObJsonType, dst.ptr(), dst.length());
      } else {
        param.set_geometry_value(ObGeometryType, dst.ptr(), dst.length());
      }
      if (param.is_lob_storage() && dst.length() > 0 &&
          OB_FAIL(ObTextStringResult::ob_convert_obj_temporay_lob(param,
                                                                  allocator))) {
        LOG_WARN("convert typed ps temporary lob failed", K(ret));
      }
    } else if (MYSQL_TYPE_STRING == type || MYSQL_TYPE_VARCHAR == type ||
               MYSQL_TYPE_VAR_STRING == type) {
      param.set_collation_type(cs_type);
      if (is_complex_element && dst.empty()) {
        param.set_null();
      } else if (is_complex_element && MYSQL_TYPE_STRING == type) {
        param.set_char(dst);
      } else {
        param.set_varchar(dst);
      }
    } else if (MYSQL_TYPE_OB_RAW == type) {
      param.set_raw_value(dst.ptr(), dst.length());
    }
  }
  return ret;
}

int ObMPStmtExecute::materialize_standard_long_data(
    ObIAllocator &allocator, ObSQLSessionInfo &session,
    const EMySQLFieldType type, const ObCharsetType charset,
    const ObCollationType cs_type, const int64_t param_id, ObObjParam &param) {
  int ret = OB_SUCCESS;
  bool is_supported_type = false;
  ObPieceCache *piece_cache = session.get_piece_cache(false);
  ObPiece *piece = NULL;
  ObSqlString payload;

  switch (type) {
  case MYSQL_TYPE_VARCHAR:
  case MYSQL_TYPE_JSON:
  case MYSQL_TYPE_NEWDECIMAL:
  case MYSQL_TYPE_TINY_BLOB:
  case MYSQL_TYPE_MEDIUM_BLOB:
  case MYSQL_TYPE_LONG_BLOB:
  case MYSQL_TYPE_BLOB:
  case MYSQL_TYPE_VAR_STRING:
  case MYSQL_TYPE_STRING:
  case MYSQL_TYPE_GEOMETRY:
    is_supported_type = true;
    break;
  default:
    break;
  }

  if (param_id < 0 || param_id > UINT16_MAX) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid standard long-data parameter id", K(ret), K(param_id));
  } else if (OB_ISNULL(piece_cache)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("piece cache is null for typed long-data parameter", K(ret),
             K(stmt_id_), K(param_id));
  } else if (OB_FAIL(piece_cache->get_piece(static_cast<int32_t>(stmt_id_),
                                            static_cast<uint16_t>(param_id),
                                            piece))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get typed long-data piece failed", K(ret), K(stmt_id_),
             K(param_id));
  } else if (OB_ISNULL(piece)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("typed long-data piece disappeared", K(ret), K(stmt_id_),
             K(param_id));
  } else if (!is_supported_type) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unsupported standard long-data parameter type", K(ret), K(type),
             K(stmt_id_), K(param_id));
  } else if (OB_ISNULL(piece->get_allocator())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("typed long-data piece allocator is null", K(ret), K(stmt_id_),
             K(param_id));
  } else if (OB_SUCCESS != piece->get_error_ret()) {
    ret = piece->get_error_ret();
    LOG_WARN("send long data has stored error", K(ret), K(stmt_id_),
             K(param_id));
  } else if (OB_FAIL(piece_cache->collect_piece_payload(
                 *piece, OB_MAX_LONGTEXT_LENGTH, payload))) {
    LOG_WARN("collect typed long-data payload failed", K(ret), K(stmt_id_),
             K(param_id));
  } else if (OB_FAIL(set_standard_bytes_param(allocator, type, charset, cs_type,
                                              payload.string(), param))) {
    LOG_WARN("materialize typed long-data parameter failed", K(ret), K(type),
             K(stmt_id_), K(param_id), "payload_length", payload.length());
  }
  return ret;
}

int ObMPStmtExecute::request_standard_params(ObSQLSessionInfo *session,
                                             ObPsSessionInfo &ps_session_info,
                                             const char *tail,
                                             ObIAllocator &alloc,
                                             bool &handled) {
  int ret = OB_SUCCESS;
  handled = false;
  const int64_t param_count = ps_session_info.get_param_count();
  const int64_t tail_len = analysis_checker_.remain_len();
  nio_mysql_execute_param_meta *cached = NULL;
  nio_mysql_execute_param *parsed = NULL;
  uint8_t *long_data = NULL;
  nio_mysql_execute_parse_result parse_result = {};
  const ParamTypeArray &cached_types = ps_session_info.get_param_types();
  const ParamTypeFlagArray &cached_flags =
      ps_session_info.get_param_type_flags();

  static_assert(sizeof(nio_mysql_execute_param_meta) == 4,
                "execute parameter meta ABI mismatch");
  static_assert(sizeof(nio_mysql_execute_param) == 40,
                "execute parameter descriptor ABI mismatch");
  static_assert(sizeof(nio_mysql_execute_parse_result) == 8,
                "execute parse result ABI mismatch");

  if (OB_ISNULL(session) || param_count < 0 || tail_len < 0 ||
      (param_count > 0 && OB_ISNULL(tail))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid standard execute parser input", K(ret), K(param_count),
             K(tail_len), KP(tail));
  } else if (param_count > INT64_MAX / static_cast<int64_t>(sizeof(*parsed))) {
    ret = OB_SIZE_OVERFLOW;
  } else if (param_count > 0 &&
             (OB_ISNULL(parsed = static_cast<nio_mysql_execute_param *>(
                            alloc.alloc(param_count * sizeof(*parsed)))) ||
              OB_ISNULL(cached = static_cast<nio_mysql_execute_param_meta *>(
                            alloc.alloc(param_count * sizeof(*cached)))) ||
              OB_ISNULL(long_data = static_cast<uint8_t *>(
                            alloc.alloc(param_count))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate standard execute descriptors failed", K(ret),
             K(param_count));
  }

  for (int64_t i = 0; OB_SUCC(ret) && i < param_count; ++i) {
    cached[i].mysql_type = i < cached_types.count()
                               ? static_cast<uint16_t>(cached_types.at(i))
                               : static_cast<uint16_t>(MYSQL_TYPE_NOT_DEFINED);
    cached[i].type_flags = i < cached_flags.count() ? cached_flags.at(i) : 0;
    cached[i].reserved = 0;
    long_data[i] = 0;
    ObPieceCache *piece_cache = session->get_piece_cache();
    ObPiece *piece = NULL;
    if (OB_NOT_NULL(piece_cache) &&
        OB_FAIL(piece_cache->get_piece(static_cast<int32_t>(stmt_id_),
                                       static_cast<uint16_t>(i), piece))) {
      LOG_WARN("get long-data state for Rust execute parser failed", K(ret),
               K(stmt_id_), K(i));
    } else {
      long_data[i] = OB_NOT_NULL(piece) ? 1 : 0;
    }
  }

  int parse_ret = NIO_MYSQL_EXECUTE_PARSE_INVALID_ARGUMENT;
  if (OB_SUCC(ret)) {
    const int64_t cached_count = cached_types.count() == param_count &&
                                         cached_flags.count() == param_count
                                     ? param_count
                                     : 0;
    parse_ret = nio_parse_mysql_execute_params(
        tail, tail_len, param_count, cached, cached_count, long_data,
        param_count, parsed, param_count, &parse_result);
    if (NIO_MYSQL_EXECUTE_PARSE_UNSUPPORTED == parse_ret) {
      handled = false;
    } else {
      handled = true;
      if (NIO_MYSQL_EXECUTE_PARSE_MALFORMED == parse_ret) {
        ret = OB_ERR_MALFORMED_PS_PACKET;
        LOG_USER_ERROR(OB_ERR_MALFORMED_PS_PACKET);
      } else if (NIO_MYSQL_EXECUTE_PARSE_CAPACITY == parse_ret) {
        ret = OB_SIZE_OVERFLOW;
      } else if (NIO_MYSQL_EXECUTE_PARSE_OK != parse_ret) {
        ret = OB_ERR_UNEXPECTED;
      }
      if (OB_FAIL(ret)) {
        LOG_WARN("Rust standard execute parser failed", K(ret), K(parse_ret),
                 K(param_count), K(tail_len));
      }
    }
  }

  ObCharsetType charset = CHARSET_INVALID;
  ObCollationType cs_conn = CS_TYPE_INVALID;
  if (OB_SUCC(ret) && handled &&
      (OB_FAIL(session->get_character_set_connection(charset)) ||
       OB_FAIL(session->get_collation_connection(cs_conn)))) {
    LOG_WARN("get session charset for typed execute parameters failed", K(ret));
  } else if (OB_SUCC(ret) && handled &&
             OB_FAIL(params_->prepare_allocate(param_count))) {
    LOG_WARN("allocate typed execute parameter store failed", K(ret),
             K(param_count));
  }

  ParamTypeArray new_types;
  ParamTypeFlagArray new_flags;
  for (int64_t i = 0; OB_SUCC(ret) && handled && i < param_count; ++i) {
    const nio_mysql_execute_param &desc = parsed[i];
    const EMySQLFieldType mysql_type =
        static_cast<EMySQLFieldType>(desc.mysql_type);
    const bool is_unsigned =
        0 != (desc.flags & NIO_MYSQL_EXECUTE_PARAM_UNSIGNED);
    ObObjParam &param = params_->at(i);
    ObObjType ob_type = ObMaxType;
    param.reset();
    if (OB_FAIL(ObSMUtils::get_ob_type(ob_type, mysql_type, is_unsigned))) {
      LOG_WARN("map typed execute parameter type failed", K(ret), K(i),
               K(mysql_type), K(is_unsigned));
    } else {
      param.set_type(ob_type);
    }

    if (OB_FAIL(ret)) {
    } else if (NIO_MYSQL_EXECUTE_VALUE_NULL == desc.kind) {
      param.set_null();
      if (ob_is_accuracy_length_valid_tc(ob_type)) {
        param.set_collation_type(cs_conn);
        param.set_collation_level(CS_LEVEL_COERCIBLE);
      }
    } else if (NIO_MYSQL_EXECUTE_VALUE_LONG_DATA == desc.kind) {
      if (OB_FAIL(materialize_standard_long_data(alloc, *session, mysql_type,
                                                 charset, cs_conn, i, param))) {
        LOG_WARN("materialize long-data execute parameter failed", K(ret), K(i),
                 K(mysql_type));
      }
    } else if (NIO_MYSQL_EXECUTE_VALUE_I64 == desc.kind ||
               NIO_MYSQL_EXECUTE_VALUE_U64 == desc.kind) {
      const int64_t signed_value = static_cast<int64_t>(desc.value);
      switch (mysql_type) {
      case MYSQL_TYPE_TINY:
        is_unsigned ? param.set_utinyint(static_cast<uint8_t>(desc.value))
                    : param.set_tinyint(static_cast<int8_t>(signed_value));
        break;
      case MYSQL_TYPE_SHORT:
        is_unsigned ? param.set_usmallint(static_cast<uint16_t>(desc.value))
                    : param.set_smallint(static_cast<int16_t>(signed_value));
        break;
      case MYSQL_TYPE_INT24:
        is_unsigned ? param.set_umediumint(static_cast<uint32_t>(desc.value))
                    : param.set_mediumint(static_cast<int32_t>(signed_value));
        break;
      case MYSQL_TYPE_LONG:
        is_unsigned ? param.set_uint32(static_cast<uint32_t>(desc.value))
                    : param.set_int32(static_cast<int32_t>(signed_value));
        break;
      case MYSQL_TYPE_LONGLONG:
        is_unsigned ? param.set_uint(ObUInt64Type, desc.value)
                    : param.set_int(signed_value);
        break;
      default:
        ret = OB_ERR_ILLEGAL_TYPE;
        LOG_WARN("integer descriptor has non-integer type", K(ret), K(i),
                 K(mysql_type));
        break;
      }
    } else if (NIO_MYSQL_EXECUTE_VALUE_F32_BITS == desc.kind) {
      const uint32_t bits = static_cast<uint32_t>(desc.value);
      float value = 0;
      MEMCPY(&value, &bits, sizeof(value));
      param.set_float(value);
    } else if (NIO_MYSQL_EXECUTE_VALUE_F64_BITS == desc.kind) {
      double value = 0;
      MEMCPY(&value, &desc.value, sizeof(value));
      param.set_double(value);
    } else if (NIO_MYSQL_EXECUTE_VALUE_YEAR == desc.kind) {
      uint8_t year = 0;
      if (OB_FAIL(ObTimeConverter::int_to_year(static_cast<int64_t>(desc.value),
                                               year))) {
        LOG_WARN("convert typed execute year failed", K(ret), K(i),
                 K(desc.value));
      } else {
        param.set_year(year);
      }
    } else if (NIO_MYSQL_EXECUTE_VALUE_BYTES == desc.kind) {
      if (desc.value_off < 0 || desc.value_len < 0 ||
          desc.value_off > tail_len ||
          desc.value_len > tail_len - desc.value_off) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("Rust execute byte range escaped tail", K(ret), K(i),
                  K(desc.value_off), K(desc.value_len), K(tail_len));
      } else {
        const ObString bytes(desc.value_len, tail + desc.value_off);
        if (OB_FAIL(set_standard_bytes_param(alloc, desc.mysql_type, charset,
                                             cs_conn, bytes, param))) {
          LOG_WARN("apply typed execute bytes failed", K(ret), K(i),
                   K(mysql_type));
        }
      }
    } else if (NIO_MYSQL_EXECUTE_VALUE_DATE == desc.kind ||
               NIO_MYSQL_EXECUTE_VALUE_DATETIME == desc.kind ||
               NIO_MYSQL_EXECUTE_VALUE_TIMESTAMP == desc.kind) {
      if (OB_FAIL(set_standard_timestamp_param(
              mysql_type, desc.year, desc.month, desc.day, desc.hour,
              desc.minute, desc.second, desc.microseconds, 0 == desc.value_len,
              session->get_timezone_info(), param))) {
        LOG_WARN("apply typed execute temporal failed", K(ret), K(i),
                 K(mysql_type));
      }
    } else if (NIO_MYSQL_EXECUTE_VALUE_TIME == desc.kind) {
      if (OB_FAIL(set_standard_time_param(
              desc.days, desc.hour, desc.minute, desc.second, desc.microseconds,
              0 != (desc.flags & NIO_MYSQL_EXECUTE_PARAM_NEGATIVE),
              0 == desc.value_len, param))) {
        LOG_WARN("apply typed execute time failed", K(ret), K(i));
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("unknown Rust execute parameter descriptor", K(ret), K(i),
                K(desc.kind));
    }

    if (OB_SUCC(ret)) {
      if (NIO_MYSQL_EXECUTE_VALUE_NULL != desc.kind) {
        // Preserve the legacy binary-protocol parameter coercibility used by
        // expression type deduction and string comparison.
        param.set_collation_level(CS_LEVEL_COERCIBLE);
      }
      param.set_param_meta();
      if (NIO_MYSQL_EXECUTE_VALUE_NULL != desc.kind) {
        param.set_length(param.get_val_len());
      }
      if (1 == parse_result.new_params_bound_flag) {
        OZ(new_types.push_back(mysql_type));
        OZ(new_flags.push_back(desc.type_flags));
      }
    }
  }

  if (OB_SUCC(ret) && handled && 1 == parse_result.new_params_bound_flag) {
    ObPsSessionInfoParamsAssignment assignment(new_types, new_flags);
    if (OB_FAIL(session->update_ps_session_info_safety(stmt_id_, assignment))) {
      LOG_WARN("update typed ps parameter cache failed", K(ret), K(stmt_id_));
    } else if (OB_FAIL(assignment.ret_)) {
      LOG_WARN("assign typed ps parameter cache failed", K(ret), K(stmt_id_));
    }
  }
  return ret;
}

int ObMPStmtExecute::request_params(ObSQLSessionInfo *session,
                                    const char* &pos,
                                    uint32_t ps_stmt_checksum,
                                    ObIAllocator &alloc)
{
  int ret = OB_SUCCESS;
  ObPsSessionInfo *ps_session_info = NULL;
  ObSQLSessionInfo::LockGuard lock_guard(session->get_query_lock());
  ObCharsetType charset = CHARSET_INVALID;
  ObCollationType cs_conn = CS_TYPE_INVALID;
  ObCollationType cs_server = CS_TYPE_INVALID;
  share::schema::ObSchemaGetterGuard schema_guard;
  

  if (OB_FAIL(gctx_.schema_service_->get_runtime_schema_guard(schema_guard))) {
    LOG_WARN("get schema guard failed", K(ret));
  } else if (OB_FAIL(session->get_character_set_connection(charset))) {
    LOG_WARN("get charset for client failed", K(ret));
  } else if (OB_FAIL(session->get_collation_connection(cs_conn))) {
    LOG_WARN("get charset for client failed", K(ret));
  } else if (OB_FAIL(session->get_collation_server(cs_server))) {
    LOG_WARN("get charset for client failed", K(ret));
  } else if (OB_FAIL(session->get_ps_session_info(stmt_id_, ps_session_info))) {
    LOG_WARN("get_ps_session_info failed", K(ret), K_(stmt_id));
  } else if (OB_ISNULL(ps_session_info)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("ps_session_info is null", K(ret));
  } else if (DEFAULT_ITERATION_COUNT == ps_stmt_checksum) {
    // do nothing
    // New protocol is not handled here
  } else if (ps_stmt_checksum != ps_session_info->get_ps_stmt_checksum()) {
    ret = OB_ERR_PREPARE_STMT_CHECKSUM;
    LOG_ERROR("ps stmt checksum fail", K(ret), "session_id", session->get_server_sid(),
                                        K(ps_stmt_checksum), K(*ps_session_info));
    LOG_DBA_ERROR_V2(OB_SERVER_PS_STMT_CHECKSUM_MISMATCH, ret,
                     "ps stmt checksum fail. ",
                     "the ps stmt checksum is ", ps_stmt_checksum,
                     ", but current session stmt checksum is ", ps_session_info->get_ps_stmt_checksum(),
                     ". current session id is ", session->get_server_sid(), ". ");
  }
  if (OB_SUCC(ret)) {
    LOG_TRACE("ps session info",
              K(ret), "session_id", session->get_server_sid(), K(*ps_session_info));
    share::schema::ObSchemaGetterGuard *old_guard = ctx_.schema_guard_;
    ObSQLSessionInfo *old_sess_info = ctx_.session_info_;
    ctx_.schema_guard_ = &schema_guard;
    ctx_.session_info_ = session;
    const int64_t input_param_num = ps_session_info->get_param_count();
    stmt_type_ = ps_session_info->get_stmt_type();
    int8_t new_param_bound_flag = 0;
    if (is_pl_stmt(stmt_type_)) {
      // pl not support save exception
      is_save_exception_ = 0;
    }
    params_num_ = input_param_num;
    bool rust_handled = false;
    if (OB_SUCC(ret) && !is_arraybinding_ && !is_pl_stmt(stmt_type_)) {
      if (OB_FAIL(request_standard_params(session, *ps_session_info, pos, alloc,
                                          rust_handled))) {
        LOG_WARN("parse standard execute parameters in Rust failed", K(ret),
                 K(stmt_id_), K(input_param_num));
      }
    }
    if (OB_SUCC(ret) && !rust_handled && params_num_ > 0) {
      ParamTypeArray &param_types = ps_session_info->get_param_types();
      ParamTypeFlagArray &param_type_flags =
          ps_session_info->get_param_type_flags();
      ParamTypeInfoArray param_type_infos;
      ParamCastArray param_cast_infos;
      // Step1: Handle null value bitmap
      const char *bitmap = pos;
      int64_t bitmap_types = (params_num_ + 7) / 8;
      PS_DEFENSE_CHECK(bitmap_types + 1)  // null value bitmap + new param bound flag
      {
        pos += bitmap_types;
        // Step2: Get the new_param_bound_flag field
        ObMySQLUtil::get_int1(pos, new_param_bound_flag);
        if (new_param_bound_flag == 1) {
          // reset param_types
          ObPsSessionInfoParamsCleaner cleaner;
          if (OB_FAIL(session->update_ps_session_info_safety(stmt_id_, cleaner))) {
            LOG_WARN("failed to reset param_types", K(ret), K(stmt_id_));
          } else if (OB_FAIL(cleaner.ret_)) {
            LOG_WARN("failed to reset param_types", K(ret), K(stmt_id_));
          }
        }
      }
      if (OB_FAIL(ret)) {
        // do nothing
      } else if (OB_FAIL(param_type_infos.prepare_allocate(input_param_num))) {
        LOG_WARN("array prepare allocate failed", K(ret), K(input_param_num));
      } else if (OB_FAIL(params_->prepare_allocate(input_param_num))) {
        LOG_WARN("array prepare allocate failed", K(ret));
      } else if (OB_FAIL(param_cast_infos.prepare_allocate(input_param_num))) {
        LOG_WARN("array prepare allocate failed", K(ret));
      } else if (is_arraybinding_) {
        CK (OB_NOT_NULL(arraybinding_params_));
        OZ (arraybinding_params_->prepare_allocate(input_param_num));
      }

      for (int i = 0; OB_SUCC(ret) && i < input_param_num; ++i) {
        param_cast_infos.at(i) = false;
      }

      // Step3: Get type information
      if (OB_SUCC(ret)) {
        if (1 == new_param_bound_flag) {
          ParamTypeArray tmp_param_types;
          ParamTypeFlagArray tmp_param_type_flags;
          ObPsSessionInfoParamsAssignment assignment(tmp_param_types,
                                                     tmp_param_type_flags);
          if (OB_FAIL(parse_request_type(
                  pos, input_param_num, new_param_bound_flag, cs_conn,
                  tmp_param_types, tmp_param_type_flags, param_type_infos))) {
            LOG_WARN("fail to parse input params type from packet", K(ret));
          } else if (OB_FAIL(session->update_ps_session_info_safety(
                         stmt_id_, assignment))) {
            LOG_WARN("fail to update params type of PsSessionInfo", K(ret));
          } else if (OB_FAIL(assignment.ret_)) {
            LOG_WARN("fail to update params type of PsSessionInfo", K(ret));
          }
        } else {
          if (OB_FAIL(parse_request_type(
                  pos, input_param_num, new_param_bound_flag, cs_conn,
                  param_types, param_type_flags, param_type_infos))) {
            LOG_WARN("fail to parse input params type", K(ret));
          }
        }
        if (OB_SUCC(ret) && is_contain_complex_element(param_types)) {
          analysis_checker_.need_check_ = false;
        }
      }
      if (OB_SUCC(ret) && is_arraybinding_) {
        OZ (check_param_type_for_arraybinding(param_type_infos));
      }
      if (OB_SUCC(ret)
          && (stmt::T_CALL_PROCEDURE == ps_session_info->get_stmt_type()
              || stmt::T_ANONYMOUS_BLOCK == ps_session_info->get_stmt_type())) {
        ctx_.is_execute_call_stmt_ = true;
      }

      // Step5: decode value
      for (int64_t i = 0; OB_SUCC(ret) && i < input_param_num; ++i) {
        ObObjParam &param = is_arraybinding_ ? arraybinding_params_->at(i) : params_->at(i);
        param.reset();
        if (OB_SUCC(ret) && OB_FAIL(parse_request_param_value(alloc,
                                                              session,
                                                              pos,
                                                              i,
                                                              param_types.at(i),
                                                              param_type_infos.at(i),
                                                              param,
                                                              bitmap))) {
          LOG_WARN("fail to parse request param values", K(ret), K(i));
        } else {
          LOG_DEBUG("after parser param", K(param), K(i));
        }
        if (OB_SUCC(ret) && is_arraybinding_) {
          OZ (check_param_value_for_arraybinding(param));
        }
      }

    }
    ctx_.schema_guard_ = old_guard;
    ctx_.session_info_ = old_sess_info;
  }
  return ret;
}

int ObMPStmtExecute::decode_type_info(const char*& buf, TypeInfo &type_info)
{
  int ret = OB_SUCCESS;
  PS_DEFENSE_CHECK(1) // check first byte
  {
    uint64_t length = 0;
    if (OB_FAIL(ObMySQLUtil::get_length(buf, length))) {
      LOG_WARN("failed to get length", K(ret));
    } else {
      PS_DEFENSE_CHECK(length)
      {
        type_info.relation_name_.assign_ptr(buf, static_cast<ObString::obstr_size_t>(length));
        buf += length;
      }
    }
  }
  PS_DEFENSE_CHECK(1)
  {
    uint64_t length = 0;
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObMySQLUtil::get_length(buf, length))) {
      LOG_WARN("failed to get length", K(ret));
    } else {
      PS_DEFENSE_CHECK(length)
      {
        type_info.type_name_.assign_ptr(buf, static_cast<ObString::obstr_size_t>(length));
        buf += length;
      }
    }
  }
  PS_DEFENSE_CHECK(1)
  {
    uint64_t version = 0;
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObMySQLUtil::get_length(buf, version))) {
      LOG_WARN("failed to get version", K(ret));
    }
  }
  return ret;
}

int ObMPStmtExecute::set_session_active(ObSQLSessionInfo &session) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(session.set_session_state(QUERY_ACTIVE))) {
    LOG_WARN("fail to set session state", K(ret));
  } else {
    session.set_query_start_time(get_receive_timestamp());
    session.set_mysql_cmd(obmysql::COM_STMT_EXECUTE);
    session.update_last_active_time();
    session.set_is_request_end(false);
  }
  return ret;
}

int ObMPStmtExecute::execute_response(ObSQLSessionInfo &session,
                                      ObMySQLResultSet &result,
                                      bool &need_response_error,
                                      bool &is_diagnostics_stmt,
                                      int64_t &execution_id,
                                      const bool force_sync_resp,
                                      bool &async_resp_used,
                                      ObPsStmtId &inner_stmt_id)
{
  int ret = OB_SUCCESS;
  inner_stmt_id = OB_INVALID_ID;
  ObIAllocator &alloc = CURRENT_CONTEXT->get_arena_allocator();
  ObPsCache *ps_cache = OB_ISNULL(get_observer_sql_engine())
      ? nullptr : &get_observer_sql_engine()->get_ps_cache();
  if (OB_ISNULL(ps_cache)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ps : ps cache is null.", K(ret), K(stmt_id_));
  } else if (OB_FAIL(session.get_inner_ps_stmt_id(stmt_id_, inner_stmt_id))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ps : get inner stmt id fail.", K(ret), K(stmt_id_));
  } else {
    ObPsStmtInfoGuard guard;
    ObPsStmtInfo *ps_info = NULL;
    if (OB_FAIL(ps_cache->get_stmt_info_guard(inner_stmt_id, guard))) {
      LOG_WARN("get stmt info guard failed", K(ret), K(stmt_id_), K(inner_stmt_id));
    } else if (OB_ISNULL(ps_info = guard.get_stmt_info())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get stmt info is null", K(ret));
    } else {
      if (is_execute_ps_cursor() && stmt::T_SELECT != ps_info->get_stmt_type()) {
        set_ps_cursor_type(ObNormalType);
      }
      ctx_.cur_sql_ = ps_info->get_ps_sql();
    }
  }
  if OB_FAIL(ret) {
    // do nothing
  } else if (is_execute_ps_cursor()) {
    ObPLServerCursorInfo *cursor = NULL;
    bool use_stream = false;
    // 1.create cursor
    if (OB_NOT_NULL(session.get_cursor(stmt_id_))) {
      if (OB_FAIL(session.close_cursor(stmt_id_))) {
        LOG_WARN("fail to close result set", K(ret), K(stmt_id_), K(session.get_server_sid()));
      }
    }
    OZ (session.make_server_cursor(cursor, stmt_id_));
    CK (OB_NOT_NULL(cursor));
    OX (cursor->set_stmt_type(stmt::T_SELECT));
    OX (cursor->set_ps_sql(ctx_.cur_sql_));
    OZ (session.ps_use_stream_result_set(use_stream));
    if (use_stream) {
      OX (cursor->set_streaming());
    }
    OZ (cursor->prepare_entity(session));
    CK (OB_NOT_NULL(cursor->get_allocator()));
    OZ (cursor->init_params(params_->count()));
    OZ (cursor->get_exec_params().assign(*params_));
    OZ (::oceanbase::observer::get_observer_sql_engine()->init_result_set(ctx_, result));
    {
      exec_start_timestamp_ = ObTimeUtility::current_time();
      session.reset_plsql_exec_time();
    }
    if (OB_SUCC(ret)) {
      ObPLExecCtx pl_ctx(cursor->get_allocator(), &result.get_exec_context(), NULL/*params*/,
                        NULL/*result*/, &ret, NULL/*func*/, true);
      int64_t max_result_rows = INT64_MAX;
      if (OB_FAIL(ObSPIService::open_server_cursor(
                     &pl_ctx, *cursor, max_result_rows))) {
        LOG_WARN("open cursor fail. ", K(ret), K(stmt_id_));
        if (!THIS_WORKER.need_retry()) {
          int cli_ret = OB_SUCCESS;
          retry_ctrl_.test_and_save_retry_state(
            gctx_, ctx_, result, ret, cli_ret, is_arraybinding_ /*ararybinding only local retry*/);
          LOG_WARN("run stmt_query failed, check if need retry",
                   K(ret), K(cli_ret), K(retry_ctrl_.need_retry()), K_(stmt_id));
          ret = cli_ret;
        }
      }
    }
    /*
    * In the PS mode exec-cursor protocol,
    * do not return the result_set, only return the packet header information
    * and set the OB_SERVER_STATUS_CURSOR_EXISTS status in the EOF packet
    * to prompt the driver to send the fetch protocol
    */
    OZ (response_query_header(session, *cursor));
    if (OB_SUCCESS != ret && OB_NOT_NULL(cursor)) {
      int tmp_ret = ret;
      if (OB_FAIL(session.close_cursor(cursor->get_id()))) {
        LOG_WARN("close cursor failed.", K(ret), K(stmt_id_));
      }
      ret = tmp_ret;
    }
  } else if (OB_FAIL(::oceanbase::observer::get_observer_sql_engine()->stmt_execute(stmt_id_,
                                                      stmt_type_,
                                                      *params_,
                                                      ctx_, result,
                                                      false /* is_inner_sql */))) {
    exec_start_timestamp_ = ObTimeUtility::current_time();
    if (!THIS_WORKER.need_retry()) {
      int cli_ret = OB_SUCCESS;
      retry_ctrl_.test_and_save_retry_state(
        gctx_, ctx_, result, ret, cli_ret, is_arraybinding_ /*ararybinding only local retry*/);
      LOG_WARN("run stmt_query failed, check if need retry",
               K(ret), K(cli_ret), K(retry_ctrl_.need_retry()), K_(stmt_id));
      ret = cli_ret;
    }
  } else {
    //Monitoring item statistics start
    exec_start_timestamp_ = ObTimeUtility::current_time();
    result.get_exec_context().set_plan_start_time(exec_start_timestamp_);
    session.reset_plsql_exec_time();
    // All errors within this branch will be handled properly inside response_result
    // No need to handle the error response packet additionally

    need_response_error = false;
    is_diagnostics_stmt = ObStmt::is_diagnostic_stmt(result.get_literal_stmt_type());
    ctx_.is_show_trace_stmt_ = ObStmt::is_show_trace_stmt(result.get_literal_stmt_type());
    session.set_current_execution_id(execution_id);

    if (OB_FAIL(ret)) {
    } else if (is_arraybinding_) {
      if (OB_FAIL(after_do_process_for_arraybinding(result))) {
        LOG_WARN("failed to process arraybinding sql", K(ret));
      }
    } else if (OB_FAIL(response_result(result,
                                        session,
                                        force_sync_resp,
                                        async_resp_used))) {
      ObPhysicalPlanCtx *plan_ctx = result.get_exec_context().get_physical_plan_ctx();
      if (OB_ISNULL(plan_ctx)) {
        // ignore ret
        LOG_ERROR("execute query fail, and plan_ctx is NULL", K(ret));
      } else {
        LOG_WARN("execute query fail", K(ret), "timeout_timestamp",
                  plan_ctx->get_timeout_timestamp());
      }
    }
  }
  return ret;
}
int ObMPStmtExecute::do_process(ObSQLSessionInfo &session,
                                 ParamStore *param_store,
                                 const bool has_more_result,
                                 const bool force_sync_resp,
                                 bool &async_resp_used)
{
  int ret = OB_SUCCESS;
  ObAuditRecordData &audit_record = session.get_raw_audit_record();
  ObExecutingSqlStatRecord sqlstat_record;
  audit_record.try_cnt_++;
  bool is_diagnostics_stmt = false;
  ObPsStmtId inner_stmt_id = OB_INVALID_ID;
  bool need_response_error = is_arraybinding_ ? false : true;
  const bool enable_sqlstat = session.is_sqlstat_enabled();

  single_process_timestamp_ = ObTimeUtility::current_time();

  /* !!!
   * Note that req_timeinfo_guard must be placed before result
   * !!!
   */
  ObReqTimeGuard req_timeinfo_guard;
  SMART_VAR(ObMySQLResultSet, result, session, THIS_WORKER.get_sql_arena_allocator(),
            ::oceanbase::observer::get_observer_sql_engine()->get_plan_cache_access_service()) {

    int64_t execution_id = 0;
    {
      {
        audit_record.exec_record_.record_start();
      }

      if (enable_sqlstat) {
        sqlstat_record.record_sqlstat_start_value(
            ::oceanbase::observer::get_observer_sql_engine()->get_query_runtime_environment());
        sqlstat_record.set_is_in_retry(session.get_is_in_retry());
        session.sql_sess_record_sql_stat_start_value(sqlstat_record);
      }
      result.set_has_more_result(has_more_result);
      result.set_ps_protocol();
      ObSqlExecutorCtx *task_ctx = result.get_exec_context().get_sql_executor_ctx();
      if (OB_ISNULL(task_ctx)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("task executor ctx can not be NULL", K(task_ctx), K(ret));
      } else {
        task_ctx->schema_service_ = gctx_.schema_service_;
        task_ctx->set_query_begin_schema_version(retry_ctrl_.get_current_local_schema_version());
        ctx_.retry_times_ = retry_ctrl_.get_retry_times();
        session.reset_plsql_exec_time();
        session.reset_plsql_compile_time();
        if (OB_ISNULL(ctx_.schema_guard_)) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("newest schema is NULL", K(ret));
        } else if (OB_FAIL(result.init())) {
          LOG_WARN("result set init failed", K(ret));
        } else if (OB_ISNULL(::oceanbase::observer::get_observer_sql_engine()) || OB_ISNULL(param_store)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_ERROR("invalid sql engine", K(ret), K(gctx_), K(param_store));
        } else if (FALSE_IT(execution_id = ::oceanbase::observer::get_observer_sql_engine()->get_execution_id())) {
          // do nothing ...
        } else if (OB_FAIL(set_session_active(session))) {
          LOG_WARN("fail to set session active", K(ret));
        } else {
          ret = execute_response(session,
                                  result,
                                  need_response_error,
                                  is_diagnostics_stmt,
                                  execution_id,
                                  force_sync_resp,
                                  async_resp_used,
                                  inner_stmt_id);
          if ((OB_SUCC(ret) && is_diagnostics_stmt) || async_resp_used) {
            // if diagnostic stmt succeed, no need to clear warning buf.
            // or async resp is used, it will be cleared in callback thread.
            session.update_show_warnings_buf();
          } else {
            session.set_show_warnings_buf(ret);
          }
        }
      }
      //Monitoring item statistics end
      exec_end_timestamp_ = ObTimeUtility::current_time();

      // some statistics must be recorded for plan stat, even though sql audit disabled
      bool first_record = (1 == audit_record.try_cnt_);
      ObExecStatUtils::record_exec_timestamp(*this, first_record, audit_record.exec_timestamp_);
      audit_record.exec_timestamp_.update_stage_time();

      if (OB_FAIL(ret)
          && !async_resp_used
          && need_response_error
          && is_conn_valid()
          && !THIS_WORKER.need_retry()
          && !retry_ctrl_.need_retry()) {
        LOG_WARN("query failed", K(ret), K(retry_ctrl_.need_retry()), K_(stmt_id));
        // When need_retry=false, a response packet may have been sent to the client, or no packets may have been sent at all.
        // However, it can be determined: this request has errored, and is not yet complete. If it has not already been handed over to asynchronous EndTrans for finalization,
        // then it is necessary to reply with an error_packet below as a conclusion. Otherwise, no one will help send the error packet to the client afterwards,
        // May cause the client to hang waiting for a response.
        int err = send_error_packet(ret, NULL);
        if (OB_SUCCESS != err) {  // send error packet
          LOG_WARN("send error packet failed", K(ret), K(err));
        }
      }
    }

    {
      audit_record.exec_record_.record_end();
      audit_record.stmt_type_ = result.get_stmt_type();
      audit_record.update_event_stage_state();
    }
    if (enable_sqlstat) {
      sqlstat_record.record_sqlstat_end_value(
          ::oceanbase::observer::get_observer_sql_engine()->get_query_runtime_environment());
      sqlstat_record.set_rows_processed(result.get_affected_rows() + result.get_return_rows());
      sqlstat_record.set_partition_cnt(result.get_exec_context().get_das_ctx().get_related_tablet_cnt());
      sqlstat_record.set_is_plan_cache_hit(ctx_.plan_cache_hit_);
      ObString sql_id = ObString::make_string(ctx_.sql_id_);
      sqlstat_record.move_to_sqlstat_cache(result.get_session(),
                                                 get_observer_sql_engine()->get_plan_cache(),
                                                 get_observer_sql_engine()->get_plan_cache_access_service(),
                                                 ctx_.cur_sql_,
                                                 result.get_physical_plan());
    }

    audit_record.status_ =
      (0 == ret || OB_ITER_END == ret) ? 0 : (ret);

    //update v$sql statistics
    if (session.get_local_ob_enable_plan_cache()
        && !retry_ctrl_.need_retry()
        && !is_ps_cursor()) {
      // ps cursor do this in inner open
      ObIArray<ObTableRowCount> *table_row_count_list = NULL;
      ObPhysicalPlan *plan = result.get_physical_plan();
      ObPhysicalPlanCtx *plan_ctx = result.get_exec_context().get_physical_plan_ctx();
      if (OB_NOT_NULL(plan_ctx)) {
        table_row_count_list = &(plan_ctx->get_table_row_count_list());
        audit_record.table_scan_stat_ = plan_ctx->get_table_scan_stat();
      }
      if (NULL != plan) {
        if (!(ctx_.self_add_plan_) && ctx_.plan_cache_hit_) {
          plan->update_plan_stat(audit_record,
              false, // false mean not first update plan stat
              table_row_count_list);
          plan->update_cache_access_stat(audit_record.table_scan_stat_);
        } else if (ctx_.self_add_plan_ && !ctx_.plan_cache_hit_) {
          plan->update_plan_stat(audit_record,
              true,
              table_row_count_list);
          plan->update_cache_access_stat(audit_record.table_scan_stat_);
        } else if (ctx_.self_add_plan_ && ctx_.plan_cache_hit_) {
          // First execution of a plan generated during this request.
          plan->update_plan_stat(audit_record,
              true,
              table_row_count_list);
          plan->update_cache_access_stat(audit_record.table_scan_stat_);
        }
      }
    }

    // reset thread waring buffer in sync mode
    if (!async_resp_used) {
      clear_wb_content(session);
    }

    bool need_retry = (THIS_THWORKER.need_retry()
                       || RETRY_TYPE_NONE != retry_ctrl_.get_retry_type());
  }
  return ret;
}

// return false only if send packet fail.
int ObMPStmtExecute::response_result(
    ObMySQLResultSet &result,
    ObSQLSessionInfo &session,
    bool force_sync_resp,
    bool &async_resp_used)
{
  int ret = OB_SUCCESS;
  bool callback_armed = false;
  bool need_trans_cb  = result.need_end_trans_callback() && (!force_sync_resp);

  // NG_TRACE_EXT(exec_begin, ID(arg1), force_sync_resp, ID(end_trans_cb), need_trans_cb);

  if (OB_LIKELY(NULL != result.get_physical_plan())) {
    if (need_trans_cb) {
      ObAsyncPlanDriver drv(gctx_, ctx_, session, retry_ctrl_, packet_sender_);
      // NOTE: sql_end_cb must be initialized before drv.response_result()
      ObSqlEndTransCb &sql_end_cb = session.get_mysql_end_trans_cb();
      if (OB_FAIL(sql_end_cb.init(packet_sender_, &session,
                                    stmt_id_, params_num_))) {
        LOG_WARN("failed to init sql end callback", K(ret));
      } else if (FALSE_IT(callback_armed = true)) {
      } else if (OB_FAIL(drv.response_result(result))) {
        LOG_WARN("fail response async result", K(ret));
      }
      async_resp_used = result.is_async_end_trans_submitted();
    } else {
      int32_t iteration_count = OB_INVALID_COUNT;
      ObSyncPlanDriver drv(gctx_, ctx_, session, retry_ctrl_, packet_sender_,
                           iteration_count);
      ret = drv.response_result(result);
    }
  } else {
    if (need_trans_cb) {
      ObSqlEndTransCb &sql_end_cb = session.get_mysql_end_trans_cb();
      ObAsyncCmdDriver drv(gctx_, ctx_, session, retry_ctrl_, packet_sender_);
      if (OB_FAIL(sql_end_cb.init(packet_sender_, &session,
                                    stmt_id_, params_num_))) {
        LOG_WARN("failed to init sql end callback", K(ret));
      } else if (FALSE_IT(callback_armed = true)) {
      } else if (OB_FAIL(drv.response_result(result))) {
        LOG_WARN("fail response async result", K(ret));
      } else {
        LOG_DEBUG("use async cmd driver success!",
                  K(result.get_stmt_type()), K(session.get_local_autocommit()));
      }
      async_resp_used = result.is_async_end_trans_submitted();
    } else {
      ObSyncCmdDriver drv(gctx_, ctx_, session, retry_ctrl_, packet_sender_);
      session.set_pl_query_sender(&drv);
      session.set_ps_protocol(result.is_ps_protocol());
      if (OB_FAIL(drv.response_result(result))) {
        LOG_WARN("failed response sync result", K(ret));
      } else {
        LOG_DEBUG("use sync cmd driver success!",
                  K(result.get_stmt_type()), K(session.get_local_autocommit()));
      }
      session.set_pl_query_sender(NULL);
    }
  }
//  NG_TRACE(exec_end);
  if (callback_armed && !async_resp_used) {
    const int cancel_ret = cancel_unsubmitted_callback(session.get_mysql_end_trans_cb());
    if (OB_SUCCESS != cancel_ret) {
      LOG_ERROR("failed to cancel unsubmitted mysql end-trans callback", K(cancel_ret));
      ret = OB_SUCCESS == ret ? cancel_ret : ret;
    }
  }
  return ret;
}

OB_NOINLINE int ObMPStmtExecute::process_retry(ObSQLSessionInfo &session,
                                               ParamStore *param_store,
                                               bool has_more_result,
                                               bool force_sync_resp,
                                               bool &async_resp_used)
{
  int ret = OB_SUCCESS;
  //create a temporary memory context to process retry, avoid memory bloat caused by retries
  lib::ContextParam param;
  param.set_mem_attr(ObModIds::OB_SQL_EXECUTOR, ObCtxIds::DEFAULT_CTX_ID)
    .set_properties(lib::USE_TL_PAGE_OPTIONAL)
    .set_page_size(!lib::is_mini_mode() ? OB_MALLOC_BIG_BLOCK_SIZE
                                        : OB_MALLOC_MIDDLE_BLOCK_SIZE)
    .set_ablock_size(lib::INTACT_MIDDLE_AOBJECT_SIZE);
  CREATE_WITH_TEMP_CONTEXT(param) {
    ret = do_process(session,
                     param_store,
                     has_more_result,
                     force_sync_resp,
                     async_resp_used);
    ctx_.clear();
  }
  return ret;
}

int ObMPStmtExecute::do_process_single(ObSQLSessionInfo &session,
                                       ParamStore *param_store,
                                       bool has_more_result,
                                       bool force_sync_resp,
                                       bool &async_resp_used)
{
  int ret = OB_SUCCESS;
  ObReqTimeGuard req_timeinfo_guard;
  // Each execution of different SQL requires an update
  ctx_.self_add_plan_ = false;
  do {
    // Must always be set to OB_SUCCESS, otherwise it may cause a deadlock due to do_process() not being called
    ret = OB_SUCCESS;
    share::schema::ObSchemaGetterGuard schema_guard;
    int64_t database_schema_version = 0;
    retry_ctrl_.clear_state_before_each_retry(session.get_retry_info_for_update());
    OZ (gctx_.schema_service_->get_runtime_schema_guard(schema_guard));
    OZ (schema_guard.get_schema_version(database_schema_version));
    OX (ctx_.schema_guard_ = &schema_guard);
    OX (retry_ctrl_.set_current_local_schema_version(database_schema_version));

    if (OB_SUCC(ret) && !is_send_long_data()) {
      if (OB_LIKELY(session.get_is_in_retry())
            || is_arraybinding_) {
        ret = process_retry(session,
				                    param_store,
                            has_more_result,
                            force_sync_resp,
                            async_resp_used);
      } else {
        ret = do_process(session,
						             param_store,
                         has_more_result,
                         force_sync_resp,
                         async_resp_used);
        ctx_.clear();
      }
      session.set_session_in_retry(retry_ctrl_.need_retry());
    }
  } while (RETRY_TYPE_LOCAL == retry_ctrl_.get_retry_type());

  if (OB_SUCC(ret) && retry_ctrl_.get_retry_times() > 0) {
    // After successful retry, print the sql. Here it can only cover the local retry case, cannot cover the case of retrying by putting back into the queue.
    // If a retry is needed, ret will never be OB_SUCCESS, so there is no need to check retry_type here.
    LOG_TRACE("sql retry",
              K(ret), "retry_times", retry_ctrl_.get_retry_times(), "sql", ctx_.cur_sql_);
  }
  ctx_.plan_key_.reset();
  return ret;
}

int ObMPStmtExecute::process_execute_stmt(const ObMultiStmtItem &multi_stmt_item,
                                          ObSQLSessionInfo &session,
                                          bool has_more_result,
                                          bool force_sync_resp,
                                          bool &async_resp_used)
{
  int ret = OB_SUCCESS;
  bool need_response_error = true;
  // After executing setup_wb, all WARNINGS will be written to the WARNING BUFFER of the current session
  setup_wb(session);
  //============================ Note the lifecycle of these variables ================================
  ObSMConnection *conn = get_conn();
  if (OB_FAIL(init_process_var(ctx_, multi_stmt_item, session))) {
    LOG_WARN("init process var failed.", K(ret), K(multi_stmt_item));
  } else {
    //set session log_level.Must use ObThreadLogLevelUtils::clear() in pair
    ObThreadLogLevelUtils::init(session.get_log_id_level_map());
    // Clients may publish a newer schema version through @@last_schema_version;
    // observer refreshes when its local version is older.
    if (OB_FAIL(check_and_refresh_schema())) {
      LOG_WARN("failed to check_and_refresh_schema", K(ret));
    } else if (OB_FAIL(session.update_timezone_info())) {
      LOG_WARN("fail to update time zone info", K(ret));
    } else if (is_arraybinding_) {
      ObSEArray<ObSavedException, 4> exception_array;
      if (OB_UNLIKELY(arraybinding_size_ <= 0)) {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("arraybinding has no parameters", K(ret), K(arraybinding_size_));
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "oci arraybinding has no parameters");
      } else {
        need_response_error = false;
        ctx_.multi_stmt_item_.set_ps_mode(true);
        ctx_.multi_stmt_item_.set_ab_cnt(0);
        for (int64_t i = 0; OB_SUCC(ret) && i < arraybinding_size_; ++i) {
          set_curr_sql_idx(i);
          OZ (construct_execute_param_for_arraybinding(i));
          OZ (do_process_single(session, params_, has_more_result, force_sync_resp, async_resp_used));
          if (OB_FAIL(ret)) {
            if (is_save_exception_) {
              ret = save_exception_for_arraybinding(i, ret, exception_array);
              ret = OB_SUCCESS;
            }
            if (OB_FAIL(ret)) {
              // If there is still an error in the new ps protocol,
              // then send an err package,
              // indicating that the server has an error that is not expected by the customer
              need_response_error = true;
              break;
            }
          }
        }
      }
      // Release array memory to avoid memory leak
      
      OZ (response_result_for_arraybinding(session, exception_array));
    } else {
      need_response_error = false;
      if (OB_FAIL(do_process_single(session, params_, has_more_result, force_sync_resp, async_resp_used))) {
        LOG_WARN("fail to do process", K(ret), K(ctx_.cur_sql_));
      }
      
      // ret = OB_SUCC(bak_ret) ? ret : bak_ret;
    }
    ObThreadLogLevelUtils::clear();
    const int64_t debug_sync_timeout = GCONF.debug_sync_timeout;
    if (debug_sync_timeout > 0) {
      // ignore thread local debug sync actions to session actions failed
      int tmp_ret = OB_SUCCESS;
      tmp_ret = GDS.collect_result_actions(session.get_debug_sync_actions());
      if (OB_UNLIKELY(OB_SUCCESS != tmp_ret)) {
        LOG_WARN("set thread local debug sync actions to session actions failed", K(tmp_ret));
      }
    }
  }
  //For tracelog processing, it does not affect normal logic, error code does not need to be assigned to ret, clear WARNING BUFFER
  do_after_process(session, async_resp_used, ret);

  if (OB_FAIL(ret) && need_response_error && is_conn_valid()) {
    send_error_packet(ret, NULL);
  }

  return ret;
}


int ObMPStmtExecute::process()
{
  int ret = OB_SUCCESS;
  int flush_ret = OB_SUCCESS;
  trace::UUID ps_execute_span_id;
  ObSQLSessionInfo *sess = NULL;
  bool need_response_error = true;
  bool need_disconnect = true;
  bool async_resp_used = false; // Asynchronously reply to the client by the transaction commit thread
  int64_t query_timeout = 0;

  ObCurTraceId::TraceId *cur_trace_id = ObCurTraceId::get_trace_id();
  ObSMConnection *conn = get_conn();
  if (OB_ISNULL(req_) || OB_ISNULL(conn) || OB_ISNULL(cur_trace_id)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("null conn ptr", K_(stmt_id), K_(req), K(cur_trace_id), K(ret));
  } else if (OB_UNLIKELY(!conn->is_in_authed_phase())) {
    ret = OB_ERR_NO_PRIVILEGE;
    LOG_WARN("receive sql without session", K_(stmt_id), K(ret));
  } else if (OB_ISNULL(conn->runtime_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid runtime", K_(stmt_id), K(conn->runtime_), K(ret));
  } else if (OB_FAIL(get_session(sess))) {
    LOG_WARN("get session fail", K_(stmt_id), K(ret));
  } else if (OB_ISNULL(sess)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is NULL or invalid", K_(stmt_id), K(sess), K(ret));
  } else {
    ObSQLSessionInfo &session = *sess;
    int64_t database_schema_version = 0;
    THIS_WORKER.set_session(sess);
    ObSQLSessionInfo::LockGuard lock_guard(session.get_query_lock());
    SQL_INFO_GUARD(ctx_.cur_sql_, ObString(ctx_.sql_id_));
    session.set_current_trace_id(ObCurTraceId::get_trace_id());
    session.get_raw_audit_record().request_memory_used_ = 0;
    observer::ObProcessMallocCallback pmcb(0,
          session.get_raw_audit_record().request_memory_used_);
    lib::ObMallocCallbackGuard guard(pmcb);
    session.set_thread_id(GETTID());
    const ObMySQLRawPacket &pkt = reinterpret_cast<const ObMySQLRawPacket&>(req_->get_packet());
    int64_t packet_len = pkt.get_clen();
    if (OB_UNLIKELY(!session.is_valid())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("invalid session", K_(stmt_id), K(ret));
    } else if (OB_UNLIKELY(session.is_zombie())) {
      //session has been killed some moment ago
      ret = OB_ERR_SESSION_INTERRUPTED;
      LOG_WARN("session has been killed", K(session.get_session_state()), K_(stmt_id),
               K(session.get_server_sid()), K(ret));
    } else if (OB_FAIL(session.check_and_init_retry_info(*cur_trace_id, ctx_.cur_sql_))) {
      LOG_WARN("fail to check and init retry info", K(ret), K(*cur_trace_id), K(ctx_.cur_sql_));
    } else if (OB_FAIL(session.get_query_timeout(query_timeout))) {
      LOG_WARN("fail to get query timeout", K(ret));
    } else if (OB_FAIL(gctx_.schema_service_->get_published_schema_version(
                database_schema_version))) {
      LOG_WARN("fail to get published database schema version", K(ret));
    } else if (OB_UNLIKELY(packet_len > session.get_max_packet_size())) {
      //packet size check with session variable max_allowd_packet or net_buffer_length
      ret = OB_ERR_NET_PACKET_TOO_LARGE;
      LOG_WARN("packet too large than allowed for the session", K_(stmt_id), K(ret));
    } else if (OB_FAIL(session.gen_configs_in_pc_str())) {
      LOG_WARN("fail to generate configuration string that can influence execution plan", K(ret));
    } else if (is_arraybinding_ && OB_FAIL(check_precondition_for_arraybinding(session))) {
      LOG_WARN("precondition for arraybinding is not satisfied", K(ret));
    } else {
      THIS_WORKER.set_timeout_ts(get_receive_timestamp() + query_timeout);
      retry_ctrl_.set_current_global_schema_version(database_schema_version);
      session.set_pl_can_retry(true);
      session.set_enable_mysql_compatible_dates(
        session.get_enable_mysql_compatible_dates_from_config());

      need_response_error = false;
      need_disconnect = false;
      ret = process_execute_stmt(ObMultiStmtItem(false, 0, ObString()),
                                 session,
                                 false, // has_mode
                                 false, // force_sync_resp
                                 async_resp_used);
      // Print out the SQL statement before exiting, for easy problem location
      if (OB_FAIL(ret)) {
        if (OB_EAGAIN == ret) {
          // Retryable request is handled by the upper scheduler.
        } else if (is_conn_valid()) { // The SQL text may be request-owned after an async handoff.
          LOG_WARN("fail execute sql", "sql_id", ctx_.sql_id_, K_(stmt_id), K(ret));
        } else {
          LOG_WARN("fail execute sql", K(ret));
        }
      }
    }
    session.check_and_reset_retry_info(*cur_trace_id, THIS_WORKER.need_retry());
    session.set_last_trace_id(ObCurTraceId::get_trace_id());

    if (!retry_ctrl_.need_retry()) {
      // if no retry would be performed any more, clear the piece cache
      ObPieceCache *piece_cache = nullptr;
      int upper_scope_ret = ret;
      ret = OB_SUCCESS;
      piece_cache = session.get_piece_cache();
      if (OB_NOT_NULL(piece_cache)) {
        for (uint64_t i = 0; OB_SUCC(ret) && i < params_num_; i++) {
          if (OB_FAIL(piece_cache->remove_piece(
                  piece_cache->get_piece_key(stmt_id_, i), session))) {
            if (OB_HASH_NOT_EXIST == ret) {
              ret = OB_SUCCESS;
              LOG_INFO("piece hash not exist", K(ret), K(stmt_id_), K(i));
            } else {
              need_disconnect = true;
              LOG_WARN("remove piece fail", K(ret), K(need_disconnect), K(stmt_id_), K(i));
            }
          }
        }
      } else {
        LOG_DEBUG("piece_cache_ is null");
      }
      ret = upper_scope_ret;
    }

    if (async_resp_used) {
      if (OB_FAIL(ret) && need_disconnect && is_conn_valid()) {
        force_disconnect();
      }
      const int handoff_ret = handoff_async_request(session.get_mysql_end_trans_cb());
      if (OB_UNLIKELY(OB_SUCCESS != handoff_ret)) {
        LOG_ERROR("failed to hand off async mysql request ownership", K(handoff_ret));
        force_disconnect();
      }
    }
  }

  if (OB_NOT_NULL(sess) && !sess->get_in_transaction()) {
    // transcation ends, end trace
  }

  if (!async_resp_used && OB_FAIL(ret) && is_conn_valid()) {
    if (need_response_error) {
      send_error_packet(ret, NULL);
    }
    if (need_disconnect) {
      force_disconnect();
      LOG_WARN("disconnect connection when process query", K(ret));
    }
  }
  // If the response has already been sent asynchronously, this logic will be executed in cb, so skip flush_buffer() here
  if (async_resp_used) {
    // The callback sender already owns response and finish.
  } else if (!THIS_WORKER.need_retry()) {
    flush_ret = flush_buffer(true);
  }

  THIS_WORKER.set_session(NULL);
  if (sess != NULL) {
    revert_session(sess); //current ignore revert session ret
  }

  return (OB_SUCCESS != ret) ? ret : flush_ret;
}

int ObMPStmtExecute::get_pl_type_by_type_info(ObIAllocator &allocator,
                                              const TypeInfo *type_info,
                                              const pl::ObUserDefinedType *&pl_type)
{
  int ret = OB_SUCCESS;
  UNUSEDx(allocator, type_info, pl_type);
  ret = OB_NOT_SUPPORTED;
  LOG_WARN("not support", K(ret));
  LOG_USER_ERROR(OB_NOT_SUPPORTED, "Get PL type by type info is not supported in CE version");
  return ret;
}

int ObMPStmtExecute::parse_complex_param_value(ObIAllocator &allocator,
                                               const ObCharsetType charset,
                                               const ObCollationType cs_type,
                                               const char *&data,
                                               const common::ObTimeZoneInfo *tz_info,
                                               TypeInfo *type_info,
                                               ObObjParam &param)
{
  int ret = OB_SUCCESS;
  const pl::ObUserDefinedType *pl_type = NULL;
  int64_t param_size = 0, param_pos = 0;
  ObSQLSessionInfo *session = ctx_.session_info_;
  CK (OB_NOT_NULL(type_info));
  OZ (get_pl_type_by_type_info(allocator, type_info, pl_type));
  CK (OB_NOT_NULL(pl_type));
  OZ (pl_type->init_obj(*(ctx_.schema_guard_), allocator, param, param_size));
  OX (param.set_udt_id(pl_type->get_user_type_id()));
  CK (OB_NOT_NULL(session));
  OZ (pl_type->deserialize(*(ctx_.schema_guard_), allocator, session, charset, cs_type,
        tz_info, data, reinterpret_cast<char *>(param.get_ext()), param_size, param_pos));
  OX (param.set_need_to_check_extend_type(true));
  return ret;
}

int ObMPStmtExecute::parse_basic_param_value(ObIAllocator &allocator,
                                             const uint32_t type,
                                             sql::ObSQLSessionInfo *session,
                                             const ObCharsetType charset,
                                             const ObCollationType cs_type,
                                             const char *& data,
                                             const common::ObTimeZoneInfo *tz_info,
                                             ObObj &param,
                                             bool is_complex_element,
                                             ObPSAnalysisChecker *checker,
                                             bool is_unsigned)
{
  int ret = OB_SUCCESS;
  UNUSED(charset);
  switch(type) {
    case MYSQL_TYPE_TINY:
    case MYSQL_TYPE_SHORT:
    case MYSQL_TYPE_LONG:
    case MYSQL_TYPE_LONGLONG: {
      if (OB_FAIL(parse_integer_value(type, data, param, allocator, is_complex_element, checker, is_unsigned))) {
        LOG_WARN("parse integer value from client failed", K(ret));
      }
      break;
    }
    case MYSQL_TYPE_FLOAT: {
      float value = 0;
      PS_STATIC_DEFENSE_CHECK(checker, sizeof(value))
      {
        MEMCPY(&value, data, sizeof(value));
        data += sizeof(value);
        param.set_float(value);
      }
      break;
    }
    case MYSQL_TYPE_ORA_BINARY_FLOAT: {
      float value = 0;
      PS_STATIC_DEFENSE_CHECK(checker, sizeof(value))
      {
        MEMCPY(&value, data, sizeof(value));
        data += sizeof(value);
        param.set_float(value);
      }
      break;
    }
    case MYSQL_TYPE_DOUBLE: {
      double value = 0;
      PS_STATIC_DEFENSE_CHECK(checker, sizeof(value))
      {
        MEMCPY(&value, data, sizeof(value));
        data += sizeof(value);
        param.set_double(value);
      }
      break;
    }
    case MYSQL_TYPE_ORA_BINARY_DOUBLE: {
      double value = 0;
      PS_STATIC_DEFENSE_CHECK(checker, sizeof(value))
      {
        MEMCPY(&value, data, sizeof(value));
        data += sizeof(value);
        param.set_double(value);
      }
      break;
    }
    case MYSQL_TYPE_YEAR: {
      int16_t value = 0;
      PS_STATIC_DEFENSE_CHECK(checker, 2)
      {
        ObMySQLUtil::get_int2(data, value);
        uint8_t year = 0;
        if (OB_FAIL(ObTimeConverter::int_to_year(value, year))) {
        LOG_WARN("convert execute year failed", K(ret), K(value));
        } else {
        param.set_year(year);
        }
      }
      break;
    }
    case MYSQL_TYPE_DATE:
    case MYSQL_TYPE_DATETIME:
    case MYSQL_TYPE_TIMESTAMP: {
      if (OB_FAIL(parse_mysql_timestamp_value(static_cast<EMySQLFieldType>(type), data,
                                              param, tz_info, checker))) {
        LOG_WARN("parse timestamp value from client failed", K(ret));
      }
      break;
    }
    case MYSQL_TYPE_TIME:{
      if (OB_FAIL(parse_mysql_time_value(data, param, checker))) {
        LOG_WARN("parse timestamp value from client failed", K(ret));
      }
      break;
    }
    case MYSQL_TYPE_OB_TIMESTAMP_WITH_TIME_ZONE:
    case MYSQL_TYPE_OB_TIMESTAMP_WITH_LOCAL_TIME_ZONE:
    case MYSQL_TYPE_OB_TIMESTAMP_NANO: {
      ObTimeConvertCtx cvrt_ctx(tz_info, true);
      if (OB_FAIL(parse_ob_timestamp_value(
                            static_cast<EMySQLFieldType>(type), data, cvrt_ctx, param, checker))) {
        LOG_WARN("parse timestamp value from client failed", K(ret));
      }
      break;
    }
    case MYSQL_TYPE_OB_RAW:
    case MYSQL_TYPE_TINY_BLOB:
    case MYSQL_TYPE_MEDIUM_BLOB:
    case MYSQL_TYPE_LONG_BLOB:
    case MYSQL_TYPE_BLOB:
    case MYSQL_TYPE_STRING:
    case MYSQL_TYPE_VARCHAR:
    case MYSQL_TYPE_VAR_STRING:
    case MYSQL_TYPE_NEWDECIMAL:
    case MYSQL_TYPE_ORA_BLOB:
    case MYSQL_TYPE_ORA_CLOB:
    case MYSQL_TYPE_JSON:
    case MYSQL_TYPE_GEOMETRY: {
      ObString str;
      ObString dst;
      uint64_t length = 0;
      ObCollationType cur_cs_type = ObCharset::get_default_collation(charset);
      PS_STATIC_DEFENSE_CHECK(checker, 1)
      {
        // check first byte of `length` field and trust the encoder reguarding the remaining bytes.
        if (OB_FAIL(ObMySQLUtil::get_length(data, length))) {
          LOG_ERROR("decode varchar param value failed", K(ret));
        }
        PS_STATIC_DEFENSE_CHECK(checker, length)
        {
          str.assign_ptr(data, static_cast<ObString::obstr_size_t>(length));
        }
      }
      if (OB_FAIL(ret)) {
      } else if (length > OB_MAX_LONGTEXT_LENGTH) {
        ret = OB_ERR_INVALID_INPUT_ARGUMENT;
        LOG_WARN("input param len is over size", K(ret), K(length));
      } else {
        if (MYSQL_TYPE_STRING == type
            || MYSQL_TYPE_VARCHAR == type
            || MYSQL_TYPE_VAR_STRING == type
            || MYSQL_TYPE_ORA_CLOB == type
            || MYSQL_TYPE_JSON == type
            || MYSQL_TYPE_GEOMETRY == type) {
          int64_t extra_len = 0;
          if (MYSQL_TYPE_ORA_CLOB == type) {
            ObLobLocatorV2 lob(str);
            if (!lob.is_valid()) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("got invalid ps lob param", K(length), K(lob), K(type), K(cs_type));
            } // if INROW, does it need to do copy_or_convert_str?
          }
          if (MYSQL_TYPE_ORA_CLOB != type) {
            OZ(copy_or_convert_str(allocator,
                                cur_cs_type,
                                cs_type,
                                ObString(str.length() - extra_len, str.ptr() + extra_len),
                                dst,
                                extra_len));
          }
          if (OB_SUCC(ret) && MYSQL_TYPE_ORA_CLOB == type) {
            if (OB_FAIL(ob_write_string(allocator, str, dst))) {
              LOG_WARN("Failed to write str", K(ret));
            }
          }
        } else if (OB_FAIL(ob_write_string(allocator, str, dst))) {
          LOG_WARN("Failed to write str", K(ret));
        }
        if (OB_SUCC(ret)) {
          if (MYSQL_TYPE_NEWDECIMAL == type) {
            number::ObNumber nb;
            if (OB_FAIL(nb.from(str.ptr(), length, allocator))) {
              LOG_WARN("decode varchar param to number failed", K(ret), K(str));
            } else {
              param.set_number(nb);
            }
          } else if (MYSQL_TYPE_ORA_BLOB == type
                    || MYSQL_TYPE_ORA_CLOB == type) {
            if (MYSQL_TYPE_ORA_BLOB == type) {
              param.set_collation_type(CS_TYPE_BINARY);
            } else {
              param.set_collation_type(cs_type);
            }
            ObLobLocatorV2 lobv2(str);
            param.set_lob_value(ObLongTextType, dst.ptr(), dst.length());
            param.set_has_lob_header();
            LOG_TRACE("get lob locator v2", K(lobv2), K(cs_type), K(type));
          } else if (MYSQL_TYPE_TINY_BLOB == type
                    || MYSQL_TYPE_MEDIUM_BLOB == type
                    || MYSQL_TYPE_BLOB == type
                    || MYSQL_TYPE_LONG_BLOB == type
                    || MYSQL_TYPE_JSON == type
                    || MYSQL_TYPE_GEOMETRY == type) {
            // in ps protocol:
            //    MySQL mode: no need to call hextoraw
            // in text protocol:
            //    MySQL mode: no need to call hextoraw
            // Notice: text tc without lob header here, should not set has_lob_header flag here
            param.set_collation_type(cs_type);
            if (MYSQL_TYPE_TINY_BLOB == type) {
              param.set_lob_value(ObTinyTextType, dst.ptr(), dst.length());
            } else if (MYSQL_TYPE_MEDIUM_BLOB == type) {
              param.set_lob_value(ObMediumTextType, dst.ptr(), dst.length());
            } else if (MYSQL_TYPE_BLOB == type) {
              param.set_lob_value(ObTextType, dst.ptr(), dst.length());
            } else if (MYSQL_TYPE_LONG_BLOB == type) {
              param.set_lob_value(ObLongTextType, dst.ptr(), dst.length());
            } else if (MYSQL_TYPE_JSON == type) {
              param.set_json_value(ObJsonType, dst.ptr(), dst.length());
            } else if (MYSQL_TYPE_GEOMETRY == type) {
              param.set_geometry_value(ObGeometryType, dst.ptr(), dst.length());
            }
            if (OB_SUCC(ret) && param.is_lob_storage() && dst.length() > 0) {
              if (OB_FAIL(ObTextStringResult::ob_convert_obj_temporay_lob(param, allocator))) {
                LOG_WARN("Fail to convert plain lob data to templob",K(ret));
              }
            }
          } else if (MYSQL_TYPE_STRING == type
                     || MYSQL_TYPE_VARCHAR == type
                     || MYSQL_TYPE_VAR_STRING == type) {
            param.set_collation_type(cs_type);
            if (is_complex_element) {
              if (dst.length()== 0) {
                param.set_null();
              } else if (MYSQL_TYPE_STRING == type) {  // ObCharType
                param.set_char(dst);
              } else {
                param.set_varchar(dst);
              }
            } else {
              param.set_varchar(dst);
            }
          }
        }
      }
      data += length;
      break;
    }
    default: {
      LOG_USER_ERROR(OB_ERR_ILLEGAL_TYPE, type);
      ret = OB_ERR_ILLEGAL_TYPE;
      break;
    }
  }
  if (OB_SUCC(ret)) {
    param.set_collation_level(CS_LEVEL_COERCIBLE);
  }
  return ret;
}

int ObMPStmtExecute::parse_param_value(ObIAllocator &allocator,
                                       const uint32_t type,
                                       const ObCharsetType charset,
                                       const ObCollationType cs_type,
                                       const char *&data,
                                       const common::ObTimeZoneInfo *tz_info,
                                       TypeInfo *type_info,
                                       ObObjParam &param,
                                       const char *bitmap,
                                       int64_t param_id)
{
  int ret = OB_SUCCESS;
  uint64_t length = 0;
  uint64_t count = 1;
  common::ObFixedArray<ObSqlString, ObIAllocator>
                str_buf(THIS_WORKER.get_sql_arena_allocator());
  ObPieceCache *piece_cache =
      NULL == ctx_.session_info_ ? NULL : ctx_.session_info_->get_piece_cache();
  ObPiece *piece = NULL;
  sql::ObSQLSessionInfo *session = ctx_.session_info_;
  if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is null", K(ret));
  } else if (OB_NOT_NULL(piece_cache) && OB_FAIL(piece_cache->get_piece(stmt_id_, param_id, piece))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get piece fail.", K(ret));
  } else if (OB_ISNULL(piece_cache) || OB_ISNULL(piece)) {
    // send piece data will init piece cache
    // if piece cache is null, it must not be send piece protocol
    bool is_null = ObSMUtils::update_from_bitmap(param, bitmap, param_id);
    if (is_null) {
      LOG_DEBUG("param is null", K(param_id), K(param), K(type));
      if (ob_is_accuracy_length_valid_tc(param.get_param_meta().get_type())) {
        const_cast<ObObjMeta &>(param.get_param_meta()).set_collation_type(cs_type);
        const_cast<ObObjMeta &>(param.get_param_meta()).set_collation_level(CS_LEVEL_COERCIBLE);
      }
    } else if (OB_UNLIKELY(MYSQL_TYPE_COMPLEX == type)) {
      if (OB_FAIL(parse_complex_param_value(allocator, charset, cs_type,
                                            data, tz_info, type_info,
                                            param))) {
        LOG_WARN("failed to parse complex value", K(ret));
      }
    } else {
      bool is_unsigned = NULL == type_info || !type_info->elem_type_.get_meta_type().is_unsigned_integer() ? false : true; 
      if (OB_FAIL(parse_basic_param_value(allocator, type, session, charset, cs_type,
                                          data, tz_info, param, false, &analysis_checker_, is_unsigned))) {
        LOG_WARN("failed to parse basic param value", K(ret));
      } else {
        param.set_length(param.get_val_len());
      }
    }
    OX (param.set_param_meta());
  } else if (!support_send_long_data(type)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("this type is not support send long data.", K(type), K(ret));
  } else if (NULL == piece->get_allocator()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("piece allocator is null.", K(stmt_id_), K(param_id), K(ret));
  } else if (OB_SUCCESS != piece->get_error_ret()) {
    ret = piece->get_error_ret();
    LOG_WARN("send long data has error. ", K(stmt_id_), K(param_id), K(ret));
  } else {
    if (OB_UNLIKELY(MYSQL_TYPE_COMPLEX == type)) {
      // this must be array bounding.
      bool is_null = ObSMUtils::update_from_bitmap(param, bitmap, param_id);
      if (is_null) {
        LOG_DEBUG("param is null", K(param_id), K(param), K(type));
      } else {
        // 1. read count
        PS_DEFENSE_CHECK(1)
        {
          if (OB_FAIL(ObMySQLUtil::get_length(data, count))) {
            LOG_WARN("failed to get length", K(ret));
          }
        }
        // 2. make null map
        int64_t bitmap_bytes = ((count + 7) / 8);
        char is_null_map[bitmap_bytes];
        MEMSET(is_null_map, 0, bitmap_bytes);
        length = piece_cache->get_length_length(count) + bitmap_bytes;
        // 3. get string buffer (include length + value)
        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(str_buf.prepare_allocate(count))) {
          LOG_WARN("prepare fail.", K(ret), K(count));
        } else if (OB_FAIL(piece_cache->get_buffer(stmt_id_,
                                                  param_id,
                                                  count,
                                                  length,
                                                  str_buf,
                                                  is_null_map))) {
          LOG_WARN("piece get buffer fail.", K(ret), K(stmt_id_), K(param_id));
        } else {
          // 4. merge all this info
          char *tmp = static_cast<char*>(piece->get_allocator()->alloc(length));
          int64_t pos = 0;
          if (OB_ISNULL(tmp)) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("failed to alloc memory", K(ret));
          } else if (FALSE_IT(MEMSET(tmp, 0, length))) {
          } else if (OB_FAIL(ObMySQLUtil::store_length(tmp, length, count, pos))) {
            LOG_WARN("store length fail.", K(ret), K(stmt_id_), K(param_id));
          } else {
            MEMCPY(tmp+pos, is_null_map, bitmap_bytes);
            pos += bitmap_bytes;
            for (int64_t i=0; OB_SUCC(ret) && i<count; i++) {
              if (OB_FAIL(ObMySQLUtil::store_obstr(tmp, length, str_buf.at(i).string(), pos))) {
                LOG_WARN("store string fail.", K(ret), K(stmt_id_), K(param_id),
                        K(length), K(pos), K(i), K(str_buf.at(i).string()), K(str_buf.at(i).string().length()),
                        K(str_buf.at(i).length()));
              }
            }
          }
          if (OB_FAIL(ret)) {
            // do nothing.
          } else {
            const char* src = tmp;
            if (OB_FAIL(parse_complex_param_value(allocator, charset, cs_type,
                                                  src, tz_info, type_info,
                                                  param))) {
              LOG_WARN("failed to parse complex value", K(ret));
            }
          }
          piece->get_allocator()->free(tmp);
        }
      }
    } else {
      sql::ObSQLSessionInfo *session = ctx_.session_info_;
      if (OB_ISNULL(session)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("session is null", K(ret));
      } else if (OB_FAIL(str_buf.prepare_allocate(count))) {
        LOG_WARN("prepare fail.");
      } else if (OB_FAIL(piece_cache->get_buffer(stmt_id_,
                                                  param_id,
                                                  count,
                                                  length,
                                                  str_buf,
                                                  NULL))) {
        LOG_WARN("piece get buffer fail.", K(ret), K(stmt_id_), K(param_id));
      } else {
        char *tmp = static_cast<char*>(piece->get_allocator()->alloc(length));
        int64_t pos = 0;
        if (OB_ISNULL(tmp)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("failed to alloc memory", K(ret));
        } else if (FALSE_IT(MEMSET(tmp, 0, length))) {
        } else if (OB_FAIL(ObMySQLUtil::store_obstr(tmp, length, str_buf.at(0).string(), pos))) {
          LOG_WARN("store string fail.", K(ret), K(stmt_id_), K(param_id));
        } else {
          const char* src = tmp;
          bool is_unsigned = NULL == type_info || !type_info->elem_type_.get_meta_type().is_unsigned_integer() ? false : true;
          if (OB_FAIL(parse_basic_param_value(allocator, type, session, charset, cs_type,
                                              src, tz_info, param, false, NULL ,is_unsigned))) {
            LOG_WARN("failed to parse basic param value", K(ret));
          } else {
            param.set_param_meta();
            param.set_length(param.get_val_len());
          }
        }
        piece->get_allocator()->free(tmp);
      }
    }
  }
  return ret;
}



int ObMPStmtExecute::copy_or_convert_str(common::ObIAllocator &allocator,
                                         const ObCollationType src_type,
                                         const ObCollationType dst_type,
                                         const ObString &src,
                                         ObString &out,
                                         int64_t extra_buf_len /* = 0 */)
{
  int ret = OB_SUCCESS;
  if (!ObCharset::is_valid_collation(src_type) || !ObCharset::is_valid_collation(dst_type)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid collation", K(ret), K(dst_type));
  } else if (0 == src.length()
             || ObCharset::charset_type_by_coll(src_type)
                == ObCharset::charset_type_by_coll(dst_type)) {
    int64_t len = src.length() + extra_buf_len;
    if (len > 0) {
      char *buf = static_cast<char *>(allocator.alloc(len));
      if (NULL == buf) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("allocate failed", K(ret), K(len));
      } else {
        if (src.length() > 0) {
          MEMCPY(buf + extra_buf_len, src.ptr(), src.length());
        }
        out.assign_ptr(buf + extra_buf_len, src.length());
      }
    } else {
      out.reset();
    }
  } else {
    int64_t maxmb_len = 0;
    OZ(ObCharset::get_mbmaxlen_by_coll(dst_type, maxmb_len));
    const int64_t len = maxmb_len * src.length() + 1 + extra_buf_len;
    uint32_t res_len = 0;
    if (OB_SUCC(ret)) {
      char *buf = static_cast<char *>(allocator.alloc(len));
      if (NULL == buf) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("allocate failed", K(ret), K(len));
      } else {
        ObDataBuffer buf_alloc(buf + extra_buf_len, len - extra_buf_len);
        if (OB_FAIL(ObCharset::charset_convert(buf_alloc,
                                               src,
                                               src_type,
                                               dst_type,
                                               out,
                                               ObCharset::REPLACE_UNKNOWN_CHARACTER))) {
          LOG_WARN("fail to charset convert", K(ret), K(src_type), K(dst_type),
          K(src), K(len), K(extra_buf_len));
        }
      }
    }
  }
  return ret;
}

int ObMPStmtExecute::parse_integer_value(const uint32_t type,
                                         const char *&data,
                                         ObObj &param,
                                         ObIAllocator &allocator,
                                         bool is_complex_element,
                                         ObPSAnalysisChecker *checker,
                                         bool is_unsigned)
{
  int ret = OB_SUCCESS;
  switch(type) {
    case MYSQL_TYPE_TINY: {
      PS_STATIC_DEFENSE_CHECK(checker, 1)
      {
        int8_t value;
        ObMySQLUtil::get_int1(data, value);
        is_unsigned ? param.set_utinyint(value) : param.set_tinyint(value);
      }
      break;
    }
    case MYSQL_TYPE_SHORT: {
      PS_STATIC_DEFENSE_CHECK(checker, 2)
      {
        int16_t value = 0;
        ObMySQLUtil::get_int2(data, value);
        is_unsigned ? param.set_usmallint(value) : param.set_smallint(value);
      }
      break;
    }
    case MYSQL_TYPE_LONG: {
      PS_STATIC_DEFENSE_CHECK(checker, 4)
      {
        int32_t value = 0;
        ObMySQLUtil::get_int4(data, value);
        is_unsigned ? param.set_uint32(value) : param.set_int32(value);
      }
      break;
    }
    case MYSQL_TYPE_LONGLONG: {
      PS_STATIC_DEFENSE_CHECK(checker, 8)
      {
        int64_t value = 0;
        ObMySQLUtil::get_int8(data, value);
        is_unsigned ? param.set_uint(ObUInt64Type, value) : param.set_int(value);
      }
      break;
    }
    default: {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("unexpected integer type", K(type), K(ret));
      break;
    }
  }
  return ret;
}

int ObMPStmtExecute::parse_mysql_timestamp_value(const EMySQLFieldType field_type,
                                                 const char *&data,
                                                 ObObj &param,
                                                 const common::ObTimeZoneInfo *tz_info,
                                                 ObPSAnalysisChecker *checker)
{
  int ret = OB_SUCCESS;
  int8_t length = 0;
  int16_t year = 0;
  int8_t month = 0;
  int8_t day = 0;
  int8_t hour = 0;
  int8_t min = 0;
  int8_t second = 0;
  int32_t microsecond = 0;
  ObPreciseDateTime value;
  PS_STATIC_DEFENSE_CHECK(checker, 1)
  {
    ObMySQLUtil::get_int1(data, length);
    if (0 == length) {
      value = 0;
    } else if (4 == length) {
      PS_STATIC_DEFENSE_CHECK(checker, 4)
      {
        ObMySQLUtil::get_int2(data, year);
        ObMySQLUtil::get_int1(data, month);
        ObMySQLUtil::get_int1(data, day);
      }
    } else if (7 == length) {
      PS_STATIC_DEFENSE_CHECK(checker, 7)
      {
        ObMySQLUtil::get_int2(data, year);
        ObMySQLUtil::get_int1(data, month);
        ObMySQLUtil::get_int1(data, day);
        ObMySQLUtil::get_int1(data, hour);
        ObMySQLUtil::get_int1(data, min);
        ObMySQLUtil::get_int1(data, second);
      }
    } else if (11 == length) {
      PS_STATIC_DEFENSE_CHECK(checker, 11)
      {
        ObMySQLUtil::get_int2(data, year);
        ObMySQLUtil::get_int1(data, month);
        ObMySQLUtil::get_int1(data, day);
        ObMySQLUtil::get_int1(data, hour);
        ObMySQLUtil::get_int1(data, min);
        ObMySQLUtil::get_int1(data, second);
        ObMySQLUtil::get_int4(data, microsecond);
      }
    } else {
      ret = OB_ERROR;
      LOG_WARN("invalid mysql timestamp value length", K(length));
    }
  }

  if (OB_SUCC(ret)) {
    ObTime ob_time;
    if (0 != length) {
      ob_time.parts_[DT_YEAR] = year;
      ob_time.parts_[DT_MON] = month;
      ob_time.parts_[DT_MDAY] = day;
      ob_time.parts_[DT_HOUR] = hour;
      ob_time.parts_[DT_MIN] = min;
      ob_time.parts_[DT_SEC] = second;
      ob_time.parts_[DT_USEC] = microsecond;
      if (!ObTimeUtility2::is_valid_date(year, month, day)
          || !ObTimeUtility2::is_valid_time(hour, min, second, microsecond)) {
        ret = OB_INVALID_DATE_FORMAT;
        LOG_WARN("invalid date format", K(ret));
      } else {
        ObTimeConvertCtx cvrt_ctx(NULL, false);
        ob_time.parts_[DT_DATE] = ObTimeConverter::ob_time_to_date(ob_time);
        if (field_type == MYSQL_TYPE_DATE) {
          value = ob_time.parts_[DT_DATE];
        } else if (OB_FAIL(ObTimeConverter::ob_time_to_datetime(ob_time, cvrt_ctx, value))){
          LOG_WARN("convert obtime to datetime failed", K(value), K(year), K(month),
                   K(day), K(hour), K(min), K(second));
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (field_type == MYSQL_TYPE_TIMESTAMP) {
      int64_t ts_value = 0;
      if (OB_FAIL(ObTimeConverter::datetime_to_timestamp(value, tz_info, ts_value))) {
        LOG_WARN("datetime to timestamp failed", K(ret));
      } else {
        param.set_timestamp(ts_value);
      }
    } else if (field_type == MYSQL_TYPE_DATETIME) {
      param.set_datetime(value);
    } else if (field_type == MYSQL_TYPE_DATE) {
      param.set_date(static_cast<int32_t>(value));
    }
  }
  LOG_DEBUG("get datetime", K(length), K(year), K(month), K(day), K(hour), K(min),K(second),  K(microsecond), K(value));
  return ret;
}

int ObMPStmtExecute::parse_ob_timestamp_value(const obmysql::EMySQLFieldType field_type,
    const char *&data, const ObTimeConvertCtx &cvrt_ctx, ObObj &param, ObPSAnalysisChecker *checker)
{
  int ret = OB_SUCCESS;
  int8_t total_len = 0;
  ObObjType obj_type;
  ObOTimestampData ot_data;
  int8_t scale = -1;
  PS_STATIC_DEFENSE_CHECK(checker, 1)
  {
    ObMySQLUtil::get_int1(data, total_len);
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ObSMUtils::get_ob_type(obj_type, field_type))) {
    LOG_WARN("failed to get_ob_type", K(ret));
  } else if (OB_FAIL(ObTimeConverter::decode_otimestamp(obj_type, data, total_len, cvrt_ctx, ot_data, scale))) {
    LOG_WARN("failed to decode_otimestamp", K(ret));
  } else {
    PS_STATIC_DEFENSE_CHECK(checker, total_len)
    {
      data += total_len;
      param.set_otimestamp_value(obj_type, ot_data);
      param.set_scale(scale);
    }
  }
  return ret;
}

int ObMPStmtExecute::parse_mysql_time_value(const char *&data, ObObj &param, ObPSAnalysisChecker *checker)
{
  int ret = OB_SUCCESS;
  int8_t length = 0;
  int8_t is_negative = 0;
  int16_t year = 0;
  int8_t month = 0;
  int32_t day = 0;
  int8_t hour = 0;
  int8_t min = 0;
  int8_t second = 0;
  int32_t microsecond = 0;
  struct tm tmval;
  MEMSET(&tmval, 0, sizeof(tmval));
  int64_t value;
  PS_STATIC_DEFENSE_CHECK(checker, 1)
  {
    ObMySQLUtil::get_int1(data, length);
    if (0 == length) {
      value = 0;
    } else if (8 == length) {
      PS_STATIC_DEFENSE_CHECK(checker, 8)
      {
        ObMySQLUtil::get_int1(data, is_negative);
        ObMySQLUtil::get_int4(data, day);
        ObMySQLUtil::get_int1(data, hour);
        ObMySQLUtil::get_int1(data, min);
        ObMySQLUtil::get_int1(data, second);
      }
    } else if (12 == length) {
      PS_STATIC_DEFENSE_CHECK(checker, 12)
      {
        ObMySQLUtil::get_int1(data, is_negative);
        ObMySQLUtil::get_int4(data, day);
        ObMySQLUtil::get_int1(data, hour);
        ObMySQLUtil::get_int1(data, min);
        ObMySQLUtil::get_int1(data, second);
        ObMySQLUtil::get_int4(data, microsecond);
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("unexpected time length", K(length), K(ret));
    }

    if (OB_SUCC(ret)) {
      ObTime ob_time;
      if (0 != length) {
        ob_time.parts_[DT_YEAR] = year;
        ob_time.parts_[DT_MON] = month;
        ob_time.parts_[DT_MDAY] = day;
        ob_time.parts_[DT_HOUR] = hour;
        ob_time.parts_[DT_MIN] = min;
        ob_time.parts_[DT_SEC] = second;
        ob_time.parts_[DT_USEC] = microsecond;
        if (!ObTimeUtility2::is_valid_time(hour, min, second, microsecond)) {
          ret = OB_INVALID_DATE_FORMAT;
          LOG_WARN("invalid date format", K(ret));
        } else {
          ob_time.parts_[DT_DATE] = ObTimeConverter::ob_time_to_date(ob_time);
          ob_time.parts_[DT_HOUR] += ob_time.parts_[DT_MDAY] * 24;
          ob_time.parts_[DT_MDAY] = 0;
          value = ObTimeConverter::ob_time_to_time(ob_time);
          if(is_negative) {
            value = -value;
          }
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    param.set_time(value);
  }
  LOG_INFO("get time", K(length), K(year), K(month), K(day), K(hour), K(min),K(second),  K(microsecond), K(value));
  return ret;
}

int ObMPStmtExecute::response_query_header(ObSQLSessionInfo &session, pl::ObPLServerCursorInfo &cursor)
{
  int ret = OB_SUCCESS;
  ObSyncPlanDriver drv(gctx_, ctx_, session, retry_ctrl_, packet_sender_,
                       OB_INVALID_COUNT);
  if (0 == cursor.get_field_columns().count()) {
    // SELECT * INTO OUTFILE return null field, and only response ok packet
    ObOKPParam ok_param;
    ok_param.affected_rows_ = 0;
    ok_param.has_more_result_ = false;
    if (OB_FAIL(send_ok_packet(session, ok_param))) {
      LOG_WARN("fail to send ok packt", K(ok_param), K(ret));
    }
  } else {
    if (OB_FAIL(drv.response_query_header(cursor.get_field_columns(), false,
                                          false))) {
      LOG_WARN("fail to get autocommit", K(ret));
    }
  }
  return ret;
}

} //end of namespace observer
} //end of namespace oceanbase

namespace oceanbase
{
namespace query
{

int decode_mysql_basic_param_value(
    common::ObIAllocator &allocator,
    uint32_t type,
    common::ObCharsetType charset,
    common::ObCharsetType ncharset,
    common::ObCollationType collation,
    const char *&data,
    const common::ObTimeZoneInfo *time_zone,
    common::ObObj &value,
    bool is_complex_element,
    bool is_unsigned)
{
  UNUSED(ncharset);
  return observer::ObMPStmtExecute::parse_basic_param_value(
      allocator,
      type,
      nullptr,
      charset,
      collation,
      data,
      time_zone,
      value,
      is_complex_element,
      nullptr,
      is_unsigned);
}

} // namespace query
} // namespace oceanbase
