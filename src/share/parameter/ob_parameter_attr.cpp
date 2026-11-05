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

#include "share/parameter/ob_parameter_attr.h"

namespace oceanbase {
namespace common {

#define _ATTR_STR(enum_str) #enum_str
#define _ATTR(enum_name)                                                       \
  [enum_name] = _ATTR_STR(enum_name)

#define DEF_ATTR_VALUES(ATTR_CLS, args...)                                     \
const char * ATTR_CLS::VALUES[] = {                                            \
  LST_DO(_ATTR, (,), args)                                                     \
}

DECL_ATTR_LIST(DEF_ATTR_VALUES);

bool ObParameterAttr::is_static() const
{
  return edit_level_ == EditLevel::STATIC_EFFECTIVE;
}

bool ObParameterAttr::is_readonly() const
{
  return edit_level_ == EditLevel::READONLY;
}

bool ObParameterAttr::is_invisible() const
{
  return visible_level_ == VisibleLevel::INVISIBLE;
}

} // common
} // oceanbase
