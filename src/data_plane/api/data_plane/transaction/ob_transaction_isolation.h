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

#ifndef OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_TRANSACTION_ISOLATION_H_
#define OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_TRANSACTION_ISOLATION_H_

#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace transaction
{

class ObTransIsolation
{
public:
  enum {
    UNKNOWN = -1,
    READ_UNCOMMITTED = 0,
    READ_COMMITED = 1,
    REPEATABLE_READ = 2,
    SERIALIZABLE = 3,
    MAX_LEVEL
  };
  static const common::ObString LEVEL_NAME[MAX_LEVEL];

  static bool is_valid(const int32_t level)
  {
    return level == READ_UNCOMMITTED
        || level == READ_COMMITED
        || level == REPEATABLE_READ
        || level == SERIALIZABLE;
  }
  static int32_t get_level(const common::ObString &level_name);
  static const common::ObString &get_name(int32_t level);

private:
  ObTransIsolation() {}
  ~ObTransIsolation() {}
};

} // namespace transaction
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_TRANSACTION_ISOLATION_H_
