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

#ifndef OCEANBASE_SQL_ENGINE_OB_AI_COLUMN_TRIGGER_H_
#define OCEANBASE_SQL_ENGINE_OB_AI_COLUMN_TRIGGER_H_

#include "sql/engine/dml/ob_dml_base_op.h"
#include "sql/engine/ob_ai_column_scheduler.h"

namespace oceanbase
{
namespace sql
{

class ObAiColumnTrigger
{
public:
  ObAiColumnTrigger();
  virtual ~ObAiColumnTrigger();

  // Trigger AI column processing on INSERT
  static int on_insert(ObExecContext& ctx,
                       const share::schema::ObTableSchema& table_schema,
                       const common::ObNewRow& new_row);

  // Trigger AI column processing on UPDATE
  static int on_update(ObExecContext& ctx,
                       const share::schema::ObTableSchema& table_schema,
                       const common::ObNewRow& old_row,
                       const common::ObNewRow& new_row);

  // Check if row has AI columns that need processing
  static bool has_ai_columns(const share::schema::ObTableSchema& table_schema);

private:
  // Process single AI column
  static int process_ai_column(ObExecContext& ctx,
                               const share::schema::ObTableSchema& table_schema,
                               uint64_t column_id,
                               const common::ObObj& source_value,
                               uint64_t row_id);

  DISALLOW_COPY_AND_ASSIGN(ObAiColumnTrigger);
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_ENGINE_OB_AI_COLUMN_TRIGGER_H_
