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

#ifndef OCEANBASE_SQL_TABLE_LOAD_CONTEXT_SERVICE_H_
#define OCEANBASE_SQL_TABLE_LOAD_CONTEXT_SERVICE_H_

namespace oceanbase
{
namespace observer
{

struct ObTableLoadUniqueKey;
class ObTableLoadTableCtx;

int get_table_load_ctx(const ObTableLoadUniqueKey &key, ObTableLoadTableCtx *&table_ctx);
void put_table_load_ctx(ObTableLoadTableCtx *table_ctx);

} // namespace observer
} // namespace oceanbase

#endif // OCEANBASE_SQL_TABLE_LOAD_CONTEXT_SERVICE_H_
