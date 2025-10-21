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

#ifndef OCEANBASE_SQL_OB_ANALYZE_EXECUTOR_H_
#define OCEANBASE_SQL_OB_ANALYZE_EXECUTOR_H_
#include "lib/string/ob_sql_string.h"
#include "sql/resolver/ddl/ob_analyze_stmt.h"
#include "share/stat/ob_stat_define.h"
namespace oceanbase
{
namespace share
{
namespace schema
{
class ObTableSchema;
}
}
namespace obrpc
{
struct ObUpdateStatCacheArg;
}
namespace sql
{
class ObExecContext;
class ObAnalyzeStmt;
class ObAnalyzeExecutor
{
public:
  static const int64_t FIXED_HISTOGRAM_SEED = 1;
  static const int64_t BUCKET_BITS = 10; // ln2(1024) = 10;
  static const int64_t TOTAL_BUCKET_BITS = 40; // 6 groups
  static const int64_t NUM_LLC_BUCKET =  (1 << BUCKET_BITS);

public:
  ObAnalyzeExecutor() {}
  virtual ~ObAnalyzeExecutor() {}
  int execute(ObExecContext &ctx, ObAnalyzeStmt &stmt);
  DISALLOW_COPY_AND_ASSIGN(ObAnalyzeExecutor);
};

} /* namespace sql */
} /* namespace oceanbase */

#endif /* SRC_SQL_ENGINE_CMD_OB_ANALYZE_EXECUTOR_H_ */
