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

#ifndef OCEANBASE_COMMON_OB_ZONE_TYPE_H_
#define OCEANBASE_COMMON_OB_ZONE_TYPE_H_
#include <stdint.h>

namespace oceanbase
{
namespace common
{
const int64_t MAX_ZONE_TYPE_LENGTH = 128;
enum ObZoneType
{
  ZONE_TYPE_READWRITE = 0,
  ZONE_TYPE_READONLY = 1,
  ZONE_TYPE_ENCRYPTION = 2,
  ZONE_TYPE_INVALID = 3,
};

const char *zone_type_to_str(ObZoneType zone_type);
ObZoneType str_to_zone_type(const char *zone_type_str);

}//end namespace common
}//end namespace oceanbase

#endif //OCEANBASE_COMMON_OB_ZONE_TYPE_H_
