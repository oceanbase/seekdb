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

#include "lib/mysqlclient/ob_isql_client.h"
#include "lib/mysqlclient/ob_isql_result_handler.h"
#include "lib/mysqlclient/ob_mysql_result.h"

namespace oceanbase
{
namespace common
{

using namespace sqlclient;


ObMySQLResult *ObISQLClient::ReadResult::mysql_result()
{
  ObMySQLResult *res = NULL;
  if (NULL != result_handler_) {
    res = result_handler_->mysql_result();
  }
  return res;
}

int ObISQLClient::ReadResult::close()
{
  int ret = OB_SUCCESS;
  if (get_result() != NULL) {
    ret = get_result()->close();
  }
  return ret;
}

ObISQLClient::ReadResult::ReadResult()
    : result_handler_(NULL),
      enable_use_result_(false)
{
}

ObISQLClient::ReadResult::~ReadResult()
{
  reset();
}

void ObISQLClient::ReadResult::reset()
{
  if (NULL != result_handler_) {
    result_handler_->~ObISQLResultHandler();
    result_handler_ = NULL;
    enable_use_result_ = false;
  }
}

void ObISQLClient::ReadResult::reuse()
{
  if (NULL != result_handler_) {
    result_handler_->~ObISQLResultHandler();
    result_handler_ = NULL;
    enable_use_result_ = false;
  }
}

void ObISQLClient::set_inactive()
{
  active_ = false;
  if (NULL != get_pool()) {
    int ret = get_pool()->on_client_inactive(this);
    if (OB_FAIL(ret)) {
      COMMON_LOG(WARN, "connection pool on client inactive failed", K(ret));
    }
  }
}

} // end namespace common
} // end namespace oceanbase
