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

#ifndef OCEANBASE_SHARE_SCHEMA_OB_SEQUENCE_SQL_SERVICE_H_
#define OCEANBASE_SHARE_SCHEMA_OB_SEQUENCE_SQL_SERVICE_H_

#include "ob_ddl_sql_service.h"
#include "lib/number/ob_number_v2.h"
#include "share/ob_rpc_struct.h"

using namespace oceanbase::common::number;

namespace oceanbase
{
namespace common
{
class ObString;
class ObISQLClient;
}
namespace share
{
namespace schema
{
class ObSequenceSchema;

class ObSequenceSqlService : public ObDDLSqlService
{
public:
  ObSequenceSqlService(ObSchemaService &schema_service)
    : ObDDLSqlService(schema_service) {}
  virtual ~ObSequenceSqlService() {}

  virtual int insert_sequence(const ObSequenceSchema &sequence_schema,
                              common::ObISQLClient *sql_client,
                              const common::ObString *ddl_stmt_str,
                              const uint64_t *old_sequence_id);
  virtual int replace_sequence(const ObSequenceSchema &sequence_schema,
                               const bool is_rename,
                               common::ObISQLClient *sql_client,
                               bool alter_start_with,
                               bool need_clean_cache,
                               bool need_write_back,
                               const common::ObString *ddl_stmt_str = NULL);
  virtual int delete_sequence(const uint64_t tenant_id,
                              const uint64_t database_id,
                              const uint64_t sequence_id,
                              const int64_t new_schema_version,
                              common::ObISQLClient *sql_client,
                              const common::ObString *ddl_stmt_str = NULL);
  virtual int drop_sequence(const ObSequenceSchema &sequence_schema,
                            const int64_t new_schema_version,
                            common::ObISQLClient *sql_client,
                            const common::ObString *ddl_stmt_str = NULL);
  int alter_sequence_start_with(const ObSequenceSchema &sequence_schema,
                                common::ObISQLClient &sql_client);
  int get_sequence_sync_value(const uint64_t tenant_id,
                              const uint64_t sequence_id,
                              const bool is_for_update,
                              common::ObISQLClient &sql_client,
                              ObIAllocator &allocator,
                              common::number::ObNumber &next_value);
private:
  int clean_and_write_back_cache(common::ObISQLClient *sql_client,
                                 const ObSequenceSchema &sequence_schema, bool &need_write_back,
                                 ObIAllocator &allocator);
  int add_sequence(common::ObISQLClient &sql_client, const ObSequenceSchema &sequence_schema,
                   const bool only_history, const uint64_t *old_sequence_id);
  int add_sequence_to_value_table(const uint64_t tenant_id,
                                  const uint64_t exec_tenant_id,
                                  const uint64_t old_sequence_id,
                                  const uint64_t new_sequence_id,
                                  common::ObISQLClient &sql_client,
                                  ObIAllocator &allocator);
  int clean_sequence_cache(uint64_t tenant_id, uint64_t sequence_id, ObNumber &inner_next_value,
                           obrpc::ObSeqCleanCacheRes &cache_res, ObIAllocator &allocator);

  int get_lastest_local_cache(ObFixedArray<SequenceCacheNode, common::ObIAllocator> &prefetch_nodes,
                             const SequenceCacheNode &target_cache_node, const ObNumber &inner_next_value,
                             obrpc::ObSeqCleanCacheRes &cache_res, ObIAllocator &allocator);

private:
  DISALLOW_COPY_AND_ASSIGN(ObSequenceSqlService);
};

} //end of namespace schema
} //end of namespace share
} //end of namespace oceanbase

#endif //OCEANBASE_SHARE_SCHEMA_OB_SEQUENCE_SQL_SERVICE_H_
