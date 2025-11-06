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

#include "share/schema/ob_mview_refresh_stats.h"
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
struct ObMViewStatsPurgeRefreshStatsArg
{
public:
  ObMViewStatsPurgeRefreshStatsArg() : retention_period_(INT64_MAX) {}
  bool is_valid() const { return true; }
  TO_STRING_KV(K_(mv_list), K_(retention_period));

public:
  ObString mv_list_;
  int64_t retention_period_;
};

class ObMViewStatsPurgeRefreshStatsExecutor
{
public:
  ObMViewStatsPurgeRefreshStatsExecutor();
  ~ObMViewStatsPurgeRefreshStatsExecutor();
  DISABLE_COPY_ASSIGN(ObMViewStatsPurgeRefreshStatsExecutor);

  int execute(sql::ObExecContext &ctx, const ObMViewStatsPurgeRefreshStatsArg &arg);

private:
  int resolve_arg(const ObMViewStatsPurgeRefreshStatsArg &arg);
  int purge_refresh_stats(const share::schema::ObMViewRefreshStats::FilterParam &filter_param);

private:
  static const int64_t PURGE_BATCH_COUNT = 1000;
  enum class OpType
  {
    PURGE_ALL_REFRESH_STATS = 0,
    PURGE_SPECIFY_REFRESH_STATS = 1,
    MAX
  };

private:
  sql::ObExecContext *ctx_;
  sql::ObSQLSessionInfo *session_info_;
  sql::ObSchemaChecker schema_checker_;

  uint64_t tenant_id_;
  OpType op_type_;
  ObArray<uint64_t> mview_ids_;
  int64_t retention_period_;
};

} // namespace storage
} // namespace oceanbase
