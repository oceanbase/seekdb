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
#include "obmp_utils.h"
#include "observer/mysql/obmp_utils.h"
#include "sql/session/ob_sess_info_verify.h"
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
    LOG_DEBUG("sys var changed", K(session.get_tenant_name()), K(sys_var.count()));
    // record sys var need sync in error scene.
    bool is_exist_error_sync_var = false;
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
        } else if (session.is_exist_error_sync_var(change_var.id_) && FALSE_IT(is_exist_error_sync_var = true)) {
          // do nothing.
        } else {
          if (is_exist_error_sync_var) {
            ObSessInfoEncoder* encoder = NULL;
            if (OB_FAIL(session.get_sess_encoder(SESSION_SYNC_ERROR_SYS_VAR, encoder))) {
              LOG_WARN("failed to get session encoder", K(ret));
            } else {
              encoder->is_changed_ = true;
              is_exist_error_sync_var = false;
            }
          }
          if (OB_FAIL(ret)) {
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
        }
      } else {
        LOG_TRACE("sys var not actully changed", K(changed), K(change_var), K(new_val),
               K(session.get_server_sid()));
      }
    }
  }

  if (session.is_user_var_changed()) {
    const ObIArray<ObString> &user_var = session.get_changed_user_var();
    ObSessionValMap &user_map = session.get_user_var_val_map();
    for (int64_t i = 0; i < user_var.count() && OB_SUCCESS == ret; ++i) {
      ObString name = user_var.at(i);
      ObSessionVariable sess_var;
      if (name.empty()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid variable name", K(name), K(ret));
      } else if (OB_FAIL(user_map.get_refactored(name, sess_var))) {
        LOG_WARN("unknown user variable", K(name), K(ret));
      } else {
        ObStringKV str_kv;
        str_kv.key_ = name;
        if (OB_FAIL(get_user_sql_literal(allocator, sess_var.value_, str_kv.value_, session.create_obj_print_params()))) {
          LOG_WARN("fail to get user sql literal", K(sess_var.value_), K(ret));
        } else if (OB_FAIL(ok_pkt.add_user_var(str_kv))) {
          LOG_WARN("fail to add user var", K(str_kv), K(ret));
        } else {
          LOG_DEBUG("succ to add user var", K(str_kv), K(ret));
        }
      }
    }
  }

  return ret;
}


int ObMPUtils::sync_session_info(sql::ObSQLSessionInfo &sess, const common::ObString &sess_infos)
{
  int ret = OB_SUCCESS;
  const char *buf = sess_infos.ptr();
  const char *data = sess_infos.ptr();
  const int64_t len = sess_infos.length();
  const char *end = buf + len;
  int64_t pos = 0;

  LOG_DEBUG("sync sess_inf", K(sess.get_is_in_retry()),
            K(sess.get_server_sid()), KP(data), K(len), KPHEX(data, len));

  // decode sess_info
  if (NULL != sess_infos.ptr() && !sess.get_is_in_retry()) {
    common::ObFixedBitSet<oceanbase::sql::SessionSyncInfoType::SESSION_SYNC_MAX_TYPE> succ_info_types;
    while (OB_SUCC(ret) && pos < len) {
      int16_t info_type = 0;
      int32_t info_len = 0;
      int64_t pos0 = 0;
      char *sess_buf = NULL;
      LOG_TRACE("sync field sess_inf", K(sess.get_server_sid()), KP(data), K(pos), K(len), KPHEX(data+pos, len-pos));
      if (OB_FAIL(ObProtoTransUtil::resolve_type_and_len(buf, len, pos, info_type, info_len))) {
        LOG_WARN("failed to resolve type and len", K(ret), K(len), K(pos));
      } else if (info_type < 0 || info_len <= 0) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid session sync info encoder", K(ret), K(info_type));
      // for old version compatible
      // if new version has a val that old version doesn't has, just ignore.
      } else if (info_type >= SESSION_SYNC_MAX_TYPE) {
        pos += info_len;
      } else if (FALSE_IT(pos0 = pos)) {
      } else if (OB_FAIL(sess.update_sess_sync_info(
                                  (oceanbase::sql::SessionSyncInfoType)(info_type),
                                  buf, (int64_t)info_len + pos0, pos0))) {
        LOG_WARN("failed to update session sync info",
                 K(ret), K(info_type), K(sess.get_server_sid()), K(succ_info_types), K(pos), K(info_len), K(info_len+pos));
      } else {
        pos += info_len;
        succ_info_types.add_member(info_type);
      }
      LOG_DEBUG("sync-session-info", K(info_type), K(info_len));
    }
  }

  return ret;
}

int ObMPUtils::add_cap_flag(OMPKOK &okp, sql::ObSQLSessionInfo &session)
{
  int ret = OB_SUCCESS;
  ObIAllocator &allocator = session.get_allocator();
  for (int64_t i = 0; OB_SUCC(ret) && i < session.get_sys_var_count(); ++i) {
    const ObBasicSysVar *sys_var = NULL;
    if (NULL == (sys_var = session.get_sys_var(i))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("sys var is NULL", K(i), "total", session.get_sys_var_count(), K(ret));
    } else if (sys_var->get_type() == SYS_VAR_OB_CAPABILITY_FLAG) {
      ObStringKV str_kv;
      str_kv.key_ = share::ObSysVarMeta::get_sys_var_name_by_id(sys_var->get_type()); // shadow copy
      if (OB_FAIL(get_plain_str_literal(allocator, sys_var->get_value(), str_kv.value_))) {
        LOG_WARN("fail to get sql literal", K(i), K(ret));
      } else if (OB_FAIL(okp.add_system_var(str_kv))) {
        LOG_WARN("fail to add system var", K(i), K(str_kv), K(ret));
      }
    } else if (sys_var->get_type() == SYS_VAR___OB_CLIENT_CAPABILITY_FLAG) {
      ObStringKV str_kv;
      str_kv.key_ = share::ObSysVarMeta::get_sys_var_name_by_id(sys_var->get_type()); // shadow copy
      if (OB_FAIL(get_plain_str_literal(allocator, sys_var->get_value(), str_kv.value_))) {
        LOG_WARN("fail to get sql literal", K(i), K(ret));
      } else if (OB_FAIL(okp.add_system_var(str_kv))) {
        LOG_WARN("fail to add system var", K(i), K(str_kv), K(ret));
      }
    } else {
      // skip
    }
  }
  return ret;
}

int ObMPUtils::add_nls_format(OMPKOK &okp, sql::ObSQLSessionInfo &session, const bool only_changed/*false*/)
{
  int ret = OB_SUCCESS;
  if (only_changed) {
    if (session.is_sys_var_changed()) {
      const ObIArray<sql::ObBasicSessionInfo::ChangedVar> &sys_var = session.get_changed_sys_var();
      LOG_DEBUG("sys var changed", K(session.get_tenant_name()), K(sys_var.count()));
      int64_t max_add_count = ObNLSFormatEnum::NLS_MAX;
      for (int64_t i = 0; OB_SUCC(ret) && i < sys_var.count() && max_add_count > 0; ++i) {
        const sql::ObBasicSessionInfo::ChangedVar change_var = sys_var.at(i);
        ObObj new_val;
        bool changed = true;
        ObNLSFormatEnum nls_enum = ObNLSFormatEnum::NLS_MAX;
        if (change_var.id_ == SYS_VAR_NLS_DATE_FORMAT) {
          nls_enum = ObNLSFormatEnum::NLS_DATE;
        } else if (change_var.id_ == SYS_VAR_NLS_TIMESTAMP_FORMAT) {
          nls_enum = ObNLSFormatEnum::NLS_TIMESTAMP;
        } else if (change_var.id_ == SYS_VAR_NLS_TIMESTAMP_TZ_FORMAT) {
          nls_enum = ObNLSFormatEnum::NLS_TIMESTAMP_TZ;
        }
        if (nls_enum != ObNLSFormatEnum::NLS_MAX) {
          --max_add_count;
          if (OB_FAIL(session.is_sys_var_actully_changed(change_var.id_,
                                                         change_var.old_val_,
                                                         new_val,
                                                         changed))) {
            LOG_WARN("failed to check actully changed", K(ret), K(change_var), K(changed));
          } else if (changed) {
            ObStringKV str_kv;
            str_kv.key_ = share::ObSysVarMeta::get_sys_var_name_by_id(change_var.id_); // shadow copy
            if (ObNLSFormatEnum::NLS_DATE == nls_enum) {
              str_kv.value_ = session.get_local_nls_date_format();
            } else if (ObNLSFormatEnum::NLS_TIMESTAMP == nls_enum) {
              str_kv.value_ = session.get_local_nls_timestamp_format();
            } else if (ObNLSFormatEnum::NLS_TIMESTAMP_TZ == nls_enum) {
              str_kv.value_ = session.get_local_nls_timestamp_tz_format();
            }

            if (OB_FAIL(okp.add_system_var(str_kv))) {
              LOG_WARN("failed to add system variable", K(str_kv), K(ret));
            } else {
              //AS ob pkt encoding is different from mysql, we should not set_state_changed true
              okp.set_state_changed(false);
              LOG_DEBUG("success add system var to ok pack", K(str_kv), K(change_var), K(new_val), K(okp));
            }
          }
        }
      }
    }
  } else {
    //AS ob pkt encoding is different from mysql, we should not set_state_changed true
    okp.set_state_changed(false);

    ObStringKV nls_date_str_kv;
    nls_date_str_kv.key_ = share::ObSysVarMeta::get_sys_var_name_by_id(SYS_VAR_NLS_DATE_FORMAT); // shadow copy
    nls_date_str_kv.value_ = session.get_local_nls_date_format();

    ObStringKV nls_timestamp_str_kv;
    nls_timestamp_str_kv.key_ = share::ObSysVarMeta::get_sys_var_name_by_id(SYS_VAR_NLS_TIMESTAMP_FORMAT); // shadow copy
    nls_timestamp_str_kv.value_ = session.get_local_nls_timestamp_format();

    ObStringKV nls_timestamp_tz_str_kv;
    nls_timestamp_tz_str_kv.key_ = share::ObSysVarMeta::get_sys_var_name_by_id(SYS_VAR_NLS_TIMESTAMP_TZ_FORMAT); // shadow copy
    nls_timestamp_tz_str_kv.value_ = session.get_local_nls_timestamp_tz_format();

    if (OB_FAIL(okp.add_system_var(nls_date_str_kv))) {
      LOG_WARN("fail to add system var", K(nls_date_str_kv), K(ret));
    } else if (OB_FAIL(okp.add_system_var(nls_timestamp_str_kv))) {
      LOG_WARN("fail to add system var", K(nls_timestamp_str_kv), K(ret));
    } else if (OB_FAIL(okp.add_system_var(nls_timestamp_tz_str_kv))) {
      LOG_WARN("fail to add system var", K(nls_timestamp_tz_str_kv), K(ret));
    } else {
      LOG_DEBUG("succ to add system var", K(okp), K(ret));
    }
  }
  return ret;
}

int ObMPUtils::add_session_info_on_connect(OMPKOK &okp, sql::ObSQLSessionInfo &session)
{
  int ret = OB_SUCCESS;
  // treat it as state changed
  okp.set_state_changed(true);

  // add database name
  if (session.is_database_changed()) {
    ObString db_name = session.get_database_name();
    okp.set_changed_schema(db_name);
  }

  // add all sys variables
  ObIAllocator &allocator = session.get_allocator();
  for (int64_t i = 0; OB_SUCC(ret) && i < session.get_sys_var_count(); ++i) {
    const ObBasicSysVar *sys_var = NULL;
    if (NULL == (sys_var = session.get_sys_var(i))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("sys var is NULL", K(i), "total", session.get_sys_var_count(), K(ret));
    } else {
      ObStringKV str_kv;
      str_kv.key_ = share::ObSysVarMeta::get_sys_var_name_by_id(sys_var->get_type()); // shadow copy
      if (OB_FAIL(sys_var->to_show_str(allocator, session, str_kv.value_))) {
        LOG_WARN("fail to get sql literal", K(i), K(ret));
      } else if (OB_FAIL(okp.add_system_var(str_kv))) {
        LOG_WARN("fail to add system var", K(i), K(str_kv), K(ret));
      }
    }
  }

  // add changed user variables
  if (session.is_user_var_changed()) {
    const ObIArray<ObString> &user_var = session.get_changed_user_var();
    ObSessionValMap &user_map = session.get_user_var_val_map();
    for (int64_t i = 0; i < user_var.count() && OB_SUCCESS == ret; ++i) {
      ObString name = user_var.at(i);
      ObSessionVariable sess_var;
      if (name.empty()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid variable name", K(name), K(ret));
      } else if (OB_FAIL(user_map.get_refactored(name, sess_var))) {
        LOG_WARN("unknown user variable", K(name), K(ret));
      } else {
        ObStringKV str_kv;
        str_kv.key_ = name;
        if (OB_FAIL(get_user_sql_literal(allocator, sess_var.value_, str_kv.value_, session.create_obj_print_params()))) {
          LOG_WARN("fail to get user sql literal", K(sess_var.value_), K(ret));
        } else if (OB_FAIL(okp.add_user_var(str_kv))) {
          LOG_WARN("fail to add user var", K(str_kv), K(ret));
        } else {
          LOG_DEBUG("succ to add user var", K(str_kv), K(ret));
        }
      }
    }
  }
  return ret;
}

// response _min_cluster_version on connect,
// design doc: 
int ObMPUtils::add_min_cluster_version(OMPKOK &okp, sql::ObSQLSessionInfo &session)
{
  int ret = OB_SUCCESS;
  const char *MIN_CLUSTER_VERSION_KEY = "_min_cluster_version";
  ObStringKV str_kv;
  str_kv.key_ = ObString::make_string(MIN_CLUSTER_VERSION_KEY);
  char version_buf[OB_CLUSTER_VERSION_LENGTH];
  int64_t pos = 0;
  ObObj version;
  if (OB_INVALID_INDEX == (pos = ObClusterVersion::print_version_str(
          version_buf, OB_CLUSTER_VERSION_LENGTH, GET_MIN_CLUSTER_VERSION()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to get min cluster version", K(ret));
  } else if (FALSE_IT(version.set_varchar(version_buf, pos))) {
    // do nothing
  } else if (OB_FAIL(get_user_sql_literal(session.get_allocator(),
                                          version,
                                          str_kv.value_,
                                          session.create_obj_print_params()))) {
    LOG_WARN("fail to get user sql literal", K(version), K(ret));
  } else if (OB_FAIL(okp.add_user_var(str_kv))) {
    LOG_WARN("fail to add user var", K(str_kv), K(ret));
  } else {
    LOG_TRACE("succ to add _min_cluster_version user var on connect", K(ret), K(str_kv),
              "sessid", session.get_server_sid());
  }

  return ret;
}

int ObMPUtils::get_plain_str_literal(ObIAllocator &allocator, const ObObj &obj, ObString &value_str)
{
  int ret = OB_SUCCESS;
  char *data = NULL;
  int64_t pos = 0;
  const bool is_plain = true;
  int64_t plain_str_print_length = 0;
  ObObjPrintParams default_print_params;
  if (obj.is_null()) {
    // if obj is null value , return ""; mysql have the same behavior
    pos = 0;
  } else if (OB_FAIL(get_literal_print_length(obj, is_plain, plain_str_print_length, default_print_params))) {
    LOG_WARN("fail to get buffer length", K(ret), K(obj), K(plain_str_print_length));
  } else if (OB_UNLIKELY(plain_str_print_length <= 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Invalid buffer length", K(ret), K(obj), K(plain_str_print_length));
  } else if (NULL == (data = static_cast<char *>(allocator.alloc(plain_str_print_length)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("fail to alloc mem", K(plain_str_print_length), K(ret));
  } else {
    ret = obj.print_plain_str_literal(data, plain_str_print_length, pos);
  }
  if (OB_SUCC(ret)) {
    value_str.assign_ptr(data, static_cast<uint32_t>(pos));
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
  if (!obj.is_string_or_lob_locator_type() && !obj.is_json() && !obj.is_geometry() && !obj.is_roaringbitmap()) {
    len = OB_MAX_SYS_VAR_NON_STRING_VAL_LENGTH;
  } else if (OB_UNLIKELY((len_of_string = obj.get_string_len()) < 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("string length invalid", K(obj), K(len_of_string));
  } else if (obj.is_char() || obj.is_varchar()
             || obj.is_text()
             || obj.is_json()
             || obj.is_geometry()
             || obj.is_roaringbitmap()) {
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
