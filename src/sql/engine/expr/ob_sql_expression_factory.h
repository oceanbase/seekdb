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

#ifndef OCEANBASE_SQL_OB_SQL_EXPRESSION_FACTORY_H
#define OCEANBASE_SQL_OB_SQL_EXPRESSION_FACTORY_H

#include "lib/container/ob_array.h"
namespace oceanbase
{
namespace sql
{
// Do not provide a free(sql-expression) interface, please release it uniformly on alloc
// It is best for expr_op and sql-expression to use the same alloc, otherwise it requires the user
// Release the space occupied by expr_op
class ObSqlExpressionFactory
{
public:
  explicit  ObSqlExpressionFactory(common::ObIAllocator &alloc) : alloc_(alloc)
  {}
  ~ObSqlExpressionFactory()
  {
  }

  template<typename T>
      int alloc(T *&sql_expression)
      {
        int ret = common::OB_SUCCESS;
        void *ptr = NULL;
        int64_t item_count = 0;
        if (OB_ISNULL(ptr = (alloc_.alloc(sizeof(T))))) {
          ret = common::OB_ALLOCATE_MEMORY_FAILED;
          SQL_ENG_LOG(ERROR, "fail to alloc memory", K(ret), K(ptr));
        } else {
          sql_expression = new(ptr)T(alloc_, item_count);
        }
        return ret;
      }

  template<typename T>
      int alloc(T *&sql_expression, int64_t item_count)
      {
        int ret = common::OB_SUCCESS;
        void *ptr = NULL;
        if (OB_ISNULL(ptr = (alloc_.alloc(sizeof(T))))) {
          ret = common::OB_ALLOCATE_MEMORY_FAILED;
          SQL_ENG_LOG(ERROR, "fail to alloc memory", K(ret), K(ptr));
        } else {
          sql_expression = new(ptr)T(alloc_, item_count);
        }
        return ret;
      }
  void destroy()
  {
    //nothing todo
    // All memory is allocated on alloc, and will be released by allocunified release
  }
  //template<typename T>
  //    void free(T *&sql_expression)
  //    {
  //      if (OB_ISNULL(sql_expression)) {
  //      } else {
  //        sql_expression->~ObSqlExpression();
  //        alloc_.free(sql_expression);
  //        sql_expression = NULL;
  //      }
  //    }
private:
  DISALLOW_COPY_AND_ASSIGN(ObSqlExpressionFactory);
private:
  common::ObIAllocator &alloc_;
};
} //namespace sql
} //namespace oceanbase
#endif

