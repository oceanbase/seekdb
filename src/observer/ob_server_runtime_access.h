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

#ifndef OCEANBASE_OBSERVER_OB_SERVER_RUNTIME_ACCESS_H_
#define OCEANBASE_OBSERVER_OB_SERVER_RUNTIME_ACCESS_H_

namespace oceanbase
{
namespace sql
{
class ObSQLSessionMgr;
class ObSql;
}
namespace observer
{

// Observer-private access to objects owned by the process composition root.
// Cross-module consumers use tenant-bound owner interfaces instead.
sql::ObSQLSessionMgr *get_observer_sql_session_mgr();
sql::ObSql *get_observer_sql_engine();

} // namespace observer
} // namespace oceanbase

#endif // OCEANBASE_OBSERVER_OB_SERVER_RUNTIME_ACCESS_H_
