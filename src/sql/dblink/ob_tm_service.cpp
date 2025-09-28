// Copyright (c) 2021 OceanBase
// OceanBase is licensed under Mulan PubL v2.
// You can use this software according to the terms and conditions of the Mulan PubL v2.
// You may obtain a copy of Mulan PubL v2 at:
//          http://license.coscl.org.cn/MulanPubL-2.0
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
// See the Mulan PubL v2 for more details.

#include "sql/dblink/ob_tm_service.h"

#define USING_LOG_PREFIX SQL

namespace oceanbase
{
using namespace transaction;
using namespace common;
using namespace common::sqlclient;
using namespace share;

namespace sql
{




int ObTMService::tm_create_savepoint(ObExecContext &exec_ctx, const ObString &sp_name)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObTMService::tm_rollback_to_savepoint(ObExecContext &exec_ctx, const ObString &sp_name)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}


int ObTMService::revert_tx_for_callback(ObExecContext &exec_ctx)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

} // end of namespace sql
} // end of namespace oceanbase
