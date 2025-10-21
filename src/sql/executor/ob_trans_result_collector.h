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

#ifndef SRC_SQL_EXECUTOR_OB_TRANS_RESULT_COLLECTOR_H_
#define SRC_SQL_EXECUTOR_OB_TRANS_RESULT_COLLECTOR_H_

#include "sql/engine/ob_exec_context.h"
#include "storage/tx/ob_trans_define.h"
#include "lib/allocator/ob_safe_arena.h"
#include "share/rpc/ob_batch_rpc.h"

namespace oceanbase
{
namespace sql
{

// 

/*
 *      /-->-- FORBIDDEN
 *     /
 *    /---------->----------\
 *   /                       \
 * SENT -->-- RUNNING -->-- FINISHED -->-- RETURNED
 *   \             \                        /
 *    \------>------\----------->----------/
 *
 */
enum ObTaskStatus
{
  TS_INVALID,
  TS_SENT,
  TS_RUNNING,
  TS_FINISHED,
  TS_FORBIDDEN,
  TS_NEED_WAIT_ABOVE,
  TS_RETURNED,
};

}  // namespace sql
}  // namespace oceanbase
#endif /* SRC_SQL_EXECUTOR_OB_TRANS_RESULT_COLLECTOR_H_ */
