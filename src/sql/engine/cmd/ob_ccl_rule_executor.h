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

#ifndef OCEANBASE_SRC_SQL_ENGINE_CMD_OB_CCL_RULE_EXECUTOR_H_
#define OCEANBASE_SRC_SQL_ENGINE_CMD_OB_CCL_RULE_EXECUTOR_H_
#include "share/ob_define.h"
#include "share/schema/ob_schema_getter_guard.h"

namespace oceanbase
{
namespace sql
{
class ObExecContext;
class ObCreateCCLRuleStmt;
class ObDropCCLRuleStmt;

class ObCreateCCLRuleExecutor
{
public:
  ObCreateCCLRuleExecutor();
  virtual ~ObCreateCCLRuleExecutor();
  int execute(ObExecContext &ctx, ObCreateCCLRuleStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObCreateCCLRuleExecutor);
};

class ObDropCCLRuleExecutor
{
public:
  ObDropCCLRuleExecutor();
  virtual ~ObDropCCLRuleExecutor();
  int execute(ObExecContext &ctx, ObDropCCLRuleStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObDropCCLRuleExecutor);
};

}  // namespace sql
}  // namespace oceanbase
#endif /* OCEANBASE_SRC_SQL_ENGINE_CMD_OB_CCL_RULE_EXECUTOR_H_ */
