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

#ifndef _OB_LOCATION_UTILS_EXECUTOR_H
#define _OB_LOCATION_UTILS_EXECUTOR_H 1
#include "sql/resolver/cmd/ob_location_utils_stmt.h"
namespace oceanbase
{
namespace sql
{
class ObExecContext;
class ObLocationUtilsExecutor
{
public:
  ObLocationUtilsExecutor() {}
  virtual ~ObLocationUtilsExecutor() {}

  int execute(ObExecContext &ctx, ObLocationUtilsStmt &stmt);
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObLocationUtilsExecutor);
private:
  // data members
};

} // end namespace sql
} // end namespace oceanbase

#endif /* _OB_LOCATION_UTILS_EXECUTOR_H */
