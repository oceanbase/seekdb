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

#ifndef OCEANBASE_SRC_SQL_RESOLVER_DDL_OB_CREATE_ROUTINE_RESOLVER_H_
#define OCEANBASE_SRC_SQL_RESOLVER_DDL_OB_CREATE_ROUTINE_RESOLVER_H_

#include "sql/resolver/ddl/ob_ddl_resolver.h"
#include "share/ob_rpc_struct.h"
#include "share/schema/ob_schema_struct.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObRoutineParam;
}
}
namespace sql
{
class ObCreateRoutineResolver: public ObDDLResolver
{
public:
  static const int64_t ROUTINE_STANDALONE_OVERLOAD = 0;
  static const int64_t ROUTINE_STANDALONE_SUBPROGRAM_ID = 0;
  static const int64_t ROUTINE_RET_TYPE_POSITION = 0;
  static const int64_t ROUTINE_RET_TYPE_SEQUENCE = 1;
  explicit ObCreateRoutineResolver(ObResolverParams &params) : ObDDLResolver(params) {}

  int resolve(const peg::Node &parse_tree);

private:
  int resolve_impl(const peg::Node &command, obcall::ObCreateRoutineArg *crt_routine_arg);
  int resolve_sp_body(const peg::Node *parse_node, share::schema::ObRoutineInfo &routine_info);
  int check_dup_routine_param(const common::ObIArray<share::schema::ObRoutineParam*> &params,
                              const common::ObString &param_name);
  int create_routine_arg(obcall::ObCreateRoutineArg *&crt_routine_arg);
  int set_routine_info(const share::schema::ObRoutineType &type,
                       share::schema::ObRoutineInfo &routine_info);
  int analyze_router_sql(obcall::ObCreateRoutineArg *crt_routine_arg);
  int resolve_sp_definer(const peg::Node *parse_node, share::schema::ObRoutineInfo &routine_info);
  int resolve_sp_name(const peg::Node *parse_node, obcall::ObCreateRoutineArg *crt_routine_arg);
  int resolve_ret_type(const peg::Node *ret_type_node, share::schema::ObRoutineInfo &func_info);
  int resolve_param_list(rust::Slice<const peg::Node> param_list, obcall::ObCreateRoutineArg &crt_routine_arg);
  int resolve_param_type(const peg::Node *type_node, const common::ObString &param_name,
                         ObSQLSessionInfo &session_info, share::schema::ObRoutineParam &routine_param);
  int resolve_clause_list(rust::Slice<const peg::Node> clause_list, share::schema::ObRoutineInfo &func_info);
  int collect_ref_obj_info(int64_t ref_obj_id, int64_t ref_timestamp,
                           ObDependencyTableType dependent_type);
  int set_routine_param(const ObIArray<pl::ObObjAccessIdx> &access_idxs,
                        share::schema::ObRoutineParam &routine_param);
};

} // end namespace sql
} // end namespace oceanbase



#endif /* OCEANBASE_SRC_SQL_RESOLVER_DDL_OB_CREATE_ROUTINE_RESOLVER_H_ */
