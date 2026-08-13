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

#ifndef OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_TRANSACTION_VERSION_H_
#define OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_TRANSACTION_VERSION_H_

#include <cstdint>

namespace oceanbase
{
namespace transaction
{

class ObTransVersion
{
public:
  static const int64_t INVALID_TRANS_VERSION = -1;
  static const int64_t MAX_TRANS_VERSION = INT64_MAX;
  static bool is_valid(const int64_t trans_version) { return trans_version >= 0; }
};

} // namespace transaction
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_TRANSACTION_VERSION_H_
