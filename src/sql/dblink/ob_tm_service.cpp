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
