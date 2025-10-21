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

#ifndef __OB_SHARE_SCHEMA_GLOBAL_CONTEXT_OPERATOR_H__
#define __OB_SHARE_SCHEMA_GLOBAL_CONTEXT_OPERATOR_H__

#include "lib/utility/ob_macro_utils.h"
#include "lib/container/ob_bit_set.h"

namespace oceanbase
{
namespace common
{
class ObISQLClient;
class ObMySQLTransaction;
}
namespace share
{
namespace schema
{
class ObSchemaGetterGuard;
class ObContextSchema;
class ObMultiVersionSchemaService;
}

class ObGlobalContextOperator
{
public:
  ObGlobalContextOperator();
  virtual ~ObGlobalContextOperator();
  int clean_global_context(const common::ObIArray<uint64_t> &tenant_ids,
                           common::ObISQLClient &sql_proxy,
                           share::schema::ObMultiVersionSchemaService &schema_service);
  int delete_global_context(const uint64_t tenant_id,
                            const uint64_t context_id,
                            const ObString &attribute,
                            const ObString &client_id,
                            ObISQLClient &sql_proxy);
  int delete_global_contexts_by_id(const uint64_t tenant_id,
                                  const uint64_t context_id,
                                  ObISQLClient &sql_proxy);
  int delete_global_contexts_by_ids(const uint64_t tenant_id,
                                    const common::ObIArray<uint64_t> &context_ids,
                                    ObISQLClient &sql_proxy);
  int insert_update_context(const uint64_t tenant_id,
                            const uint64_t context_id,
                            const ObString &context_name,
                            const ObString &attribute,
                            const ObString &client_id,
                            const ObString &username,
                            const ObString &value,
                            ObISQLClient &sql_proxy);
  
private:
  /* functions */
  /* variables */
  DISALLOW_COPY_AND_ASSIGN(ObGlobalContextOperator);
};
}
}
#endif /* __OB_SHARE_SCHEMA_GLOBAL_CONTEXT_OPERATOR_H__ */
//// end of header file
