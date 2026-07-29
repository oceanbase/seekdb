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
void __init_str_expr_cmp_func_part_3()
{
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF8MB4_PERSIAN_UCA_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF8MB4_ESPERANTO_UCA_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF8MB4_HUNGARIAN_UCA_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF8MB4_SINHALA_UCA_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF8MB4_GERMAN2_UCA_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF8MB4_CROATIAN_UCA_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF8MB4_UNICODE_520_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF8MB4_VIETNAMESE_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF16_ICELANDIC_UCA_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF16_LATVIAN_UCA_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF16_ROMANIAN_UCA_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF16_SLOVENIAN_UCA_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF16_POLISH_UCA_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF16_ESTONIAN_UCA_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF16_SPANISH_UCA_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF16_SWEDISH_UCA_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF16_TURKISH_UCA_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF16_CZECH_UCA_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF16_DANISH_UCA_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF16_LITHUANIAN_UCA_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF16_SLOVAK_UCA_CI);
}
} // end sql
} // end oceanbase
