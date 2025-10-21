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

#ifndef _OB_SET_NAMES_EXECUTOR_H
#define _OB_SET_NAMES_EXECUTOR_H 1
#include "sql/resolver/cmd/ob_set_names_stmt.h"
namespace oceanbase
{
namespace sql
{
class ObExecContext;
class ObSetNamesExecutor
{
public:
  ObSetNamesExecutor() {}
  virtual ~ObSetNamesExecutor() {}

  int execute(ObExecContext &ctx, ObSetNamesStmt &stmt);
private:
  int get_global_sys_var_character_set_client(ObExecContext &ctx, common::ObString &character_set_client) const;
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObSetNamesExecutor);
  // function members
private:
  // data members
};

} // end namespace sql
} // end namespace oceanbase

#endif /* _OB_SET_NAMES_EXECUTOR_H */
