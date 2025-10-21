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

#ifndef OCEANBASE_SHARE_OB_JSON_ACCESS_UTILS_
#define OCEANBASE_SHARE_OB_JSON_ACCESS_UTILS_

#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace common {
class ObIJsonBase;
class ObIAllocator;
}
namespace share
{
class ObJsonWrapper
{
public:
  static int get_raw_binary(common::ObIJsonBase *j_base, common::ObString &result, common::ObIAllocator *allocator);
};

} // end namespace share
} // end namespace oceanbase
#endif
