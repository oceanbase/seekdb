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
      const sql::ObBasicSessionInfo::ChangedVar &change_var = sys_var.at(i);
      DatumValue new_val;
      bool changed = true;
      if (OB_FAIL(session.is_sys_var_actully_changed(change_var.id_,
                                                     change_var.old_val_,
                                                     new_val,
                                                     changed,
                                                     allocator))) {
      } else if (changed) {
        ObStringKV str_kv;
        sql::ObBasicSysVar *sys_var_ptr = NULL;
        if (OB_FAIL(share::ObSysVarMeta::get_sys_var_name_by_id(change_var.id_, str_kv.key_))) {
        } else if (OB_FAIL(session.get_sys_variable(change_var.id_, sys_var_ptr))){
        } else if (OB_ISNULL(sys_var_ptr)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(sys_var_ptr->to_show_str(allocator, session, str_kv.value_))) {
        } else if (OB_FAIL(ok_pkt.add_system_var(str_kv))) {
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

} // end of namespace observer
} // end of namespace oceanbase
