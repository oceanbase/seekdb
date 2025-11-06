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

#ifndef OCEANBASE_SRC_PL_SYS_PACKAGE_DBMS_MONITOR_H_
#define OCEANBASE_SRC_PL_SYS_PACKAGE_DBMS_MONITOR_H_

#include "sql/engine/ob_exec_context.h"
#include "lib/number/ob_number_v2.h"

using namespace oceanbase::common::number;

namespace oceanbase
{
namespace pl
{
class ObDBMSMonitor
{
public:
  ObDBMSMonitor() {}
  virtual ~ObDBMSMonitor() {}
public:
  // DBMS_MONITOR.OB_SESSION_TRACE_ENABLE(session_id   IN  BINARY_INTEGER DEFAULT NULL, 
  //                                      level        IN  INT,
  //                                      sample_pct   IN  NUMBER,
  //                                      record_policy IN VARCHAR2);
  static int session_trace_enable(sql::ObExecContext &ctx, sql::ParamStore &params, common::ObObj &result);
  //DBMS_MONITOR.OB_SESSION_TRACE_DISABLE(session_id   IN  BINARY_INTEGER);
  static int session_trace_disable(sql::ObExecContext &ctx, sql::ParamStore &params, common::ObObj &result);

  // DBMS_MONITOR.OB_CLIENT_ID_TRACE_ENABLE(client_id    IN  VARCHAR2,
  //                                        level        IN  INT,
  //                                        sample_pct   IN  NUMBER,
  //                                        record_policy IN VARCHAR2);
  static int client_id_trace_enable(sql::ObExecContext &ctx, sql::ParamStore &params, common::ObObj &result);
  // DBMS_MONITOR.OB_CLIENT_ID_TRACE_DISABLE(client_id IN  VARCHAR2);
  static int client_id_trace_disable(sql::ObExecContext &ctx, sql::ParamStore &params, common::ObObj &result);

  // DBMS_MONITOR.OB_MOD_ACT_TRACE_ENABLE(module_name     IN VARCHAR2 DEFAULT ANY_MODULE,
  //                                      action_name     IN VARCHAR2 DEFAULT ANY_ACTION,
  //                                      level        IN  INT,
  //                                      sample_pct   IN  NUMBER,
  //                                      record_policy IN VARCHAR2);
  static int mod_act_trace_enable(sql::ObExecContext &ctx, sql::ParamStore &params, common::ObObj &result);
  // DBMS_MONITOR.OB_MOD_ACT_TRACE_DISABLE(module_name     IN  VARCHAR2,
  //                                       action_name     IN  VARCHAR2 DEFAULT ALL_ACTIONS)
  static int mod_act_trace_disable(sql::ObExecContext &ctx, sql::ParamStore &params, common::ObObj &result);

  // DBMS_MONITOR.OB_TRACE_ENABLE(level        IN  INT,
  //                              sample_pct   IN  NUMBER,
  //                              record_policy IN VARCHAR2);
  static int tenant_trace_enable(sql::ObExecContext &ctx, sql::ParamStore &params, common::ObObj &result);
  // DBMS_MONITOR.OB_TENANT_TRACE_DISABLE(tenant_name  IN VARCHAR2 DEFAULT NULL);
  static int tenant_trace_disable(sql::ObExecContext &ctx, sql::ParamStore &params, common::ObObj &result);

  static int resolve_control_info(FLTControlInfo &coninfo, ObNumber level, ObNumber sample_pct, ObString record_policy);
};

} // end of pl
} // end of oceanbase

#endif /* OCEANBASE_SRC_PL_SYS_PACKAGE_DBMS_MONITOR_H_ */
