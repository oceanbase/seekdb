/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
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



