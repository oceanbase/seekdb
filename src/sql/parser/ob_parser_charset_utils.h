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

#ifndef OCEANBASE_COMMON_OB_PARSER_CHARSET_UTILS_H
#define OCEANBASE_COMMON_OB_PARSER_CHARSET_UTILS_H

#ifdef __cplusplus
extern "C"
{
#endif


typedef enum ObCharsetParserType_ {
  CHARSET_PARSER_TYPE_NONE = 0,
  CHARSET_PARSER_TYPE_GB,
  CHARSET_PARSER_TYPE_SINGLE_BYTE,
  CHARSET_PARSER_TYPE_UTF8MB4,
  CHARSET_PARSER_TYPE_HKSCS,
  CHARSET_PARSER_TYPE_MAX,
} ObCharsetParserType;


#ifdef __cplusplus
}
#endif
#endif //OCEANBASE_COMMON_OB_PARSER_CHARSET_UTILS_H
