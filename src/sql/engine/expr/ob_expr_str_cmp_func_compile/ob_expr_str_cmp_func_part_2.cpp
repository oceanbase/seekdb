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
void __init_str_expr_cmp_func_part_2()
{
  INIT_COMPILE_STR_FUNC(CS_TYPE_SJIS_JAPANESE_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_SJIS_BIN);
  INIT_COMPILE_STR_FUNC(CS_TYPE_BIG5_CHINESE_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_BIG5_BIN);
  INIT_COMPILE_STR_FUNC(CS_TYPE_HKSCS_BIN);
  INIT_COMPILE_STR_FUNC(CS_TYPE_HKSCS31_BIN);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF8MB4_ICELANDIC_UCA_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF8MB4_LATVIAN_UCA_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF8MB4_ROMANIAN_UCA_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF8MB4_SLOVENIAN_UCA_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF8MB4_POLISH_UCA_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF8MB4_ESTONIAN_UCA_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF8MB4_SPANISH_UCA_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF8MB4_SWEDISH_UCA_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF8MB4_TURKISH_UCA_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF8MB4_CZECH_UCA_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF8MB4_DANISH_UCA_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF8MB4_LITHUANIAN_UCA_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF8MB4_SLOVAK_UCA_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF8MB4_SPANISH2_UCA_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF8MB4_ROMAN_UCA_CI);
}
} // end sql
} // end oceanbase
