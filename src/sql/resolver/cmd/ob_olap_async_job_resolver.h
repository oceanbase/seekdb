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

#ifndef _OB_OLAP_ASYNC_JOB_RESOLVER_H
#define _OB_OLAP_ASYNC_JOB_RESOLVER_H 1

#include "sql/resolver/dml/ob_select_resolver.h"
#include "sql/resolver/cmd/ob_olap_async_job_stmt.h"
namespace oceanbase
{
namespace sql
{
class ObOLAPAsyncJobResolver: public ObSelectResolver
{
public:
  explicit ObOLAPAsyncJobResolver(ObResolverParams &params);
  virtual ~ObOLAPAsyncJobResolver() = default;

  virtual int resolve(const ParseNode &parse_tree);
private:
  static const int OB_JOB_NAME_MAX_LENGTH = 128;
  static const int OB_JOB_SQL_MAX_LENGTH = 4096;
  int resolve_submit_job_stmt(const ParseNode &parse_tree, ObOLAPAsyncSubmitJobStmt &stmt);
  int resolve_cancel_job_stmt(const ParseNode &parse_tree, ObOLAPAsyncCancelJobStmt *stmt);
  int execute_submit_job(ObOLAPAsyncSubmitJobStmt &stmt);
  int init_select_stmt(ObOLAPAsyncSubmitJobStmt &stmt);
  int parse_and_resolve_select_sql(const common::ObString &select_sql);
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObOLAPAsyncJobResolver);
};
}//namespace sql
}//namespace oceanbase
#endif // _OB_OLAP_ASYNC_JOB_RESOLVER_H
