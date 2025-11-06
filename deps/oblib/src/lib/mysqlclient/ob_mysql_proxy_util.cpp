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

#define USING_LOG_PREFIX COMMON_MYSQLP

#include "lib/ob_define.h"
#include "lib/mysqlclient/ob_mysql_proxy_util.h"
#include "lib/mysqlclient/ob_isql_connection_pool.h"

using namespace oceanbase::common;
using namespace oceanbase::common::sqlclient;

ObMySQLProxyUtil::ObMySQLProxyUtil() : pool_(NULL)
{
}

ObMySQLProxyUtil::~ObMySQLProxyUtil()
{
}



