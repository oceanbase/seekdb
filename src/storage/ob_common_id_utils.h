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

#ifndef OCEANBASE_STORAGE_OB_COMMON_ID_UTILS_H_
#define OCEANBASE_STORAGE_OB_COMMON_ID_UTILS_H_

#include "share/ob_common_id.h"             // ObCommonID

namespace oceanbase
{
namespace common
{
class ObMySQLProxy;
}

namespace storage
{

// Utils for ObCommonID
class ObCommonIDUtils
{
public:
  // Generate an ID unique within this server runtime. The ID is not monotonic.
  static int gen_unique_id(share::ObCommonID &id);
};

}
}

#endif /* OCEANBASE_STORAGE_OB_COMMON_ID_UTILS_H_ */
