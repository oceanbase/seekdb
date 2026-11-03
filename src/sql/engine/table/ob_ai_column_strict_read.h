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

// OB-P:a4L9E_IDIyEM2Ol_ZvDXqw

#pragma once

#include "common/ob_common_types.h"
#include "share/schema/ob_col_desc.h"
#include "share/schema/ob_column_schema.h"

namespace oceanbase
{
namespace sql
{
class ObExecContext;

// The strict read check of the asynchronous AI columns.
//
// When the session variable ai_column_strict_read is enabled, a statement
// that reads an AI result column fails with OB_ERR_AI_COLUMN_NOT_READY when
// any row of the column is not READY.  The check is column wide: it does not
// matter whether the rows that are not ready are selected by the query.
// The check runs when the table scan operator opens, so that it also covers
// the executions that reuse a plan from the plan cache.
class ObAiColumnStrictRead
{
public:
  // Checks the AI result columns that are accessed by one table scan of the
  // table `table_id`.  `columns` are the column descriptions of the scan.
  static int check_table_scan(ObExecContext &ctx,
                              uint64_t table_id,
                              const common::ObIArray<share::schema::ObColDesc> &columns);
};

} // namespace sql
} // namespace oceanbase
