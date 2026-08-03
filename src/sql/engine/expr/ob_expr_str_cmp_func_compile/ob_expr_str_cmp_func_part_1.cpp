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

#include "ob_expr_str_cmp_func_common.ipp"

namespace oceanbase
{
namespace sql
{
void __init_str_expr_cmp_func_part_1()
{
  INIT_COMPILE_STR_FUNC(CS_TYPE_LATIN1_DANISH_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_LATIN1_GERMAN2_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_LATIN1_BIN);
  INIT_COMPILE_STR_FUNC(CS_TYPE_LATIN1_GENERAL_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_LATIN1_GENERAL_CS);
  INIT_COMPILE_STR_FUNC(CS_TYPE_LATIN1_SPANISH_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_GB2312_CHINESE_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_GB2312_BIN);
  INIT_COMPILE_STR_FUNC(CS_TYPE_GB18030_2022_BIN);
  INIT_COMPILE_STR_FUNC(CS_TYPE_GB18030_2022_PINYIN_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_GB18030_2022_PINYIN_CS);
  INIT_COMPILE_STR_FUNC(CS_TYPE_GB18030_2022_RADICAL_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_GB18030_2022_RADICAL_CS);
  INIT_COMPILE_STR_FUNC(CS_TYPE_GB18030_2022_STROKE_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_GB18030_2022_STROKE_CS);
  INIT_COMPILE_STR_FUNC(CS_TYPE_ASCII_GENERAL_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_ASCII_BIN);
  INIT_COMPILE_STR_FUNC(CS_TYPE_TIS620_THAI_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_TIS620_BIN);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF16LE_GENERAL_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF16LE_BIN);
}
} // end sql
} // end oceanbase
