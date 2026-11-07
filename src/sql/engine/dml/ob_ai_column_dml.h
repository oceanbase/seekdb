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
#include "sql/engine/dml/ob_dml_ctx_define.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObTableSchema;
}
}
}

namespace oceanbase
{
namespace sql
{

class ObTableModifyOp;

// True when the table has at least one AI result column.
bool table_has_ai_column(const share::schema::ObTableSchema &table_schema);

// Row level AI column maintenance.  Called by the DML operators after a row
// has been written to DAS.  The invalidation UPDATE and the task INSERT run in
// a nested SQL session that shares the outer transaction, so a rollback also
// removes the task rows.
class ObAiColumnDml
{
public:
  static int process_insert(ObTableModifyOp &op, const ObInsCtDef &ctdef);
  static int process_update(ObTableModifyOp &op, const ObUpdCtDef &ctdef);
  static int process_delete(ObTableModifyOp &op, const ObDelCtDef &ctdef);

private:
  enum DmlEvent
  {
    AI_DML_INSERT = 0,
    AI_DML_UPDATE = 1,
    AI_DML_DELETE = 2
  };
  static int process(ObTableModifyOp &op,
                     const ObDMLBaseCtDef &ctdef,
                     const DmlEvent event);
};

} // namespace sql
} // namespace oceanbase
