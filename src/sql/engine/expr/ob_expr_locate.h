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

#ifndef OB_EXPR_LOCATE_H_
#define OB_EXPR_LOCATE_H_
#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprLocate : public ObLocationExprOperator
{
public:
  explicit  ObExprLocate(common::ObIAllocator &alloc);
  virtual ~ObExprLocate();
  virtual int calc_result_typeN(ObExprResType &type,
                                ObExprResType *types_array,
                                int64_t param_num,
                                common::ObExprTypeCtx &type_ctx) const;

  DECLARE_SET_LOCAL_SESSION_VARS;

private:
  static const int8_t PARAM_NUM_TWO = 2;
  static const int8_t PARAM_NUM_THREE = 3;

  DISALLOW_COPY_AND_ASSIGN(ObExprLocate);
};


}//end of namespace sql
}//end of namespace oceanbase

#endif /* SRC_SQL_ENGINE_EXPR_OB_EXPR_LOCATE_H_ */
