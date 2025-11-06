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

#ifndef _OBMP_UTILS_H_
#define _OBMP_UTILS_H_
#include <stdint.h>
#include "rpc/obmysql/ob_2_0_protocol_utils.h"
#include "sql/monitor/flt/ob_flt_control_info_mgr.h"

namespace oceanbase
{
namespace obmysql
{
class OMPKOK;
}
namespace share
{
class ObFeedbackRerouteInfo;
}
namespace sql
{
class ObSQLSessionInfo;
}
namespace common
{
class ObTimeZoneInfo;
class ObString;
class ObIAllocator;
class ObObj;
}
namespace observer
{
class ObMPUtils
{
public:
  static int add_changed_session_info(obmysql::OMPKOK &ok_pkt, sql::ObSQLSessionInfo &session);
  static int append_modfied_sess_info(common::ObIAllocator &allocator,
                                      sql::ObSQLSessionInfo &sess,
                                      ObIArray<obmysql::ObObjKV> *extra_info,
                                      ObIArray<obmysql::Obp20Encoder*> *extra_info_ecds,
                                      bool is_new_extra_info,
                                      bool need_sync_sys_var = true);
  static int sync_session_info(sql::ObSQLSessionInfo &sess, const common::ObString &sess_infos);
  static int add_session_info_on_connect(obmysql::OMPKOK &okp, sql::ObSQLSessionInfo &session);
  static int add_min_cluster_version(obmysql::OMPKOK &okp, sql::ObSQLSessionInfo &session);
  static int add_client_feedback(obmysql::OMPKOK &ok_pkt, sql::ObSQLSessionInfo &session);
  static int add_client_reroute_info(obmysql::OMPKOK &pk_pkt,
                                     sql::ObSQLSessionInfo &session,
                                     share::ObFeedbackRerouteInfo &reroute_info);
  static int add_nls_format(obmysql::OMPKOK &pk_pkt,
                            sql::ObSQLSessionInfo &session,
                            const bool only_changed = false);
  static int add_cap_flag(obmysql::OMPKOK &okp, sql::ObSQLSessionInfo &session);
private:
  static int get_plain_str_literal(common::ObIAllocator &allocator, const common::ObObj &obj,
                                   common::ObString &value_str);

  static int get_user_sql_literal(common::ObIAllocator &allocator, const common::ObObj &obj,
                                  common::ObString &value_str, const common::ObObjPrintParams &print_param);
  static int get_literal_print_length(const common::ObObj &obj, bool is_plain, int64_t &len,
                                      const common::ObObjPrintParams &print_param);
};
} // end of namespace observer
} // end of namespace oceanbase

#endif /* _OBMP_UTILS_H_ */
