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

#ifndef OCEANBASE_SRC_SQL_RESOLVER_DDL_OB_CALL_PROCEDURE_RESOLVER_H_
#define OCEANBASE_SRC_SQL_RESOLVER_DDL_OB_CALL_PROCEDURE_RESOLVER_H_

#include "sql/resolver/cmd/ob_cmd_resolver.h"
#include "lib/container/ob_se_array.h"
#include "share/ob_rpc_struct.h"
#include "share/schema/ob_schema_struct.h"
#include "pl/pl_cache/ob_pl_cache.h"
#include "pl/ob_pl_stmt.h"
namespace oceanbase
{
namespace share
{
namespace schema
{
class ObRoutineInfo;
}
}
namespace sql
{
class ObCallProcedureStmt;
class ObCallProcedureInfo;
class ObCallProcedureResolver: public ObCMDResolver
{
public:
  explicit ObCallProcedureResolver(ObResolverParams &params) : ObCMDResolver(params) {}
  virtual ~ObCallProcedureResolver() {}

  virtual int resolve(const ParseNode &parse_tree);
private:
  int resolve_cparams(const ParseNode* params_node,
                      const share::schema::ObRoutineInfo *routien_info,
                      ObCallProcedureInfo *call_proc_info,
                      ObIArray<ObRawExpr*> &params,
                      pl::ObPLDependencyTable &deps);
  int resolve_cparam_without_assign(const ParseNode *param_node,
                      const int64_t position,
                      common::ObIArray<ObRawExpr*> &params,
                      pl::ObPLDependencyTable &deps);
  int resolve_cparam_with_assign(const ParseNode *param_node,
                      const share::schema::ObRoutineInfo *routine_info,
                      common::ObIArray<ObRawExpr*> &params,
                      pl::ObPLDependencyTable &deps);
  int resolve_param_exprs(const ParseNode *params_node,
                      ObIArray<ObRawExpr*> &expr_params);
  int check_param_expr_legal(ObRawExpr *param);
  int find_call_proc_info(ObCallProcedureStmt &stmt);
  int add_call_proc_info(ObCallProcedureInfo *call_info);
  int generate_pl_cache_ctx(pl::ObPLCacheCtx &pc_ctx);
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObCallProcedureResolver);
  // function members

private:
  // data members

};

} // end namespace sql
} // end namespace oceanbase



#endif /* OCEANBASE_SRC_SQL_RESOLVER_DDL_OB_CALL_PROCEDURE_RESOLVER_H_ */
