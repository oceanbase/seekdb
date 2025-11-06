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

#pragma once

#include "sql/engine/ob_exec_context.h"
#include "lib/mysqlclient/ob_isql_client.h"

namespace oceanbase
{
namespace pl
{

class ObDBMSMViewMysql
{
public:
enum ObDBMSMViewRefreshParam
{
  MV_LIST = 0,
  METHOD = 1,
  REFRESH_PARALLEL = 2,
  NESTED = 3,
  NESTED_REFRESH_MODE = 4,
  MAX_PARAM
};

public:
  ObDBMSMViewMysql() {}
  virtual ~ObDBMSMViewMysql() {}

#define DECLARE_FUNC(func) \
  static int func(sql::ObExecContext &ctx, sql::ParamStore &params, common::ObObj &result);

  DECLARE_FUNC(purge_log);
  DECLARE_FUNC(refresh);

#undef DECLARE_FUNC

};

} // namespace pl
} // namespace oceanbase
