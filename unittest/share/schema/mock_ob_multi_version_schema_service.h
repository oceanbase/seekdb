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

#ifndef MOCK_OB_MULTI_VERSION_SCHEMA_SERVICE_H_
#define MOCK_OB_MULTI_VERSION_SCHEMA_SERVICE_H_

#include <gmock/gmock.h>
#define private public
#include "share/schema/ob_multi_version_schema_service.h"
#include "mock_schema_fetcher.h"
#include "share/ob_rpc_struct.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/cache/ob_kv_storecache.h"
#include "share/ob_define.h"
namespace oceanbase
{
namespace share
{
namespace schema
{
class MockObMultiVersionSchemaService : public ObMultiVersionSchemaService
{
public:
  MockObMultiVersionSchemaService() {}
  virtual ~MockObMultiVersionSchemaService() {}
  MOCK_METHOD2(check_table_exist,
      int(const uint64_t table_id, bool &exist));
  //MOCK_METHOD(check_inner_stat, bool());
  int init();
  int get_schema_guard(ObSchemaGetterGuard &guard, int64_t snapshot_version = common::OB_INVALID_VERSION);
  int add_table_schema(ObTableSchema &table_schema, int64_t schema_version);
  int add_database_schema(ObDatabaseSchema &database_schema, int64_t schema_version);
protected:
  MockObSchemaFetcher mock_schema_fetcher_;
};
}
}
}
#include "mock_ob_multi_version_schema_service.ipp"



#endif /* MOCK_OB_MULTI_VERSION_SCHEMA_SERVICE_H_ */
