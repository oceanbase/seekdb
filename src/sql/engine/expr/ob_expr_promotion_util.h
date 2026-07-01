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

#ifndef _OB_EXPR_PROMOTION_UTIL_H_
#define _OB_EXPR_PROMOTION_UTIL_H_

//#include "sql/engine/expr/ob_expr_operator.h"
#include "common/object/ob_obj_type.h"
//#include "common/expression/ob_expr_string_buf.h"
//#include "lib/timezone/ob_timezone_info.h"

namespace oceanbase
{
namespace sql
{
  class ObExprResType;
  class ObExprPromotionUtil
  {
  public:
    static int get_nvl_type(
      ObExprResType &type,
      const ObExprResType &type1,
      const ObExprResType &type2);
  private:
    static int get_calc_type(
      ObExprResType &type,
      const ObExprResType &type1,
      const ObExprResType &type2,
      const common::ObObjType map[common::ObMaxTC][common::ObMaxTC]);
};

}
}
#endif  /* _OB_EXPR_PROMOTION_UTIL_H_ */
