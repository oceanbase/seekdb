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
#include "observer/mysql/obmp_utils.h"
#include "common/object/ob_object.h"
#include "lib/charset/ob_charset.h"
#include "rpc/obmysql/packet/ompk_ok.h"
#include "share/system_variable/ob_sys_var_meta.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/session/ob_system_variable.h"
namespace oceanbase
{
namespace observer
{
using namespace common;
using namespace share;
using namespace obmysql;
using namespace sql;

int ObMPUtils::add_changed_session_info(OMPKOK &ok_pkt, sql::ObSQLSessionInfo &session)
{
  int ret = OB_SUCCESS;
  if (session.is_session_info_changed()) {
    ok_pkt.set_state_changed(true);
  }

  ObIAllocator &allocator = session.get_allocator();
  if (session.is_database_changed()) {
    ObCollationType client_cs_type = session.get_local_collation_connection();
    ObString db_name;
    if (OB_UNLIKELY(OB_SUCCESS != ObCharset::charset_convert(allocator,
                                                             session.get_database_name(),
                                                             CS_TYPE_UTF8MB4_BIN,
                                                             client_cs_type,
                                                             db_name,
                                                             ObCharset::REPLACE_UNKNOWN_CHARACTER))) {
    } else {
      ok_pkt.set_changed_schema(db_name);
    }
  }

  if (session.is_sys_var_changed()) {
    const ObIArray<sql::ObBasicSessionInfo::ChangedVar> &sys_var = session.get_changed_sys_var();
    LOG_DEBUG("sys var changed", K(session.get_runtime_name()), K(sys_var.count()));
    for (int64_t i = 0; OB_SUCC(ret) && i < sys_var.count(); ++i) {
      sql::ObBasicSessionInfo::ChangedVar change_var = sys_var.at(i);
      ObObj new_val;
      bool changed = true;
      if (OB_FAIL(session.is_sys_var_actully_changed(change_var.id_,
                                                     change_var.old_val_,
                                                     new_val,
                                                     changed))) {
        LOG_WARN("failed to check actully changed", K(ret), K(change_var), K(changed));
      } else if (changed) {
        ObStringKV str_kv;
        sql::ObBasicSysVar *sys_var_ptr = NULL;
        if (OB_FAIL(share::ObSysVarMeta::get_sys_var_name_by_id(change_var.id_, str_kv.key_))) {
          LOG_WARN("failed to get sys variable name", K(ret), K(change_var));
        } else if (OB_FAIL(session.get_sys_variable(change_var.id_, sys_var_ptr))){
          LOG_WARN("failed to get sys variable", K(ret), K(change_var));
        } else if (OB_ISNULL(sys_var_ptr)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("sys var ptr is null", K(ret), K(change_var));
        } else if (OB_FAIL(sys_var_ptr->to_show_str(allocator, session, str_kv.value_))) {
          LOG_WARN("failed to get sys variable new value string", K(ret), K(new_val), K(change_var.id_));
        } else if (OB_FAIL(ok_pkt.add_system_var(str_kv))) {
          LOG_WARN("failed to add system variable", K(str_kv), K(ret));
        } else {
#ifndef NDEBUG
          LOG_INFO("success add system var to ok pack", K(str_kv), K(change_var), K(new_val),
             K(session.get_server_sid()));
#else
          // for autocommit change record.
          LOG_TRACE("success add system var to ok pack", K(str_kv), K(change_var), K(new_val),
             K(session.get_server_sid()), K(change_var.id_));
#endif
        }
      } else {
        LOG_TRACE("sys var not actully changed", K(changed), K(change_var), K(new_val),
               K(session.get_server_sid()));
      }
    }
  }

  return ret;
}

int ObMPUtils::add_nls_format(OMPKOK &okp, sql::ObSQLSessionInfo &session, const bool only_changed/*false*/)
{
  int ret = OB_SUCCESS;
  if (!only_changed) {
    okp.set_state_changed(false);

    ObStringKV nls_date_str_kv;
    ObStringKV nls_timestamp_str_kv;
    ObStringKV nls_timestamp_tz_str_kv;
    nls_date_str_kv.key_.assign_ptr("nls_date_format", static_cast<int32_t>(strlen("nls_date_format")));
    nls_date_str_kv.value_ = session.get_local_nls_date_format();
    nls_timestamp_str_kv.key_.assign_ptr("nls_timestamp_format",
                                         static_cast<int32_t>(strlen("nls_timestamp_format")));
    nls_timestamp_str_kv.value_ = session.get_local_nls_timestamp_format();
    nls_timestamp_tz_str_kv.key_.assign_ptr("nls_timestamp_tz_format",
                                            static_cast<int32_t>(strlen("nls_timestamp_tz_format")));
    nls_timestamp_tz_str_kv.value_ = session.get_local_nls_timestamp_tz_format();

    if (OB_FAIL(okp.add_system_var(nls_date_str_kv))) {
      LOG_WARN("fail to add system var", K(nls_date_str_kv), K(ret));
    } else if (OB_FAIL(okp.add_system_var(nls_timestamp_str_kv))) {
      LOG_WARN("fail to add system var", K(nls_timestamp_str_kv), K(ret));
    } else if (OB_FAIL(okp.add_system_var(nls_timestamp_tz_str_kv))) {
      LOG_WARN("fail to add system var", K(nls_timestamp_tz_str_kv), K(ret));
    }
  } else {
    // NLS system variables are fixed in MySQL-only mode, so there is no changed
    // variable payload to append after login.
  }
  return ret;
}

int ObMPUtils::get_user_sql_literal(ObIAllocator &allocator, const ObObj &obj, ObString &value_str,
                                    const common::ObObjPrintParams &print_param)
{
  int ret = OB_SUCCESS;
  char *data = NULL;
  int64_t pos = 0;
  const bool is_plain = false;
  int64_t user_sql_print_length = 0;
  if (OB_FAIL(get_literal_print_length(obj, is_plain, user_sql_print_length, print_param))) {
    LOG_WARN("fail to get buffer length", K(ret), K(obj), K(user_sql_print_length));
  } else if (OB_UNLIKELY(user_sql_print_length <= 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Invalid buffer length", K(ret), K(obj), K(user_sql_print_length));
  } else if (NULL == (data = static_cast<char *>(allocator.alloc(user_sql_print_length)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("fail to alloc mem", K(user_sql_print_length), K(ret));
  } else if (OB_FAIL(obj.print_sql_literal(data, user_sql_print_length, pos, print_param))) {
    LOG_WARN("fail to print sql  literal", K(ret), K(pos), K(user_sql_print_length), K(obj));
  } else {
    value_str.assign_ptr(data, static_cast<uint32_t>(pos));
  }
  return ret;
}

int ObMPUtils::get_literal_print_length(const ObObj &obj, bool is_plain, int64_t &len,
                                        const common::ObObjPrintParams &print_param)
{
  int ret = OB_SUCCESS;
  len = 0;
  int32_t len_of_string = 0;
  if (!obj.is_string_or_lob_locator_type() && !obj.is_json() && !obj.is_geometry()) {
    len = OB_MAX_SYS_VAR_NON_STRING_VAL_LENGTH;
  } else if (OB_UNLIKELY((len_of_string = obj.get_string_len()) < 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("string length invalid", K(obj), K(len_of_string));
  } else if (obj.is_char() || obj.is_varchar()
             || obj.is_text()
             || obj.is_json()
             || obj.is_geometry()) {
    //if is_plain is false, 'j' will be print as "j\0" (with Quotation Marks here)
    //otherwise. as j\0 (withOUT Quotation Marks here)
    ObHexEscapeSqlStr sql_str(obj.get_string());
    len = len_of_string + (is_plain ? 1 : (3 + sql_str.get_extra_length()));
    if (ObCharset::charset_type_by_coll(print_param.cs_type_)
        != ObCharset::charset_type_by_coll(obj.get_collation_type())) {
      len += len_of_string * 4;
    }
    
    if (obj.is_json()) {
      // json add quote for stringbegin and end
      len += 2;
    }
  } else if (obj.is_binary() || obj.is_varbinary() || obj.is_hex_string() || obj.is_blob()) {
    //if is_plain is false, 'j' will be print as "X'6a'\0" (With Quotation Marks Here)
    //otherwise. as X'6a'\0 (Without Quotation Marks Here)
    len = 2 * len_of_string + (is_plain ? 4 : 6);
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("obj type unexpected", K(obj), K(is_plain));
  }
  return ret;
}

} // end of namespace observer
} // end of namespace oceanbase
