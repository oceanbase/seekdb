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
#ifndef OB_DEFINED_COLLATIONS_H_
#define OB_DEFINED_COLLATIONS_H_

#include "lib/charset/ob_charset.h"
namespace oceanbase
{
namespace common
{
namespace datum_cmp
{
template<ObCollationType coll>
struct SupportedCollection
{
  static const bool defined_ = false;
};

#define DEF_SUPPORT_COLL(cs)                                                                     \
  template <>                                                                                    \
  struct SupportedCollection<cs>                                                                 \
  {                                                                                              \
    static const bool defined_ = true;                                                           \
  };

DEF_SUPPORT_COLL(CS_TYPE_BINARY)
DEF_SUPPORT_COLL(CS_TYPE_UTF8MB4_GENERAL_CI)
DEF_SUPPORT_COLL(CS_TYPE_UTF8MB4_BIN)
} // end datum_cmp
} // end common
} // end oceanbase

#endif//OB_DEFINED_COLLATIONS_H_
