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
#ifndef OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_TO_PINYIN_TABLE_
#define OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_TO_PINYIN_TABLE_

#include <cstdint>
#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace sql
{
// The content of `begin` and `end` in `PINYIN_TABLE` comes from the file `cldr-common-33.0.zip:common/collation/zh.xml:35~1558`;
# define PINYIN_COUNT 1502
struct PinyinPair{
  uint64_t begin;
  uint64_t end;
  ObString pinyin;
};
extern PinyinPair PINYIN_TABLE[PINYIN_COUNT];
} // end namespace sql
} // end namespace oceanbase
#endif /* OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_TO_PINYIN_ */
