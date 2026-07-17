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

#ifndef OCEANBASE_SHARE_OB_MOCK_SCHEMA_FETCHER_H_
#define OCEANBASE_SHARE_OB_MOCK_SCHEMA_FETCHER_H_

#include "share/schema/ob_schema_cache.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class MockObSchemaFetcher : public ObSchemaFetcher
{
public:
  MOCK_METHOD2(init, int(ObSchemaService *, common::ObISQLClient *));
  MOCK_METHOD5(fetch_schema, int(ObSchemaType, uint64_t, int64_t,
      common::ObIAllocator &, ObSchema *&));
  MOCK_METHOD4(fetch_tenant_schema, int(uint64_t, int64_t,
      common::ObIAllocator &, ObTenantSchema *&));
  MOCK_METHOD4(fetch_user_info, int(uint64_t, int64_t,
      common::ObIAllocator &, ObUserInfo *&));
  MOCK_METHOD4(fetch_database_schema, int(uint64_t, int64_t,
      common::ObIAllocator &, ObDatabaseSchema *&));
  MOCK_METHOD4(fetch_tablegroup_schema, int(uint64_t, int64_t,
      common::ObIAllocator &, ObTablegroupSchema *&));
  MOCK_METHOD4(fetch_table_schema, int(uint64_t, int64_t,
       common::ObIAllocator &, ObTableSchema *&));
  virtual ~MockObSchemaFetcher() {}
};
}//end namespace schema
}//end namespace share
}//end namespace oceanbase

#endif
