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

#ifdef _WIN32
#define USING_LOG_PREFIX RPC
#endif
#include "ob_sql_request_operator.h"
#include "rpc/obmysql/ob_sql_sock_session.h"
#include "rpc/obmysql/ob_poc_sql_request_operator.h"

namespace oceanbase
{
using namespace obmysql;
namespace rpc
{
ObPocSqlRequestOperator global_poc_sql_req_operator;
ObISqlRequestOperator& ObSqlRequestOperator::get_operator()
{
  return global_poc_sql_req_operator;
}

void *ObSqlRequestOperator::get_sql_session(ObRequest* req)
{
  return (void *)(get_operator().get_sql_session(req));
}

void ObSqlSockDesc::clear_sql_session_info() {
  if (NULL != sock_desc_) {
    obmysql::ObSqlSockSession* sess = (obmysql::ObSqlSockSession *)sock_desc_;
    sess->clear_sql_session_info();
  }
}

ObSqlRequestOperator global_sql_req_operator;
}; // end namespace rpc
}; // end namespace oceanbase
