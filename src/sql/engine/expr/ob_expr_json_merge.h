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


#ifndef OCEANBASE_SQL_OB_EXPR_JSON_MERGE_H_
#define OCEANBASE_SQL_OB_EXPR_JSON_MERGE_H_

#include "sql/engine/expr/ob_expr_operator.h"
#include "sql/engine/expr/ob_expr_json_merge_preserve.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{
class ObExprJsonMerge : public ObExprJsonMergePreserve
{
public:
  explicit ObExprJsonMerge(common::ObIAllocator &alloc);
  virtual ~ObExprJsonMerge();
private:
    DISALLOW_COPY_AND_ASSIGN(ObExprJsonMerge);
};

} // sql
} // oceanbase
#endif // OCEANBASE_SQL_OB_EXPR_JSON_MERGE_H_
