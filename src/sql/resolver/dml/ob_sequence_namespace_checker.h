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

#ifndef OCEANBASE_SRC_SQL_RESOLVER_DML_OB_SEQUENCE_NAMESPACE_CHECKER_H_
#define OCEANBASE_SRC_SQL_RESOLVER_DML_OB_SEQUENCE_NAMESPACE_CHECKER_H_
#include "share/ob_define.h"
#include "lib/container/ob_array.h"
#include "lib/string/ob_string.h"
namespace oceanbase
{
namespace sql
{
struct ObResolverParams;
class ObDMLStmt;
class ObSQLSessionInfo;
class ObSchemaChecker;
struct ObQualifiedName;
class ObSequenceNamespaceChecker
{
public:
  explicit ObSequenceNamespaceChecker(ObResolverParams &resolver_params);
  explicit ObSequenceNamespaceChecker(const ObSchemaChecker *schema_checker,
                                      const ObSQLSessionInfo *session_info);
  ~ObSequenceNamespaceChecker() {};
  int check_sequence_namespace(const ObQualifiedName &q_name,
                               uint64_t &sequence_id);
  int check_sequence_namespace(const ObQualifiedName &q_name,
                               const ObSQLSessionInfo *session_info,
                               const ObSchemaChecker *schema_checker,
                               uint64_t &sequence_id);
  inline static bool is_curr_or_next_val(const common::ObString &s)
  {
    return 0 == s.case_compare("nextval")  || 0 == s.case_compare("currval");
  }
private:
  int check_sequence_with_synonym_recursively(const uint64_t tenant_id,
                                              const uint64_t database_id,
                                              const common::ObString &sequence_name,
                                              const ObSchemaChecker *schema_checker,
                                              bool &exists,
                                              uint64_t &sequence_id);
  const ObSchemaChecker *schema_checker_;
  const ObSQLSessionInfo *session_info_;
};
}  // namespace sql
}  // namespace oceanbase
#endif /* OCEANBASE_SRC_SQL_RESOLVER_DML_OB_SEQUENCE_NAMESPACE_CHECKER_H_ */
