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

#define USING_LOG_PREFIX PL

#include "pl/sys_package/ob_dbms_monitor.h"

namespace oceanbase
{
namespace pl
{

namespace
{
int full_link_trace_not_supported(sql::ObExecContext &ctx,
                                  sql::ParamStore &params,
                                  common::ObObj &result)
{
  UNUSED(ctx);
  UNUSED(params);
  UNUSED(result);
  int ret = OB_NOT_SUPPORTED;
  LOG_USER_ERROR(OB_NOT_SUPPORTED, "DBMS monitor trace is");
  return ret;
}
} // namespace

int ObDBMSMonitor::session_trace_enable(sql::ObExecContext &ctx,
                                        sql::ParamStore &params,
                                        common::ObObj &result)
{
  return full_link_trace_not_supported(ctx, params, result);
}

int ObDBMSMonitor::session_trace_disable(sql::ObExecContext &ctx,
                                         sql::ParamStore &params,
                                         common::ObObj &result)
{
  return full_link_trace_not_supported(ctx, params, result);
}

int ObDBMSMonitor::client_id_trace_enable(sql::ObExecContext &ctx,
                                          sql::ParamStore &params,
                                          common::ObObj &result)
{
  return full_link_trace_not_supported(ctx, params, result);
}

int ObDBMSMonitor::client_id_trace_disable(sql::ObExecContext &ctx,
                                           sql::ParamStore &params,
                                           common::ObObj &result)
{
  return full_link_trace_not_supported(ctx, params, result);
}

int ObDBMSMonitor::mod_act_trace_enable(sql::ObExecContext &ctx,
                                        sql::ParamStore &params,
                                        common::ObObj &result)
{
  return full_link_trace_not_supported(ctx, params, result);
}

int ObDBMSMonitor::mod_act_trace_disable(sql::ObExecContext &ctx,
                                         sql::ParamStore &params,
                                         common::ObObj &result)
{
  return full_link_trace_not_supported(ctx, params, result);
}

int ObDBMSMonitor::tenant_trace_enable(sql::ObExecContext &ctx,
                                       sql::ParamStore &params,
                                       common::ObObj &result)
{
  return full_link_trace_not_supported(ctx, params, result);
}

int ObDBMSMonitor::tenant_trace_disable(sql::ObExecContext &ctx,
                                        sql::ParamStore &params,
                                        common::ObObj &result)
{
  return full_link_trace_not_supported(ctx, params, result);
}

} // namespace pl
} // namespace oceanbase
