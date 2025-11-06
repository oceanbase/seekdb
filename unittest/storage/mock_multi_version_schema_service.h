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

#ifndef MOCK_MV_SCHEMA_SERVICE_H_
#define MOCK_MV_SCHEMA_SERVICE_H_

#include <gmock/gmock.h>
#include "share/schema/ob_multi_version_schema_service.h"
#include "sql/resolver/ob_stmt.h"

namespace oceanbase
{
namespace storage
{

class MockMultiVersionSchemaService : public share::schema::ObMultiVersionSchemaService
{
public:
  MockMultiVersionSchemaService()
  {
    schema_manager_ = new share::schema::ObSchemaManager();
  }
  virtual ~MockMultiVersionSchemaService()
  {
    delete schema_manager_;
  }
  int parse_from_file(const char *path);
  virtual const share::schema::ObSchemaManager *get_user_schema_manager(const int64_t version);
private:
  void do_create_table(common::ObArenaAllocator &allocator,
                       share::schema::ObSchemaManager *schema_mgr,
                       const char *query_str,
                       uint64_t table_id);
  void do_resolve(common::ObArenaAllocator &allocator,
                  share::schema::ObSchemaManager *schema_mgr,
                  const char *query_str,
                  sql::ObStmt *&stmt);
  share::schema::ObSchemaManager *schema_manager_;
};
}// namespace storage
}// namespace oceanbase

#endif /* MOCK_OB_PARTITION_REPORT_H_ */
