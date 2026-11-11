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

#ifndef OCEANBASE_SQL_ENGINE_CMD_OB_AI_COLUMN_UTIL_H_
#define OCEANBASE_SQL_ENGINE_CMD_OB_AI_COLUMN_UTIL_H_

#include <stdint.h>

namespace oceanbase
{
namespace common
{
class ObIAllocator;
class ObMySQLProxy;
namespace sqlclient
{
class ObISQLConnection;
}
}
namespace sql
{
namespace ai_column
{

// Every table that carries at least one AI column is remembered here, so the
// DML path only pays one hash lookup. A miss triggers a single catalogue scan.
bool table_has_ai_column(const uint64_t table_id);
int add_ai_column_table(const uint64_t table_id);

// Keep __all_ai_column_task in step with the base table, inside the caller's
// transaction: queue every row whose AI result is missing or pending, and drop
// the tasks of rows that no longer exist.
int maintain_tasks(common::sqlclient::ObISQLConnection *conn,
                   common::ObMySQLProxy *proxy,
                   common::ObIAllocator &alloc,
                   const uint64_t table_id);

// True while the AI column still has queued work or a recorded failure, i.e.
// while a strict read has to reject every statement that touches it.
int has_unfinished_task(common::ObMySQLProxy &proxy,
                        const uint64_t table_id,
                        const uint64_t column_id,
                        bool &unfinished);

}  // namespace ai_column
}  // namespace sql
}  // namespace oceanbase

#endif  // OCEANBASE_SQL_ENGINE_CMD_OB_AI_COLUMN_UTIL_H_
