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

#ifndef OCEANBASE_SQL_EXECUTOR_OB_CMD_EXECUTOR_
#define OCEANBASE_SQL_EXECUTOR_OB_CMD_EXECUTOR_

#include "share/ob_define.h"

namespace oceanbase
{
namespace sql
{
class ObICmd;
class ObExecContext;
class ObCmdExecutor
{
public:
  static int execute(ObExecContext &ctx, ObICmd &cmd);
private:
  /* functions */
  /* variables */
  DISALLOW_COPY_AND_ASSIGN(ObCmdExecutor);
};
}
}
#endif /* OCEANBASE_SQL_EXECUTOR_OB_CMD_EXECUTOR_ */
//// end of header file
