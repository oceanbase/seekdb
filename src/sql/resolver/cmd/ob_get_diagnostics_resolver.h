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

#ifndef _OB_GET_DIAGNOSTICS_RESOLVER_H
#define _OB_GET_DIAGNOSTICS_RESOLVER_H
#include "sql/resolver/dml/ob_select_resolver.h"
#include "sql/resolver/cmd/ob_get_diagnostics_stmt.h"

namespace oceanbase
{
namespace sql
{
class ObGetDiagnosticsResolver : public ObStmtResolver
{
public:
    explicit ObGetDiagnosticsResolver(ObResolverParams &params);
    virtual ~ObGetDiagnosticsResolver();
    virtual int resolve(const ParseNode &parse_tree);
    int set_diagnostics_type(ObGetDiagnosticsStmt *diagnostics_stmt, const bool &is_current, const bool &is_cond);
private:
    class ObSqlStrGenerator;
    DISALLOW_COPY_AND_ASSIGN(ObGetDiagnosticsResolver);
};


}
}
#endif
