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

#include "lib/mysqlclient/ob_mysql_proxy.h"
#include "share/schema/ob_mview_refresh_stats.h"

namespace oceanbase
{
namespace storage
{
class ObMViewRefreshStatsPurgeUtil final
{
public:
  static int purge_refresh_stats(
    ObISQLClient &sql_client, uint64_t tenant_id,
    const share::schema::ObMViewRefreshStats::FilterParam &filter_param, int64_t &affected_rows,
    int64_t limit = -1);
};

} // namespace storage
} // namespace oceanbase
