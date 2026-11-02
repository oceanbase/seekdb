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

#include "lib/string/ob_string.h"
#include "lib/container/ob_iarray.h"
#include "sql/resolver/ddl/ob_ai_column_def.h"

namespace oceanbase
{
namespace share
{
namespace schema { class ObTableSchema; }
}
namespace sql
{

// Persists AI COLUMN definitions into oceanbase.__all_ai_column right after the
// DDL statement succeeded. Runs in its own internal transaction, so a failed
// metadata write reports an error to the client instead of silently losing the
// definition.
//
// NOTE: always pass the freshly created/altered table schema when available.
// The default lookup re-queries __all_column through inner SQL, which deadlocks
// on the single request worker when called from inside a DDL executor.
class ObAiColumnDdlUtil
{
public:
  static int persist_ai_columns(const uint64_t table_id,
                                const common::ObIArray<ObAiColumnDef> &defs,
                                const share::schema::ObTableSchema *table_schema = NULL);
  // Removes every definition and pending task of a dropped table.
  static int drop_ai_columns(const uint64_t table_id);
};

} // namespace sql
} // namespace oceanbase
