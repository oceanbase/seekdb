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

#ifndef OCEANBASE_SQL_OB_TM_SERVICE_H
#define OCEANBASE_SQL_OB_TM_SERVICE_H

#include "sql/engine/ob_exec_context.h"
#include "storage/tx/ob_trans_define.h"

namespace oceanbase
{
using namespace transaction;
namespace sql
{
class ObTMService
{
public:
  static int tm_create_savepoint(ObExecContext &exec_ctx,
                                 const ObString &sp_name);
  static int tm_rollback_to_savepoint(ObExecContext &exec_ctx,
                                      const ObString &sp_name);
  // for callback link
  static int revert_tx_for_callback(ObExecContext &exec_ctx);
private:
  DISALLOW_COPY_AND_ASSIGN(ObTMService);
};

} // end of namespace sql
} // end of namespace oceanbase

#endif // OCEANBASE_SQL_OB_TM_SERVICE_H
