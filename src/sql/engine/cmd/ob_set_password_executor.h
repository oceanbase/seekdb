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

#ifndef OCEANBASE_SQL_SET_PASSWORD_EXECUTOR_
#define OCEANBASE_SQL_SET_PASSWORD_EXECUTOR_

namespace oceanbase
{
namespace common
{
class ObString;
class ObSqlString;
}
namespace obrpc
{
class ObCommonRpcProxy;
}

namespace sql
{
class ObExecContext;
class ObSetPasswordStmt;

class ObSetPasswordExecutor
{
public:
  ObSetPasswordExecutor();
  virtual ~ObSetPasswordExecutor();
  int execute(ObExecContext &ctx, ObSetPasswordStmt &stmt);
};

}
}
#endif /* __OB_SQL_SET_PASSWORD_EXECUTOR_H__ */
//// end of header file

