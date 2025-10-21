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

#include "sql/engine/expr/ob_expr_cmp_func.ipp"

namespace oceanbase
{
namespace sql
{

#define DEF_COMPILE_STR_FUNC_INIT(COLLATION, unit_idx)                                                     \
  void __init_str_expr_cmp_func##unit_idx()                                                                \
  {                                                                                                        \
    StrExprFuncIniter<COLLATION, CO_EQ>::init_array();                                                     \
    StrExprFuncIniter<COLLATION, CO_LE>::init_array();                                                     \
    StrExprFuncIniter<COLLATION, CO_LT>::init_array();                                                     \
    StrExprFuncIniter<COLLATION, CO_GE>::init_array();                                                     \
    StrExprFuncIniter<COLLATION, CO_GT>::init_array();                                                     \
    StrExprFuncIniter<COLLATION, CO_NE>::init_array();                                                     \
    StrExprFuncIniter<COLLATION, CO_CMP>::init_array();                                                    \
    DatumStrExprCmpIniter<COLLATION>::init_array();                                                        \
    TextExprFuncIniter<COLLATION, CO_EQ>::init_array();                                                    \
    TextExprFuncIniter<COLLATION, CO_LE>::init_array();                                                    \
    TextExprFuncIniter<COLLATION, CO_LT>::init_array();                                                    \
    TextExprFuncIniter<COLLATION, CO_GE>::init_array();                                                    \
    TextExprFuncIniter<COLLATION, CO_GT>::init_array();                                                    \
    TextExprFuncIniter<COLLATION, CO_NE>::init_array();                                                    \
    TextExprFuncIniter<COLLATION, CO_CMP>::init_array();                                                   \
    DatumTextExprCmpIniter<COLLATION>::init_array();                                                       \
    TextStrExprFuncIniter<COLLATION, CO_EQ>::init_array();                                                 \
    TextStrExprFuncIniter<COLLATION, CO_LE>::init_array();                                                 \
    TextStrExprFuncIniter<COLLATION, CO_LT>::init_array();                                                 \
    TextStrExprFuncIniter<COLLATION, CO_GE>::init_array();                                                 \
    TextStrExprFuncIniter<COLLATION, CO_GT>::init_array();                                                 \
    TextStrExprFuncIniter<COLLATION, CO_NE>::init_array();                                                 \
    TextStrExprFuncIniter<COLLATION, CO_CMP>::init_array();                                                \
    DatumTextStrExprCmpIniter<COLLATION>::init_array();                                                    \
    StrTextExprFuncIniter<COLLATION, CO_EQ>::init_array();                                                 \
    StrTextExprFuncIniter<COLLATION, CO_LE>::init_array();                                                 \
    StrTextExprFuncIniter<COLLATION, CO_LT>::init_array();                                                 \
    StrTextExprFuncIniter<COLLATION, CO_GE>::init_array();                                                 \
    StrTextExprFuncIniter<COLLATION, CO_GT>::init_array();                                                 \
    StrTextExprFuncIniter<COLLATION, CO_NE>::init_array();                                                 \
    StrTextExprFuncIniter<COLLATION, CO_CMP>::init_array();                                                \
    DatumStrTextExprCmpIniter<COLLATION>::init_array();                                                    \
  }

} // end sql
} // end oceanbase
