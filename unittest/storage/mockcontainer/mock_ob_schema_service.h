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

#ifndef OCEANBASE_UNITTEST_MOCK_OB_SCHEMA_SERVICE_H_
#define OCEANBASE_UNITTEST_MOCK_OB_SCHEMA_SERVICE_H_

#include "ob_restore_schema.h"
#include "share/schema/ob_multi_version_schema_service.h"


#ifndef SCHEMA_VERSION
#define SCHEMA_VERSION 0LL
#endif

namespace oceanbase
{

using namespace common;
using namespace sql;

namespace share
{
namespace schema
{

class MockObSchemaService : public ObMultiVersionSchemaService
{
public:
  MockObSchemaService() : schema_guard_(NULL) {}
  ~MockObSchemaService() {}
  int init(const char *schema_file);
  //virtual const ObSchemaManager *get_schema_manager_by_version(const int64_t version = 0,
  //                                                             const bool for_merge = false);
  //virtual int release_schema(const ObSchemaManager *schema);
  //virtual int64_t get_latest_local_version(const bool core_schema_version = false) const;
  //virtual int64_t get_received_broadcast_version(const bool core_schema_version = false) const;
private:
  //virtual const ObSchemaManager *get_user_schema_manager(const int64_t version);
  virtual const ObSchemaGetterGuard *get_schema_guard(const int64_t version);
private:
  ObRestoreSchema restore_schema_;
  //ObSchemaManager *manager_;
  ObSchemaGetterGuard *schema_guard_;
};

} // schema
} // share
} // oceanbase

#endif
