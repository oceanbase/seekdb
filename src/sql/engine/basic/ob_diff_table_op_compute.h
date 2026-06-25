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

#ifndef OCEANBASE_SQL_ENGINE_BASIC_OB_DIFF_TABLE_OP_COMPUTE_H_
#define OCEANBASE_SQL_ENGINE_BASIC_OB_DIFF_TABLE_OP_COMPUTE_H_

#include "lib/ob_define.h"
#include "lib/container/ob_iarray.h"

namespace oceanbase
{
namespace common
{
class ObIAllocator;
class ObNewRow;
}
namespace share
{
namespace schema
{
class ObSchemaGetterGuard;
}
}
namespace sql
{
class ObDiffTableStmt;
class ObSQLSessionInfo;

// Compute-helper for DIFF TABLE. Stateless. Called from ObDiffTableOp
// at execute time. Writes rendered output rows into `out_rows` (deep
// copied into `alloc`). The caller owns out_rows lifecycle.
class ObDiffTableOpCompute
{
public:
  static int compute_diff_rows(
      const ObDiffTableStmt &param,
      share::schema::ObSchemaGetterGuard &schema_guard,
      ObSQLSessionInfo &session,
      common::ObIAllocator &alloc,
      common::ObIArray<common::ObNewRow *> &out_rows);
};

} // namespace sql
} // namespace oceanbase
#endif
