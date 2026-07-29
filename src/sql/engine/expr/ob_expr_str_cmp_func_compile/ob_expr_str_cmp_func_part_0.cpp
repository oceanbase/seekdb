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
void __init_str_expr_cmp_func_part_0()
{
  INIT_COMPILE_STR_FUNC(CS_TYPE_GBK_CHINESE_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF8MB4_GENERAL_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF8MB4_BIN);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF16_GENERAL_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF16_BIN);
  INIT_COMPILE_STR_FUNC(CS_TYPE_BINARY);
  INIT_COMPILE_STR_FUNC(CS_TYPE_GBK_BIN);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF16_UNICODE_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UTF8MB4_UNICODE_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_GB18030_CHINESE_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_GB18030_BIN);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UJIS_JAPANESE_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_UJIS_BIN);
  INIT_COMPILE_STR_FUNC(CS_TYPE_EUCKR_KOREAN_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_EUCKR_BIN);
  INIT_COMPILE_STR_FUNC(CS_TYPE_CP932_JAPANESE_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_CP932_BIN);
  INIT_COMPILE_STR_FUNC(CS_TYPE_EUCJPMS_JAPANESE_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_EUCJPMS_BIN);
  INIT_COMPILE_STR_FUNC(CS_TYPE_LATIN1_GERMAN1_CI);
  INIT_COMPILE_STR_FUNC(CS_TYPE_LATIN1_SWEDISH_CI);
}
} // end sql
} // end oceanbase
