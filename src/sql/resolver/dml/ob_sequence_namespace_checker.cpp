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

#define USING_LOG_PREFIX SQL_RESV
#include "sql/resolver/dml/ob_sequence_namespace_checker.h"
#include "sql/resolver/ob_resolver_utils.h"
#include "sql/resolver/ob_stmt_resolver.h"


namespace oceanbase
{
using namespace common;
using namespace common::sqlclient;
namespace sql
{

ObSequenceNamespaceChecker::ObSequenceNamespaceChecker(ObResolverParams &resolver_params)
    : schema_checker_(resolver_params.schema_checker_),
      session_info_(resolver_params.session_info_)
{}

ObSequenceNamespaceChecker::ObSequenceNamespaceChecker(const ObSchemaChecker *schema_checker,
                                                       const ObSQLSessionInfo *session_info)
  : schema_checker_(schema_checker), session_info_(session_info)
{}
int ObSequenceNamespaceChecker::check_sequence_namespace(const ObQualifiedName &q_name,
                                                        uint64_t &sequence_id)
{
  return check_sequence_namespace(q_name,
                                  session_info_, 
                                  schema_checker_, 
                                  sequence_id);
}

int ObSequenceNamespaceChecker::check_sequence_namespace(const ObQualifiedName &q_name,
                                                         const ObSQLSessionInfo *session_info,
                                                         const ObSchemaChecker *schema_checker,
                                                         uint64_t &sequence_id)
{
  int ret = OB_NOT_IMPLEMENT;
  const ObString &sequence_name = q_name.tbl_name_;
  const ObString &sequence_expr = q_name.col_name_;
  if (OB_ISNULL(schema_checker) || OB_ISNULL(session_info)) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is null", K(ret));
  } else if (!is_curr_or_next_val(q_name.col_name_)) {
    ret = OB_ERR_BAD_FIELD_ERROR;
  } else if (q_name.database_name_.empty() && session_info->get_database_name().empty()) {
    ret = OB_ERR_NO_DB_SELECTED;
    LOG_WARN("No database selected", K(q_name), K(ret));
  } else if (0 != sequence_expr.case_compare("NEXTVAL") &&
             0 != sequence_expr.case_compare("CURRVAL")) {
    ret = OB_ERR_BAD_FIELD_ERROR;
  } else if (q_name.dblink_name_.empty()) {
    const ObString &database_name = q_name.database_name_.empty() ?
        session_info->get_database_name() : q_name.database_name_;
    uint64_t tenant_id = session_info->get_effective_tenant_id();
    uint64_t database_id = OB_INVALID_ID;
    bool exist = false;
    if (OB_FAIL(schema_checker->get_database_id(tenant_id, database_name, database_id))) {
      LOG_WARN("failed to get database id", K(ret), K(tenant_id), K(database_name));
    } else if (OB_FAIL(check_sequence_with_synonym_recursively(tenant_id, database_id, sequence_name,
                                                               schema_checker, exist, sequence_id))) {
      LOG_WARN("fail recursively check sequence with name", K(q_name), K(database_name), K(ret));
    } else if (!exist) {
      ret = OB_ERR_BAD_FIELD_ERROR;
    }
  } else {
    // has dblink_name_, not support
    ret = OB_NOT_IMPLEMENT;
  }
  return ret;
}

int ObSequenceNamespaceChecker::check_sequence_with_synonym_recursively(const uint64_t tenant_id,
                                                                        const uint64_t database_id,
                                                                        const common::ObString &sequence_name,
                                                                        const ObSchemaChecker *schema_checker,
                                                                        bool &exists,
                                                                        uint64_t &sequence_id)
{
  int ret = OB_SUCCESS;
  bool exist_with_synonym = false;
  ObString object_seq_name;
  uint64_t object_db_id;
  uint64_t synonym_id;
  if (OB_FAIL(schema_checker->check_sequence_exist_with_name(tenant_id,
                                                                      database_id,
                                                                      sequence_name,
                                                                      exists,
                                                                      sequence_id))) {
    LOG_WARN("failed to check sequence with name", K(ret), K(sequence_name), K(database_id));
  } else if (!exists) { // check synonym
  }
  return ret;
}

}  // namespace sql
}  // namespace oceanbase
