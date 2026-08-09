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
#include "ob_dbms_session.h"
#include "pl/ob_pl.h"

namespace oceanbase
{
namespace pl
{

int ObDBMSSession::clear_identifier(sql::ObExecContext &ctx,
                                    sql::ParamStore &params,
                                    common::ObObj &result)
{
  int ret = OB_SUCCESS;
  sql::ObSQLSessionInfo *session = ctx.get_my_session();
  ObString client_id = "";
  if (OB_UNLIKELY(OB_ISNULL(session))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session info is nullptr", K(ret));
  } else if (OB_UNLIKELY(0 != params.count())) {
    ObString func_name("CLEAR_IDENTIFIER");
    ret = OB_ERR_WRONG_FUNC_ARGUMENTS_TYPE;
    LOG_USER_ERROR(OB_ERR_WRONG_FUNC_ARGUMENTS_TYPE, func_name.length(), func_name.ptr());
  } else if (OB_FAIL(session->set_client_id(client_id))) {
  }
  return ret;
}

int ObDBMSSession::set_identifier(sql::ObExecContext &ctx,
                                  sql::ParamStore &params,
                                  common::ObObj &result)
{
  int ret = OB_SUCCESS;
  sql::ObSQLSessionInfo *session = ctx.get_my_session();
  ObString client_id;
  if (OB_UNLIKELY(OB_ISNULL(session))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session info is nullptr", K(ret));
  } else if (OB_UNLIKELY(1 != params.count())) {
    ObString func_name("SET_IDENTIFIER");
    ret = OB_ERR_WRONG_FUNC_ARGUMENTS_TYPE;
    LOG_USER_ERROR(OB_ERR_WRONG_FUNC_ARGUMENTS_TYPE, func_name.length(), func_name.ptr());
  } else if (params.at(0).is_null()) {
    client_id = ObString("");
  } else if (!params.at(0).is_varchar()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get wrong param in set identifier", K(ret), K(params.at(0)));
  } else if (OB_FAIL(params.at(0).get_varchar(client_id))) {
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(session->set_client_id(client_id))) {
  }

  return ret;
}

int ObDBMSSession::reset_package(sql::ObExecContext &ctx,
                                  sql::ParamStore &params,
                                  common::ObObj &result)
{
  int ret = OB_SUCCESS;
  sql::ObSQLSessionInfo *session = ctx.get_my_session();
  ObPLContext *pl_ctx = nullptr;
  ObString client_id;
  if (OB_UNLIKELY(OB_ISNULL(session))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session info is nullptr", K(ret));
  } else if (OB_UNLIKELY(0 != params.count())) {
    ObString func_name("RESET_PACKAGE");
    ret = OB_ERR_WRONG_FUNC_ARGUMENTS_TYPE;
    LOG_USER_ERROR(OB_ERR_WRONG_FUNC_ARGUMENTS_TYPE, func_name.length(), func_name.ptr());
  } else {
    session->set_need_reset_package(true);
  }
  return ret;
}

} // end of pl
} // end oceanbase
