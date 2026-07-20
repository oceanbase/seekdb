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
#include "pl/sys_package/ob_dbms_application.h"
namespace oceanbase
{

using namespace sql;
namespace pl
{
// this is a procedure, and not need to return result
int ObDBMSAppInfo::read_client_info(sql::ObExecContext &ctx, sql::ParamStore &params, common::ObObj &result)
{
  int ret = OB_SUCCESS;
  ObString client_info;
  UNUSED(result);
  CK (OB_NOT_NULL(ctx.get_my_session()));
  CK (OB_LIKELY(1 == params.count()));
  OV (params.at(0).get_param_meta().is_varchar(), OB_INVALID_ARGUMENT);
  client_info = ctx.get_my_session()->get_client_info();
  params.at(0).set_varchar(client_info);
  return ret;
}
// this is a procedure, and not need to return result
int ObDBMSAppInfo::read_module(sql::ObExecContext &ctx, sql::ParamStore &params, common::ObObj &result)
{
  int ret = OB_SUCCESS;
  ObString module_name;
  ObString action_name;
  UNUSED(result);
  CK (OB_NOT_NULL(ctx.get_my_session()));
  CK (OB_LIKELY(2 == params.count()));
  OV (params.at(0).get_param_meta().is_varchar(), OB_INVALID_ARGUMENT);
  OV (params.at(1).get_param_meta().is_varchar(), OB_INVALID_ARGUMENT);
  module_name = ctx.get_my_session()->get_module_name();
  action_name = ctx.get_my_session()->get_action_name();
  params.at(0).set_varchar(module_name);
  params.at(1).set_varchar(action_name);
  return ret;
}
// this is a procedure, and not need to return result
int ObDBMSAppInfo::set_action(sql::ObExecContext &ctx, sql::ParamStore &params, common::ObObj &result)
{
  int ret = OB_SUCCESS;
  ObString action_name;
  UNUSED(result);
  CK (OB_NOT_NULL(ctx.get_my_session()));
  ObSQLSessionInfo* sess = const_cast<ObSQLSessionInfo*>(ctx.get_my_session());
  if (OB_FAIL(ret)) {
    // do nothing
  } else {
    CK (OB_LIKELY(1 == params.count()));
    OV (params.at(0).is_varchar(), OB_INVALID_ARGUMENT);
    OZ (params.at(0).get_string(action_name));
    OZ (sess->set_action_name(action_name));
  }
  return ret;
}
// this is a procedure, and not need to return result
int ObDBMSAppInfo::set_client_info(sql::ObExecContext &ctx, sql::ParamStore &params, common::ObObj &result)
{
  int ret = OB_SUCCESS;
  ObString client_info;
  UNUSED(result);
  CK (OB_NOT_NULL(ctx.get_my_session()));
  ObSQLSessionInfo* sess = const_cast<ObSQLSessionInfo*>(ctx.get_my_session());
  if (OB_FAIL(ret)) {
    // do nothing
  } else {
    CK (OB_LIKELY(1 == params.count()));
    OV (params.at(0).is_varchar() || params.at(0).is_null_or_empty_string(), OB_INVALID_ARGUMENT);
    if (params.at(0).is_null_or_empty_string()) {
      client_info.reset();
    } else {
      OZ (params.at(0).get_string(client_info));
    }
    OZ (sess->set_client_info(client_info));
  }
  return ret;
}
// this is a procedure, and not need to return result
int ObDBMSAppInfo::set_module(sql::ObExecContext &ctx, sql::ParamStore &params, common::ObObj &result)
{
  int ret = OB_SUCCESS;
  ObString module_name;
  ObString action_name;
  UNUSED(result);
  CK (OB_NOT_NULL(ctx.get_my_session()));
  ObSQLSessionInfo* sess = const_cast<ObSQLSessionInfo*>(ctx.get_my_session());
  if (OB_FAIL(ret)) {
    // do nothing
  } else {
    CK (OB_LIKELY(2 == params.count()));
    OV (params.at(0).is_varchar() || params.at(0).is_null_or_empty_string(), OB_INVALID_ARGUMENT);
    if (params.at(0).is_null_or_empty_string()) {
      module_name.reset();
    } else {
      OZ (params.at(0).get_string(module_name));
    }
    OV (params.at(1).is_varchar() || params.at(1).is_null_or_empty_string(), OB_INVALID_ARGUMENT);
    if (params.at(1).is_null_or_empty_string()) {
      action_name.reset();
    } else {   
      OZ (params.at(1).get_string(action_name));
    }
    OZ (sess->set_module_name(module_name));
    OZ (sess->set_action_name(action_name));
  }
  return ret;
}
} // end of pl
} // end oceanbase
