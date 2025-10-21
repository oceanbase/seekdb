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

#ifndef OCEANBASE_LIB_OB_HEX_UTILS_BASE_H_
#define OCEANBASE_LIB_OB_HEX_UTILS_BASE_H_

#include "common/object/ob_object.h"
#include "lib/string/ob_string.h"
#include "lib/allocator/ob_allocator.h"

namespace oceanbase
{
namespace common
{

/*
 * move unhex and hex function from ObHexUtils here, because we need them in
 * deps/oblib/src/lib/mysqlclient/ob_mysql_result_impl.cpp for raw type.
 */
class ObHexUtilsBase
{
public:
  static int unhex(const ObString &text, ObIAllocator &alloc, ObObj &result);
  static int unhex(const ObString &text, ObIAllocator &alloc, char *&binary_buf, int64_t &binary_len);
  static int hex(ObString &text, ObIAllocator &alloc, const char *binary_buf, int64_t binary_len);
};
} // end namespace common
} // end namespace oceanbase

#endif // OCEANBASE_LIB_OB_HEX_UTILS_BASE_H_
