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

#ifndef OCEANBASE_OBSERVER_OB_TABLE_QUERY_COMMON_H_
#define OCEANBASE_OBSERVER_OB_TABLE_QUERY_COMMON_H_

#include "ob_table_filter.h"
#include "ob_table_context.h"

namespace oceanbase
{
namespace table
{

class ObTableQueryUtils
{
public:
  static int generate_query_result_iterator(ObIAllocator &allocator,
                                            const ObTableQuery &query,
                                            bool is_hkv,
                                            ObTableQueryResult &one_result,
                                            const ObTableCtx &tb_ctx,
                                            ObTableQueryResultIterator *&result_iter);

  template<typename ResultType>
  static int generate_htable_result_iterator(ObIAllocator &allocator,
                                             const ObTableQuery &query,
                                             ResultType &one_result,
                                             const ObTableCtx &tb_ctx,
                                             ObTableQueryResultIterator *&result_iter);
  static void destroy_result_iterator(ObTableQueryResultIterator *&result_iter);
  static int get_rowkey_column_names(ObKvSchemaCacheGuard &schema_cache_guard, ObIArray<ObString> &names);
  static int get_scan_row_interator(const ObTableCtx &tb_ctx, ObTableApiScanRowIterator *&scan_iter);

  static int get_table_schemas(ObMultiVersionSchemaService *schema_service,
                                ObSchemaGetterGuard& schema_guard,
                                const ObString &arg_table_name,
                                bool is_tablegroup_name,
                                uint64_t arg_tenant_id,
                                uint64_t arg_database_id,
                                common::ObIArray<const schema::ObSimpleTableSchemaV2*> &table_schemas);
  static int get_table_schemas(ObSchemaGetterGuard& schema_guard,
                               const ObString &arg_table_name,
                               bool is_tablegroup_name,
                               uint64_t arg_tenant_id,
                               uint64_t arg_database_id,
                               common::ObIArray<const schema::ObSimpleTableSchemaV2*> &table_schemas);

private:
  static int check_htable_query_args(const ObTableQuery &query, const ObTableCtx &tb_ctx);
private:
  DISALLOW_COPY_AND_ASSIGN(ObTableQueryUtils);
};

} // end namespace table
} // end namespace oceanbase

#endif /* OCEANBASE_OBSERVER_OB_TABLE_QUERY_COMMON_H_ */
