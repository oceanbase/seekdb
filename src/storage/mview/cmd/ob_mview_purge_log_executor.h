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

#include "share/schema/ob_schema_struct.h"
#include "sql/resolver/ob_schema_checker.h"

namespace oceanbase
{
namespace sql
{
class ObExecContext;
} // namespace sql
namespace storage
{
struct ObMViewPurgeLogArg
{
public:
  ObMViewPurgeLogArg() : num_(-1), purge_log_parallel_(0) {}
  bool is_valid() const { return !master_.empty(); }
  TO_STRING_KV(K_(master), K_(num), K_(flag), K_(purge_log_parallel));

public:
  ObString master_;
  int64_t num_;
  ObString flag_;
  int64_t purge_log_parallel_;
};

class ObMViewPurgeLogExecutor
{
public:
  ObMViewPurgeLogExecutor();
  ~ObMViewPurgeLogExecutor();
  DISABLE_COPY_ASSIGN(ObMViewPurgeLogExecutor);

  int execute(sql::ObExecContext &ctx, const ObMViewPurgeLogArg &arg);

private:
  int resolve_arg(const ObMViewPurgeLogArg &arg);

private:
  sql::ObExecContext *ctx_;
  sql::ObSQLSessionInfo *session_info_;
  sql::ObSchemaChecker schema_checker_;

  uint64_t tenant_id_;
  uint64_t master_table_id_;
};

} // namespace storage
} // namespace oceanbase
